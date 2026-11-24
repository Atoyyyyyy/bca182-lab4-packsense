/*
 * sensor_task.c: SensorTask (IMU 100 Hz, ALS/PS 20 Hz) and EnvTask (AHT 1 Hz).
 * See sensor_task.h.
 *
 * Fault handling (PRD 6.4): SENSOR_FAIL_LIMIT consecutive failed reads put a
 * sensor in DEGRADED; it is then not read, its samples are flagged, and a
 * re-init is tried every SENSOR_REINIT_MS. The IMU is re-initialised by
 * SensorTask itself (its samples are invalid anyway); the AP3216C and the AHT
 * by EnvTask, so their slow init never delays the 100 Hz IMU reads.
 * `fault inject` makes a sensor's reads and re-inits fail as a NACK would.
 */
#include "sensor_task.h"

#include <string.h>

#include "task.h"

#include "app/wdg.h"
#include "app_config.h"
#include "drivers/aht.h"
#include "drivers/ap3216c.h"
#include "drivers/console_uart.h"
#include "drivers/icm20608.h"

static CCM_BSS StackType_t s_sensor_stack[SENSOR_TASK_STACK_WORDS];
static CCM_BSS StaticTask_t s_sensor_tcb;
static CCM_BSS StackType_t s_env_stack[ENV_TASK_STACK_WORDS];
static CCM_BSS StaticTask_t s_env_tcb;

static StaticQueue_t s_q_buf;
static uint8_t s_q_storage[SENSOR_QUEUE_LEN * sizeof(sample_t)];
static QueueHandle_t s_q;

static TaskHandle_t s_sensor_task;
static TaskHandle_t s_env_task;
static volatile bool s_run;

static sensor_stats_t s_stats;
static sensor_health_t s_health[SENS_COUNT];
static volatile uint32_t s_inject; /* bit per sensor_id_t */
static TickType_t s_reinit_t[SENS_COUNT];

/* Latest environment values, written by EnvTask, read by SensorTask. */
static volatile float s_temp, s_rh;
static volatile bool s_env_ok;

static const char *const k_sensor_names[SENS_COUNT] = {"IMU", "ALS/PS", "AHT"};

const char *sensor_name(sensor_id_t id)
{
    return id < SENS_COUNT ? k_sensor_names[id] : "?";
}

static bool injected(sensor_id_t id)
{
    return (s_inject & (1UL << id)) != 0U;
}

/* Count a read result; returns true when the sensor just became DEGRADED. */
static void note_result(sensor_id_t id, bool ok)
{
    sensor_health_t *h = &s_health[id];
    if (ok) {
        h->consecutive = 0U;
        return;
    }
    h->failures++;
    h->consecutive++;
    if (!h->degraded && h->consecutive >= SENSOR_FAIL_LIMIT) {
        h->degraded = true;
        h->degraded_events++;
        s_reinit_t[id] = xTaskGetTickCount();
        console_printf("[sensor] t=%lu %s DEGRADED after %lu failed reads%s; re-init every %u ms\r\n",
                       (unsigned long)xTaskGetTickCount(), k_sensor_names[id], (unsigned long)h->consecutive,
                       injected(id) ? " (injected fault)" : "", SENSOR_REINIT_MS);
    }
}

/* DEGRADED: try a re-init when the period is up. Returns true on recovery. */
static bool try_reinit(sensor_id_t id)
{
    sensor_health_t *h = &s_health[id];
    const TickType_t now = xTaskGetTickCount();
    if (!h->degraded || (now - s_reinit_t[id]) < pdMS_TO_TICKS(SENSOR_REINIT_MS)) {
        return false;
    }
    s_reinit_t[id] = now;
    h->reinit_tries++;
    status_t st = ST_NACK;
    if (!injected(id)) {
        switch (id) {
        case SENS_IMU: st = icm20608_init(); break;
        case SENS_ALS: st = ap3216c_init(); break;
        case SENS_AHT: st = aht_init(); break;
        default: break;
        }
    }
    if (st != ST_OK) {
        return false;
    }
    h->consecutive = 0U;
    h->recoveries++;
    h->degraded = false;
    console_printf("[sensor] t=%lu %s recovered (re-init %lu)\r\n", (unsigned long)xTaskGetTickCount(),
                   k_sensor_names[id], (unsigned long)h->reinit_tries);
    return true;
}

static void sensor_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (!s_run) {
            wdg_checkin(WDG_SENSOR);
            (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500));
            continue;
        }
        TickType_t wake = xTaskGetTickCount();
        uint32_t tick = 0U;
        uint32_t last_t = 0U;
        bool have_last = false;
        float als = 0.0f;
        uint16_t ps = 0U;
        uint8_t ps_valid = 0U;

        while (s_run) {
            wdg_checkin(WDG_SENSOR);
            sample_t s;
            memset(&s, 0, sizeof s);

            if (s_health[SENS_IMU].degraded) {
                if (try_reinit(SENS_IMU)) {
                    wake = xTaskGetTickCount(); /* no burst of catch-up reads */
                }
            }
            icm_sample_t imu;
            status_t st = ST_NOT_PRESENT;
            if (!s_health[SENS_IMU].degraded) {
                st = injected(SENS_IMU) ? ST_NACK : icm20608_read(&imu);
                note_result(SENS_IMU, st == ST_OK || st == ST_RANGE_ERR);
            }
            s.t_ms = (uint32_t)xTaskGetTickCount();
            if (st == ST_OK || st == ST_RANGE_ERR) {
                s.ax = imu.ax;
                s.ay = imu.ay;
                s.az = imu.az;
                s.gx = imu.gx;
                s.gy = imu.gy;
                s.gz = imu.gz;
                if (imu.saturated) {
                    s.flags |= SF_IMU_SAT;
                }
            } else {
                s.flags |= SF_IMU_ERR;
                s_stats.imu_errors++;
            }

            if ((tick % SENSOR_ALS_EVERY) == 0U) {
                s.flags |= SF_ALS_NEW;
                status_t sa = ST_NOT_PRESENT;
                ap_sample_t a;
                if (!s_health[SENS_ALS].degraded) {
                    sa = injected(SENS_ALS) ? ST_NACK : ap3216c_read(&a);
                    note_result(SENS_ALS, sa == ST_OK || sa == ST_RANGE_ERR);
                }
                if (sa == ST_OK || sa == ST_RANGE_ERR) {
                    als = a.lux;
                    ps = a.ps;
                    ps_valid = (sa == ST_OK && !a.ir_overflow) ? 1U : 0U;
                } else {
                    ps_valid = 0U;
                    s_stats.als_errors++;
                }
            }
            if (s_health[SENS_ALS].degraded || s_health[SENS_ALS].consecutive > 0U) {
                s.flags |= SF_ALS_ERR; /* als/ps held from the last good read */
            }
            s.als = als;
            s.ps = ps;
            s.ps_valid = ps_valid;

            s.temp = s_temp;
            s.rh = s_rh;
            if (!s_env_ok) {
                s.flags |= SF_ENV_ERR;
            }

            if (have_last) {
                const uint32_t dt = s.t_ms - last_t;
                if (dt > s_stats.max_period_ms) {
                    s_stats.max_period_ms = dt;
                }
                if (dt > 3U * SENSOR_PERIOD_MS) {
                    s.flags |= SF_GAP;
                    s_stats.gaps++;
                }
            }
            last_t = s.t_ms;
            have_last = true;

            s_stats.samples++;
            if (xQueueSend(s_q, &s, 0) != pdPASS) {
                s_stats.queue_full++;
            }
            tick++;
            (void)xTaskDelayUntil(&wake, pdMS_TO_TICKS(SENSOR_PERIOD_MS));
        }
    }
}

static void env_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (!s_run) {
            wdg_checkin(WDG_ENV);
            (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500));
            continue;
        }
        TickType_t wake = xTaskGetTickCount();
        while (s_run) {
            wdg_checkin(WDG_ENV);
            (void)try_reinit(SENS_ALS);
            if (s_health[SENS_AHT].degraded) {
                (void)try_reinit(SENS_AHT);
            }
            if (!s_health[SENS_AHT].degraded) {
                aht_sample_t e;
                const status_t st = injected(SENS_AHT) ? ST_NACK : aht_read(&e);
                s_stats.env_reads++;
                note_result(SENS_AHT, st == ST_OK);
                if (st == ST_OK) {
                    s_temp = e.temp_c;
                    s_rh = e.rh;
                    s_env_ok = true;
                } else {
                    s_env_ok = false;
                    s_stats.env_errors++;
                }
            } else {
                s_env_ok = false;
            }
            wdg_checkin(WDG_ENV);
            (void)xTaskDelayUntil(&wake, pdMS_TO_TICKS(ENV_PERIOD_MS));
        }
    }
}

void sensor_tasks_create(void)
{
    s_q = xQueueCreateStatic(SENSOR_QUEUE_LEN, sizeof(sample_t), s_q_storage, &s_q_buf);
    s_sensor_task = xTaskCreateStatic(sensor_task, "Sensor", SENSOR_TASK_STACK_WORDS, NULL, SENSOR_TASK_PRIO,
                                      s_sensor_stack, &s_sensor_tcb);
    s_env_task = xTaskCreateStatic(env_task, "Env", ENV_TASK_STACK_WORDS, NULL, ENV_TASK_PRIO, s_env_stack,
                                   &s_env_tcb);
    configASSERT(s_q != NULL && s_sensor_task != NULL && s_env_task != NULL);
}

/* Init one sensor if needed; a failure starts it DEGRADED (re-init later). */
static bool init_if_needed(sensor_id_t id)
{
    bool ok;
    switch (id) {
    case SENS_IMU: {
        icm_info_t i;
        icm20608_get_info(&i);
        ok = i.initialised || icm20608_init() == ST_OK;
        break;
    }
    case SENS_ALS: {
        ap_info_t i;
        ap3216c_get_info(&i);
        ok = i.initialised || ap3216c_init() == ST_OK;
        break;
    }
    default: {
        aht_info_t i;
        aht_get_info(&i);
        ok = i.initialised || aht_init() == ST_OK;
        break;
    }
    }
    ok = ok && !injected(id);
    if (!ok && !s_health[id].degraded) {
        s_health[id].degraded = true;
        s_health[id].degraded_events++;
        s_reinit_t[id] = xTaskGetTickCount();
        console_printf("[sensor] t=%lu %s init failed: DEGRADED, re-init every %u ms\r\n",
                       (unsigned long)xTaskGetTickCount(), k_sensor_names[id], SENSOR_REINIT_MS);
    }
    return ok;
}

static void start(void)
{
    (void)xQueueReset(s_q);
    s_stats.running = true;
    s_run = true;
    xTaskNotifyGive(s_sensor_task);
    xTaskNotifyGive(s_env_task);
}

bool sensors_start(void)
{
    icm_info_t ii;
    icm20608_get_info(&ii);
    if (!ii.initialised && icm20608_init() != ST_OK) {
        return false;
    }
    (void)init_if_needed(SENS_ALS);
    (void)init_if_needed(SENS_AHT);
    start();
    return true;
}

void sensors_start_any(void)
{
    (void)init_if_needed(SENS_IMU);
    (void)init_if_needed(SENS_ALS);
    (void)init_if_needed(SENS_AHT);
    start();
}

void sensors_stop(void)
{
    s_run = false;
    s_stats.running = false;
}

QueueHandle_t sensors_queue(void)
{
    return s_q;
}

void sensors_get_stats(sensor_stats_t *out)
{
    taskENTER_CRITICAL();
    *out = s_stats;
    taskEXIT_CRITICAL();
}

void sensors_reset_stats(void)
{
    taskENTER_CRITICAL();
    const bool r = s_stats.running;
    memset(&s_stats, 0, sizeof s_stats);
    s_stats.running = r;
    taskEXIT_CRITICAL();
}

void sensors_get_health(sensor_health_t out[SENS_COUNT])
{
    taskENTER_CRITICAL();
    for (unsigned i = 0; i < SENS_COUNT; i++) {
        out[i] = s_health[i];
        out[i].injected = injected((sensor_id_t)i);
    }
    taskEXIT_CRITICAL();
}

uint32_t sensors_fault_mask(void)
{
    uint32_t m = 0U;
    for (unsigned i = 0; i < SENS_COUNT; i++) {
        if (s_health[i].degraded) {
            m |= 1UL << i;
        }
    }
    return m;
}

void sensors_inject(sensor_id_t id, bool on)
{
    if (id >= SENS_COUNT) {
        return;
    }
    taskENTER_CRITICAL();
    if (on) {
        s_inject |= 1UL << id;
    } else {
        s_inject &= ~(1UL << id);
    }
    taskEXIT_CRITICAL();
}
