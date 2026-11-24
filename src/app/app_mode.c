/*
 * app_mode.c: mode state machine (see app_mode.h).
 */
#include "app_mode.h"

#include <string.h>
#include <strings.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

#include "app/capture.h"
#include "app/cli.h"
#include "app/evlog.h"
#include "app/infer_task.h"
#include "drivers/console_uart.h"

static const char *const k_names[MODE_COUNT] = {"BOOT", "RUN", "GUARD", "CAPTURE", "DIAG"};

static volatile app_mode_t s_mode = MODE_BOOT;
static StaticQueue_t s_q_buf;
static uint8_t s_q_storage[2 * sizeof(app_mode_t)];
static QueueHandle_t s_q;
static StaticSemaphore_t s_done_buf;
static SemaphoreHandle_t s_done;
static StaticSemaphore_t s_req_mx_buf;
static SemaphoreHandle_t s_req_mx; /* one request at a time */

const char *app_mode_name(app_mode_t m)
{
    return m < MODE_COUNT ? k_names[m] : "?";
}

app_mode_t app_mode_get(void)
{
    return s_mode;
}

bool app_mode_is_product(app_mode_t m)
{
    return m == MODE_RUN || m == MODE_GUARD || m == MODE_DIAG;
}

app_mode_t app_mode_request(app_mode_t m)
{
    if (m >= MODE_COUNT || m == MODE_BOOT || s_q == NULL) {
        return s_mode;
    }
    (void)xSemaphoreTake(s_req_mx, portMAX_DELAY);
    (void)xSemaphoreTake(s_done, 0);
    if (xQueueSend(s_q, &m, pdMS_TO_TICKS(100)) == pdPASS) {
        (void)xSemaphoreTake(s_done, pdMS_TO_TICKS(10000));
    }
    xSemaphoreGive(s_req_mx);
    return s_mode;
}

void app_mode_init(void)
{
    s_q = xQueueCreateStatic(2, sizeof(app_mode_t), s_q_storage, &s_q_buf);
    s_done = xSemaphoreCreateBinaryStatic(&s_done_buf);
    s_req_mx = xSemaphoreCreateMutexStatic(&s_req_mx_buf);
    configASSERT(s_q != NULL && s_done != NULL && s_req_mx != NULL);
}

bool app_mode_take_request(app_mode_t *m)
{
    return xQueueReceive(s_q, m, 0) == pdPASS;
}

void app_mode_request_done(void)
{
    xSemaphoreGive(s_done);
}

app_mode_t app_mode_switch(app_mode_t to)
{
    const app_mode_t from = s_mode;
    if (to == from || to == MODE_BOOT || to >= MODE_COUNT) {
        return from;
    }
    if (to == MODE_CAPTURE) {
        if (infer_is_active()) {
            infer_set_active(false);
        }
        if (!capture_set(true)) {
            console_printf("[mode] t=%lu CAPTURE could not start; staying in the product modes\r\n",
                           (unsigned long)xTaskGetTickCount());
            to = (from == MODE_BOOT) ? MODE_RUN : from;
            infer_set_active(true);
            if (to == from) {
                return from;
            }
        }
    } else {
        if (from == MODE_CAPTURE) {
            (void)capture_set(false);
        }
        if (!infer_is_active()) {
            infer_set_active(true);
        }
    }
    s_mode = to;
    console_printf("[mode] t=%lu %s -> %s\r\n", (unsigned long)xTaskGetTickCount(), k_names[from], k_names[to]);
    evlog_post(EV_MODE, (uint8_t)to, 0xFFU, 0U, (uint32_t)to, (uint32_t)from);
    return to;
}

/* ---- console ------------------------------------------------------------------ */

static int cmd_mode(int argc, char **argv)
{
    if (argc == 0) {
        console_printf("[mode] %s\r\n", k_names[s_mode]);
        return 0;
    }
    if (argc != 1) {
        return 1;
    }
    for (int m = MODE_RUN; m < MODE_COUNT; m++) {
        if (strcasecmp(argv[0], k_names[m]) == 0) {
            const app_mode_t now = app_mode_request((app_mode_t)m);
            console_printf("[mode] now %s\r\n", k_names[now]);
            return now == (app_mode_t)m ? 0 : 1;
        }
    }
    return 1;
}

static const cli_cmd_t s_cmds[] = {
    {"mode", "[run|guard|capture|diag]", "show or change the operating mode", cmd_mode},
};

void app_mode_cmds_register(void)
{
    cli_register(s_cmds, sizeof s_cmds / sizeof s_cmds[0]);
}
