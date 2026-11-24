/*
 * lcd.c: ST7789V3 240x240 LCD on FSMC bank 1 region 3, 8-bit 8080 bus.
 *
 * The FSMC maps the panel into memory: a byte written to the bank base goes
 * out with A18 (D/C) low = command, a byte written to base + (1 << 18) goes out
 * with A18 high = parameter / pixel data. Reads work the same way (RD strobe),
 * and the first byte read after a read command is a dummy.
 *
 * Bus timing and the register values of the init sequence follow the board
 * vendor's driver for this panel (RT-Thread-Studio/sdk-bsp-stm32f407-spark,
 * libraries/Board_Drivers/lcd/drv_lcd.c at commit 4c284fc), because the
 * ST7789V3 datasheet is not in docs/hardware/vendor yet.
 */
#include "lcd.h"

#include "FreeRTOS.h"
#include "task.h"

#include "board_pins.h"

#define LCD_CMD  (*(volatile uint8_t *)LCD_FSMC_BANK_BASE)
#define LCD_DATA (*(volatile uint8_t *)(LCD_FSMC_BANK_BASE + (1UL << LCD_FSMC_DC_ADDR_BIT)))

/* MIPI DCS / ST7789 command codes used here. */
#define CMD_SLPOUT    0x11U
#define CMD_INVON     0x21U
#define CMD_DISPON    0x29U
#define CMD_CASET     0x2AU
#define CMD_RASET     0x2BU
#define CMD_RAMWR     0x2CU
#define CMD_TEON      0x35U
#define CMD_MADCTL    0x36U
#define CMD_COLMOD    0x3AU
#define CMD_RDDID     0x04U
#define CMD_RDDPM     0x0AU
#define CMD_RDDMADCTL 0x0BU
#define CMD_RDDCOLMOD 0x0CU

#define INIT_MADCTL 0x00U
#define INIT_COLMOD 0x65U /* 16 bit/pixel, as in the vendor driver */

static SRAM_HandleTypeDef s_sram;
static bool s_ready;
static bool s_backlight;

/* ---- bus access ----------------------------------------------------------- */

static inline void wr_cmd(uint8_t c)
{
    LCD_CMD = c;
}

static inline void wr_data(uint8_t d)
{
    LCD_DATA = d;
}

static void wr_cmd_data(uint8_t c, const uint8_t *d, unsigned n)
{
    wr_cmd(c);
    for (unsigned i = 0; i < n; i++) {
        wr_data(d[i]);
    }
}

/* Read `n` parameter bytes after command `c` (the dummy byte is dropped). */
static void rd_cmd(uint8_t c, uint8_t *out, unsigned n)
{
    wr_cmd(c);
    __DSB();
    (void)LCD_DATA; /* dummy */
    for (unsigned i = 0; i < n; i++) {
        out[i] = LCD_DATA;
    }
}

/* ---- init ----------------------------------------------------------------- */

typedef struct {
    uint8_t cmd;
    uint8_t n;
    uint8_t data[14];
} init_step_t;

/* Vendor init sequence (drv_lcd.c, see the header comment): porch, gate,
   VCOM, power and gamma settings for this panel, then inversion on (IPS
   panel) and the TE output on (V-blank only). */
static const init_step_t s_init[] = {
    {CMD_MADCTL, 1, {INIT_MADCTL}},
    {CMD_COLMOD, 1, {INIT_COLMOD}},
    {0xB2, 5, {0x0C, 0x0C, 0x00, 0x33, 0x33}},                 /* porch */
    {0xB7, 1, {0x35}},                                         /* gate control */
    {0xBB, 1, {0x37}},                                         /* VCOM */
    {0xC0, 1, {0x2C}},                                         /* LCM control */
    {0xC2, 1, {0x01}},                                         /* VDV/VRH enable */
    {0xC3, 1, {0x12}},                                         /* VRH */
    {0xC4, 1, {0x20}},                                         /* VDV */
    {0xC6, 1, {0x0F}},                                         /* frame rate control */
    {0xD0, 2, {0xA4, 0xA1}},                                   /* power control 1 */
    {0xE0, 14, {0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B, 0x3F, 0x54, 0x4C, 0x18, 0x0D, 0x0B, 0x1F, 0x23}},
    {0xE1, 14, {0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C, 0x3F, 0x44, 0x51, 0x2F, 0x1F, 0x1F, 0x20, 0x23}},
    {CMD_INVON, 0, {0}},
    {CMD_TEON, 1, {0x00}},
};

void lcd_early_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();

    HAL_GPIO_WritePin(LCD_BL_PORT, LCD_BL_PIN, LCD_BL_OFF_LEVEL);
    gpio.Pin = LCD_BL_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LCD_BL_PORT, &gpio);

    HAL_GPIO_WritePin(LCD_RST_PORT, LCD_RST_PIN, GPIO_PIN_RESET);
    gpio.Pin = LCD_RST_PIN;
    HAL_GPIO_Init(LCD_RST_PORT, &gpio);

    /* TE: pull-down, so an undriven line counts no edges. */
    gpio.Pin = LCD_TE_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(LCD_TE_PORT, &gpio);
}

static status_t fsmc_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_FSMC_CLK_ENABLE();

    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = LCD_FSMC_GPIO_AF;
    gpio.Pin = LCD_FSMC_PD_PINS;
    HAL_GPIO_Init(GPIOD, &gpio);
    gpio.Pin = LCD_FSMC_PE_PINS;
    HAL_GPIO_Init(GPIOE, &gpio);
    gpio.Pin = LCD_FSMC_PG_PINS;
    HAL_GPIO_Init(GPIOG, &gpio);

    s_sram.Instance = FSMC_NORSRAM_DEVICE;
    s_sram.Extended = FSMC_NORSRAM_EXTENDED_DEVICE;
    s_sram.Init.NSBank = FSMC_NORSRAM_BANK3;
    s_sram.Init.DataAddressMux = FSMC_DATA_ADDRESS_MUX_DISABLE;
    s_sram.Init.MemoryType = FSMC_MEMORY_TYPE_SRAM;
    s_sram.Init.MemoryDataWidth = FSMC_NORSRAM_MEM_BUS_WIDTH_8;
    s_sram.Init.BurstAccessMode = FSMC_BURST_ACCESS_MODE_DISABLE;
    s_sram.Init.WaitSignalPolarity = FSMC_WAIT_SIGNAL_POLARITY_LOW;
    s_sram.Init.WrapMode = FSMC_WRAP_MODE_DISABLE;
    s_sram.Init.WaitSignalActive = FSMC_WAIT_TIMING_BEFORE_WS;
    s_sram.Init.WriteOperation = FSMC_WRITE_OPERATION_ENABLE;
    s_sram.Init.WaitSignal = FSMC_WAIT_SIGNAL_DISABLE;
    s_sram.Init.ExtendedMode = FSMC_EXTENDED_MODE_ENABLE; /* separate read / write timing */
    s_sram.Init.AsynchronousWait = FSMC_ASYNCHRONOUS_WAIT_DISABLE;
    s_sram.Init.WriteBurst = FSMC_WRITE_BURST_DISABLE;
    s_sram.Init.PageSize = FSMC_PAGE_SIZE_NONE;

    /* HCLK 168 MHz = 5.95 ns per cycle. Read: address setup 15 + data
       phase 60 cycles (~450 ns per byte). Write: 9 + 8 cycles (~100 ns per
       byte). Same values as the vendor driver; the reads are slow on purpose
       (register reads only, never pixels). */
    FSMC_NORSRAM_TimingTypeDef rd = {0};
    rd.AddressSetupTime = 15;
    rd.AddressHoldTime = 1; /* not used in mode A, but HAL requires >= 1 */
    rd.DataSetupTime = 60;
    rd.BusTurnAroundDuration = 0;
    rd.CLKDivision = 2;     /* not used for asynchronous SRAM, HAL requires >= 2 */
    rd.DataLatency = 2;     /* idem */
    rd.AccessMode = FSMC_ACCESS_MODE_A;

    FSMC_NORSRAM_TimingTypeDef wr = rd;
    wr.AddressSetupTime = 9;
    wr.DataSetupTime = 8;

    return (HAL_SRAM_Init(&s_sram, &rd, &wr) == HAL_OK) ? ST_OK : ST_BUS_ERR;
}

status_t lcd_init(void)
{
    s_ready = false;
    lcd_early_init();

    /* Hardware reset: hold low, release, give the controller time to load
       its defaults (vendor: 100 ms each). */
    HAL_GPIO_WritePin(LCD_RST_PORT, LCD_RST_PIN, GPIO_PIN_RESET);
    vTaskDelay(pdMS_TO_TICKS(100));
    HAL_GPIO_WritePin(LCD_RST_PORT, LCD_RST_PIN, GPIO_PIN_SET);
    vTaskDelay(pdMS_TO_TICKS(100));

    status_t st = fsmc_init();
    if (st != ST_OK) {
        return st;
    }

    uint8_t id[3];
    rd_cmd(CMD_RDDID, id, 3);
    const bool all0 = (id[0] | id[1] | id[2]) == 0x00U;
    const bool all1 = (id[0] & id[1] & id[2]) == 0xFFU;
    if (all0 || all1) {
        return ST_NOT_PRESENT;
    }

    for (unsigned i = 0; i < sizeof s_init / sizeof s_init[0]; i++) {
        wr_cmd_data(s_init[i].cmd, s_init[i].data, s_init[i].n);
    }
    wr_cmd(CMD_SLPOUT);
    vTaskDelay(pdMS_TO_TICKS(120)); /* sleep-out settling (vendor: 120 ms) */
    wr_cmd(CMD_DISPON);

    s_ready = true;
    lcd_fill(LCD_BLACK);
    lcd_backlight(true);
    return ST_OK;
}

bool lcd_is_initialised(void)
{
    return s_ready;
}

void lcd_get_info(lcd_info_t *info)
{
    info->initialised = s_ready;
    info->backlight = s_backlight;
    info->id[0] = info->id[1] = info->id[2] = 0U;
    info->power_mode = info->madctl = info->colmod = 0U;
    if (!s_ready) {
        return;
    }
    rd_cmd(CMD_RDDID, info->id, 3);
    rd_cmd(CMD_RDDPM, &info->power_mode, 1);
    rd_cmd(CMD_RDDMADCTL, &info->madctl, 1);
    rd_cmd(CMD_RDDCOLMOD, &info->colmod, 1);
}

void lcd_backlight(bool on)
{
    HAL_GPIO_WritePin(LCD_BL_PORT, LCD_BL_PIN, on ? LCD_BL_ON_LEVEL : LCD_BL_OFF_LEVEL);
    s_backlight = on;
}

/* ---- drawing -------------------------------------------------------------- */

static void set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    const uint8_t cols[4] = {(uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(x1 >> 8), (uint8_t)x1};
    const uint8_t rows[4] = {(uint8_t)(y0 >> 8), (uint8_t)y0, (uint8_t)(y1 >> 8), (uint8_t)y1};
    wr_cmd_data(CMD_CASET, cols, 4);
    wr_cmd_data(CMD_RASET, rows, 4);
    wr_cmd(CMD_RAMWR);
}

void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if (!s_ready || x >= LCD_WIDTH || y >= LCD_HEIGHT || w == 0U || h == 0U) {
        return;
    }
    if (w > LCD_WIDTH - x) {
        w = (uint16_t)(LCD_WIDTH - x);
    }
    if (h > LCD_HEIGHT - y) {
        h = (uint16_t)(LCD_HEIGHT - y);
    }
    set_window(x, y, (uint16_t)(x + w - 1U), (uint16_t)(y + h - 1U));
    const uint8_t hi = (uint8_t)(color >> 8);
    const uint8_t lo = (uint8_t)color;
    for (uint32_t n = (uint32_t)w * h; n > 0U; n--) {
        LCD_DATA = hi;
        LCD_DATA = lo;
    }
}

void lcd_fill(uint16_t color)
{
    lcd_fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, color);
}

/* ---- checks --------------------------------------------------------------- */

unsigned lcd_bus_test(lcd_bus_result_t res[LCD_BUS_TESTS])
{
    /* MADCTL 0xFC/0x00/0xA8/0x54 drive D2..D7 both ways (D1, D0 unused);
       COLMOD 0x55/0x66 read back as 0x05/0x06 and drive D0, D1 both ways. */
    static const uint8_t vals[LCD_BUS_TESTS] = {0xFC, 0x00, 0xA8, 0x54, 0x55, 0x66};
    unsigned bad = 0U;
    if (!s_ready) {
        return LCD_BUS_TESTS;
    }
    for (unsigned i = 0; i < LCD_BUS_TESTS; i++) {
        const bool is_madctl = i < 4U;
        lcd_bus_result_t *r = &res[i];
        r->reg = is_madctl ? CMD_MADCTL : CMD_COLMOD;
        r->wrote = vals[i];
        r->expect = (uint8_t)(vals[i] & (is_madctl ? 0xFCU : 0x07U));
        wr_cmd_data(r->reg, &r->wrote, 1);
        rd_cmd(is_madctl ? CMD_RDDMADCTL : CMD_RDDCOLMOD, &r->got, 1);
        if (r->got != r->expect) {
            bad++;
        }
    }
    const uint8_t m = INIT_MADCTL;
    const uint8_t c = INIT_COLMOD;
    wr_cmd_data(CMD_MADCTL, &m, 1);
    wr_cmd_data(CMD_COLMOD, &c, 1);
    return bad;
}

uint32_t lcd_te_period_us(unsigned frames, unsigned *edges)
{
    const uint32_t per_ms = SystemCoreClock / 1000U;
    const uint32_t limit = per_ms * (100U + 50U * frames);
    const uint32_t start = DWT->CYCCNT;
    uint32_t first = 0U;
    uint32_t last = 0U;
    unsigned n = 0U;
    bool prev = HAL_GPIO_ReadPin(LCD_TE_PORT, LCD_TE_PIN) == GPIO_PIN_SET;

    while (n < frames + 1U && (DWT->CYCCNT - start) < limit) {
        const bool cur = HAL_GPIO_ReadPin(LCD_TE_PORT, LCD_TE_PIN) == GPIO_PIN_SET;
        if (cur && !prev) {
            last = DWT->CYCCNT;
            if (n == 0U) {
                first = last;
            }
            n++;
        }
        prev = cur;
    }
    *edges = n;
    if (n < 2U) {
        return 0U;
    }
    return (uint32_t)(((uint64_t)(last - first) * 1000000ULL / SystemCoreClock) / (n - 1U));
}
