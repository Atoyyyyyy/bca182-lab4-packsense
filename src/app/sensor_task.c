/*
 * sensor_task.c: SensorTask (IMU 100 Hz, ALS/PS 20 Hz) and EnvTask (AHT 1 Hz).
 * See sensor_task.h.
 */
#include "sensor_task.h"

#include <string.h>

#include "task.h"

#include "app_config.h"
#include "drivers/aht.h"
#include "drivers/ap3216c.h"
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

/* Latest environment values, written by EnvTask, read by SensorTask. */
static volatile float s_temp, s_rh;
static volatile bool s_env_ok;

static void sensor_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (!s_run) {
            (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
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
            sample_t s;
            memset(&s, 0, sizeof s);

            icm_sample_t imu;
            const status_t st = icm20608_read(&imu);
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
                ap_sample_t a;
                const status_t sa = ap3216c_read(&a);
                s.flags |= SF_ALS_NEW;
                if (sa == ST_OK || sa == ST_RANGE_ERR) {
                    als = a.lux;
                    ps = a.ps;
                    ps_valid = (sa == ST_OK && !a.ir_overflow) ? 1U : 0U;
                } else {
                    ps_valid = 0U;
                    s.flags |= SF_ALS_ERR;
                    s_stats.als_errors++;
                }
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
            (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }
        TickType_t wake = xTaskGetTickCount();
        while (s_run) {
            aht_sample_t e;
            const status_t st = aht_read(&e);
            s_stats.env_reads++;
            if (st == ST_OK) {
                s_temp = e.temp_c;
                s_rh = e.rh;
                s_env_ok = true;
            } else {
                s_env_ok = false;
                s_stats.env_errors++;
            }
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

bool sensors_start(void)
{
    icm_info_t ii;
    icm20608_get_info(&ii);
    if (!ii.initialised && icm20608_init() != ST_OK) {
        return false;
    }
    ap_info_t ai;
    ap3216c_get_info(&ai);
    if (!ai.initialised) {
        (void)ap3216c_init(); /* failures show up as SF_ALS_ERR */
    }
    aht_info_t hi;
    aht_get_info(&hi);
    if (!hi.initialised) {
        (void)aht_init();
    }
    (void)xQueueReset(s_q);
    s_stats.running = true;
    s_run = true;
    xTaskNotifyGive(s_sensor_task);
    xTaskNotifyGive(s_env_task);
    return true;
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
