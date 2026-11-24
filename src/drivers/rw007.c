/*
 * rw007.c: RW007 Wi-Fi module control over SPI2 (see rw007.h).
 *
 * One transfer = two chip-select windows:
 *   stage 1: master request (type 1) -> slave response (type 3), CS up;
 *            wait for the INT falling edge;
 *   stage 2: master request (type 2) -> slave response (type 4) with the
 *            slave's data length, then, in the same CS window, the data
 *            phase (max of both lengths, rounded up to 4 bytes); wait for INT.
 * Headers are 16 bytes, little-endian, magic words as in the vendor driver.
 * A data packet is {u32 data_len, u32 data_type, buffer[1520]}; a command
 * is {u32 cmd, u32 len, value}, a response {u32 cmd, u32 len, i32 result,
 * value}. Everything runs in the calling task; the ISR only gives a
 * semaphore.
 */
#include "rw007.h"

#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include "app_config.h"
#include "board_pins.h"
#include "spi2_bus.h"

#define MASTER_MAGIC1 0x67452301UL
#define MASTER_MAGIC2 0xEFCDAB89UL
#define SLAVE_MAGIC1  0x98BADCFEUL
#define SLAVE_MAGIC2  0x10325476UL
#define MASTER_FLAG_MRDY 0x01U
#define SLAVE_FLAG_SRDY  0x01U

#define PH_MASTER_CMD  1U
#define PH_MASTER_DATA 2U
#define PH_SLAVE_CMD   3U
#define PH_SLAVE_DATA  4U

#define TYPE_CMD  4U
#define TYPE_RESP 5U
#define TYPE_CB   6U

#define CMD_INIT     0U
#define CMD_SET_MODE 1U
#define CMD_MAC_GET  2U
#define CMD_GET_SN   4U
#define CMD_GET_VSR  5U
#define CMD_SCAN     6U

#define EVT_SCAN_REPORT 9U
#define EVT_SCAN_DONE   10U
#define MODE_STATION    1U

#define HDR          16U
#define DATA_MAX     1520U
#define PKT_MAX      (8U + DATA_MAX)
#define INT_WAIT_MS  100U
#define SPI_TIMEOUT  50U
#define BOOT_WAIT_MS 3000U

static uint8_t s_tx[PKT_MAX] __attribute__((aligned(4)));
static uint8_t s_rx[PKT_MAX] __attribute__((aligned(4)));
static bool s_tx_pending;
static uint16_t s_seq;
static SemaphoreHandle_t s_int;
static StaticSemaphore_t s_int_buf;
static rw007_stats_t s_stats;
static bool s_ready;

/* Awaited response (one at a time). */
static uint32_t s_want_cmd;
static bool s_got_resp;
static int32_t s_resp_result;
static uint8_t s_resp_val[64];
static uint32_t s_resp_len;

static rw007_scan_t *s_scan; /* non-NULL while a scan collects reports */

static inline void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static inline void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static inline uint16_t get16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static inline uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

void rw007_exti_irq(void)
{
    if ((EXTI->PR & WIFI_INT_PIN) == 0U) {
        return;
    }
    EXTI->PR = WIFI_INT_PIN;
    s_stats.int_edges++;
    if (s_int != NULL) {
        BaseType_t woken = pdFALSE;
        (void)xSemaphoreGiveFromISR(s_int, &woken);
        portYIELD_FROM_ISR(woken);
    }
}

static void wait_int(void)
{
    if (xSemaphoreTake(s_int, pdMS_TO_TICKS(INT_WAIT_MS)) != pdTRUE) {
        s_stats.int_timeouts++;
    }
}

static void build_hdr(uint8_t h[HDR], uint8_t phase, uint16_t seq, uint16_t m2s_len)
{
    memset(h, 0, HDR);
    h[1] = (uint8_t)(MASTER_FLAG_MRDY | (phase << 4)); /* flag: bits 3..0, type: bits 7..4 */
    put16(&h[4], seq);
    put16(&h[6], m2s_len);
    put32(&h[8], MASTER_MAGIC1);
    put32(&h[12], MASTER_MAGIC2);
}

static bool slave_hdr_ok(const uint8_t r[HDR], uint8_t phase)
{
    return get32(&r[8]) == SLAVE_MAGIC1 && get32(&r[12]) == SLAVE_MAGIC2 && (r[1] >> 4) == phase;
}

static inline void cs(bool low)
{
    HAL_GPIO_WritePin(WIFI_CS_PORT, WIFI_CS_PIN, low ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

typedef enum { XF_ERR, XF_OK, XF_MORE } xf_t;

/* One two-stage transfer. On return *rx_len > 0 means s_rx holds a packet. */
static xf_t transfer_once(uint16_t seq, uint32_t *rx_len)
{
    uint8_t h[HDR], r[HDR];
    const uint16_t m2s = s_tx_pending ? (uint16_t)(8U + get32(&s_tx[0])) : 0U;
    SPI_HandleTypeDef *spi = spi2_bus_handle();
    *rx_len = 0U;

    /* Stage 1 */
    build_hdr(h, PH_MASTER_CMD, seq, m2s);
    spi2_take();
    cs(true);
    const bool ok1 = HAL_SPI_TransmitReceive(spi, h, r, HDR, SPI_TIMEOUT) == HAL_OK;
    cs(false);
    spi2_give();
    (void)xSemaphoreTake(s_int, 0); /* drop a stale edge */
    if (!ok1 || !slave_hdr_ok(r, PH_SLAVE_CMD)) {
        s_stats.stage1_err++;
        vTaskDelay(1);
        return XF_ERR;
    }
    wait_int();

    /* Stage 2 */
    build_hdr(h, PH_MASTER_DATA, seq, m2s);
    spi2_take();
    cs(true);
    const bool ok2 = HAL_SPI_TransmitReceive(spi, h, r, HDR, SPI_TIMEOUT) == HAL_OK;
    if (!ok2 || !slave_hdr_ok(r, PH_SLAVE_DATA) || get16(&r[4]) != seq) {
        cs(false);
        spi2_give();
        s_stats.stage2_err++;
        vTaskDelay(1);
        return XF_ERR;
    }
    const bool srdy = (r[1] & SLAVE_FLAG_SRDY) != 0U;
    uint32_t s2m = get16(&r[6]);
    if (s2m > PKT_MAX) {
        s2m = 0U; /* vendor: drop bad length */
    }
    uint32_t n = srdy ? m2s : 0U;
    if (s2m > n) {
        n = s2m;
    }
    n = (n + 3U) & ~3U;
    if (n > PKT_MAX) {
        n = PKT_MAX;
    }
    bool ok3 = true;
    if (n > 0U) {
        ok3 = HAL_SPI_TransmitReceive(spi, s_tx, s_rx, (uint16_t)n, SPI_TIMEOUT) == HAL_OK;
    }
    cs(false);
    spi2_give();
    if (!ok3) {
        s_stats.stage2_err++;
        return XF_ERR;
    }
    if (srdy && s_tx_pending) {
        s_tx_pending = false;
        memset(s_tx, 0, sizeof s_tx);
    }
    if (s2m > 0U) {
        *rx_len = s2m;
    }
    wait_int();
    return ((r[2] >> 4) > 0U) ? XF_MORE : XF_OK; /* slave_tx_buf: more queued */
}

static void handle_rx(void)
{
    s_stats.rx_packets++;
    const uint32_t type = get32(&s_rx[4]);
    const uint8_t *b = &s_rx[8];
    const uint32_t cmd = get32(&b[0]);

    if (type == TYPE_RESP && cmd == s_want_cmd && !s_got_resp) {
        s_resp_len = get32(&b[4]);
        s_resp_result = (int32_t)get32(&b[8]);
        const uint32_t n = s_resp_len < sizeof s_resp_val ? s_resp_len : sizeof s_resp_val;
        memcpy(s_resp_val, &b[12], n);
        s_got_resp = true;
        return;
    }
    if (type == TYPE_CB && s_scan != NULL) {
        if (cmd == EVT_SCAN_REPORT) {
            const uint8_t *v = &b[12]; /* struct rt_wlan_info */
            if (s_scan->count < RW007_SCAN_MAX) {
                rw007_ap_t *a = &s_scan->ap[s_scan->count];
                a->security = get32(&v[0]);
                a->channel = (int16_t)get16(&v[12]);
                a->rssi = (int16_t)get16(&v[14]);
                a->ssid_len = v[16] > 32U ? 32U : v[16];
                memcpy(a->bssid, &v[50], 6);
            }
            s_scan->count++;
            return;
        }
        if (cmd == EVT_SCAN_DONE) {
            s_scan->done = true;
            return;
        }
    }
    s_stats.unsolicited++;
}

/* Run transfers until `until()` is true or timeout. */
static status_t pump(uint32_t timeout_ms, bool (*until)(void))
{
    const TickType_t t0 = xTaskGetTickCount();
    while (!until()) {
        if ((xTaskGetTickCount() - t0) > pdMS_TO_TICKS(timeout_ms)) {
            return ST_TIMEOUT;
        }
        s_seq = (uint16_t)((s_seq >= 65533U) ? 1U : s_seq + 1U);
        xf_t x = XF_ERR;
        uint32_t rx_len = 0U;
        for (int tries = 0; tries < 3 && x == XF_ERR; tries++) {
            if (tries) {
                s_stats.retries++;
            }
            s_stats.transfers++;
            x = transfer_once(s_seq, &rx_len);
        }
        if (x == XF_ERR) {
            return ST_BUS_ERR;
        }
        if (rx_len > 0U) {
            handle_rx();
        }
        if (x != XF_MORE && rx_len == 0U && !until()) {
            vTaskDelay(pdMS_TO_TICKS(10)); /* idle poll */
        }
    }
    return ST_OK;
}

static bool resp_arrived(void)
{
    return s_got_resp;
}

static status_t command(uint32_t cmd, const void *val, uint32_t len, uint32_t timeout_ms)
{
    memset(s_tx, 0, sizeof s_tx);
    put32(&s_tx[0], 8U + len);   /* data_len: cmd + len + value */
    put32(&s_tx[4], TYPE_CMD);
    put32(&s_tx[8], cmd);
    put32(&s_tx[12], len);
    if (len) {
        memcpy(&s_tx[16], val, len);
    }
    s_tx_pending = true;
    s_want_cmd = cmd;
    s_got_resp = false;
    status_t st = pump(timeout_ms, resp_arrived);
    s_tx_pending = false;
    if (st != ST_OK) {
        return st;
    }
    return (s_resp_result == 0) ? ST_OK : ST_BUS_ERR;
}

static void copy_str(char *dst, size_t cap)
{
    size_t n = s_resp_len < cap - 1U ? s_resp_len : cap - 1U;
    memcpy(dst, s_resp_val, n);
    dst[n] = '\0';
    for (size_t i = 0; i < n; i++) {
        if (dst[i] == '\0') {
            break;
        }
        if (dst[i] < 0x20 || dst[i] > 0x7E) {
            dst[i] = '?';
        }
    }
}

static void gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_SYSCFG_CLK_ENABLE();

    HAL_GPIO_WritePin(WIFI_RST_PORT, WIFI_RST_PIN, GPIO_PIN_RESET);
    gpio.Pin = WIFI_RST_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(WIFI_RST_PORT, &gpio);

    /* INT with pull-down during boot (vendor): stays low if no module. */
    gpio.Pin = WIFI_INT_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(WIFI_INT_PORT, &gpio);
}

static void int_irq_enable(void)
{
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = WIFI_INT_PIN;
    gpio.Mode = GPIO_MODE_IT_FALLING;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(WIFI_INT_PORT, &gpio);
    __HAL_GPIO_EXTI_CLEAR_IT(WIFI_INT_PIN);
    HAL_NVIC_SetPriority(EXTI15_10_IRQn, IRQ_PRIO_WIFI_INT, 0);
    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
}

status_t rw007_init(rw007_info_t *info)
{
    memset(info, 0, sizeof *info);
    s_ready = false;
    if (s_int == NULL) {
        s_int = xSemaphoreCreateBinaryStatic(&s_int_buf);
    }
    if (spi2_bus_init() != ST_OK) {
        return ST_BUS_ERR;
    }
    HAL_NVIC_DisableIRQ(EXTI15_10_IRQn);
    gpio_init();
    memset(s_tx, 0, sizeof s_tx);
    s_tx_pending = false;

    /* Reset pulse and boot wait, as in the vendor port (rw007_stm32_port.c). */
    vTaskDelay(pdMS_TO_TICKS(100));
    HAL_GPIO_WritePin(WIFI_RST_PORT, WIFI_RST_PIN, GPIO_PIN_SET);
    const TickType_t t0 = xTaskGetTickCount();
    while (HAL_GPIO_ReadPin(WIFI_INT_PORT, WIFI_INT_PIN) == GPIO_PIN_RESET) {
        if ((xTaskGetTickCount() - t0) > pdMS_TO_TICKS(BOOT_WAIT_MS)) {
            return ST_NOT_PRESENT;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    info->boot_ms = (uint32_t)(xTaskGetTickCount() - t0);
    vTaskDelay(pdMS_TO_TICKS(200));
    int_irq_enable();
    (void)xSemaphoreTake(s_int, 0);

    const TickType_t t1 = xTaskGetTickCount();
    status_t st = command(CMD_INIT, NULL, 0U, 5000U);
    if (st == ST_OK) {
        const uint32_t mode = MODE_STATION;
        st = command(CMD_SET_MODE, &mode, sizeof mode, 5000U);
    }
    if (st == ST_OK && (st = command(CMD_GET_SN, NULL, 0U, 2000U)) == ST_OK) {
        copy_str(info->sn, sizeof info->sn);
    }
    if (st == ST_OK && (st = command(CMD_GET_VSR, NULL, 0U, 2000U)) == ST_OK) {
        copy_str(info->version, sizeof info->version);
    }
    if (st == ST_OK && (st = command(CMD_MAC_GET, NULL, 0U, 2000U)) == ST_OK) {
        memcpy(info->mac, s_resp_val, 6);
    }
    info->init_ms = (uint32_t)(xTaskGetTickCount() - t1);
    if (st != ST_OK) {
        return st;
    }
    info->ready = true;
    s_ready = true;
    return ST_OK;
}

bool rw007_is_ready(void)
{
    return s_ready;
}

void rw007_off(void)
{
    s_ready = false;
    HAL_NVIC_DisableIRQ(EXTI15_10_IRQn);
    gpio_init(); /* RST driven low: module held in reset */
}

static bool scan_done(void)
{
    return s_scan != NULL && s_scan->done;
}

status_t rw007_scan(uint32_t timeout_ms, rw007_scan_t *out)
{
    memset(out, 0, sizeof *out);
    if (!s_ready) {
        return ST_NOT_PRESENT;
    }
    const TickType_t t0 = xTaskGetTickCount();
    s_scan = out;
    status_t st = command(CMD_SCAN, NULL, 0U, 5000U);
    if (st == ST_OK) {
        const uint32_t used = (uint32_t)(xTaskGetTickCount() - t0);
        st = pump(timeout_ms > used ? timeout_ms - used : 1U, scan_done);
    }
    s_scan = NULL;
    out->ms = (uint32_t)(xTaskGetTickCount() - t0);
    return st;
}

void rw007_get_stats(rw007_stats_t *s)
{
    *s = s_stats;
}
