/*
 * main.c: PackSense firmware entry point (M1: build system + FreeRTOS bring-up).
 *
 * Boot order:
 *   1. capture the reset cause, zero CCM RAM, enable fault handlers
 *   2. HAL_Init (HAL tick on TIM6), NVIC grouping 4, 168 MHz clock
 *   3. LEDs + console, boot banner (polled UART: interrupts not needed yet)
 *   4. create RTOS objects and tasks, start the scheduler
 * No HAL_Delay() after step 4 begins: the first FreeRTOS API call masks
 * kernel-level interrupts until the scheduler starts.
 */
#include <stdint.h>
#include <string.h>

#include "stm32f4xx_hal.h"

#include "FreeRTOS.h"
#include "task.h"

#include "app/diag_cmds.h"
#include "app/io_cmds.h"
#include "app/lcd_cmds.h"
#include "app/rtc_cmds.h"
#include "app/storage_cmds.h"
#include "app/wifi_cmds.h"
#include "app/app_mode.h"
#include "app/capture.h"
#include "app/evlog.h"
#include "app/infer_task.h"
#include "app/selftest.h"
#include "app/sensor_task.h"
#include "app/tasks.h"
#include "app/ui.h"
#include "app/wdg.h"
#include "app_config.h"
#include "board_pins.h"
#include "drivers/console_uart.h"
#include "drivers/i2c_bus.h"
#include "drivers/buttons.h"
#include "drivers/buzzer.h"
#include "drivers/lcd.h"
#include "drivers/sdcard.h"
#include "drivers/led.h"
#include "fault.h"
#include "system_clock.h"

extern uint32_t _sccmram;
extern uint32_t _eccmram;

static void ccmram_zero(void)
{
    __HAL_RCC_CCMDATARAMEN_CLK_ENABLE(); /* on after reset; make it explicit */
    for (uint32_t *p = &_sccmram; p < &_eccmram; p++) {
        *p = 0U;
    }
}

static void enable_fault_handlers(void)
{
    /* Report MemManage/BusFault/UsageFault by name instead of HardFault, and
       trap integer divide-by-zero. */
    SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk | SCB_SHCSR_BUSFAULTENA_Msk | SCB_SHCSR_USGFAULTENA_Msk;
    SCB->CCR |= SCB_CCR_DIV_0_TRP_Msk;
}

static void print_reset_cause(uint32_t csr)
{
    console_printf("[boot] reset cause:%s%s%s%s%s%s%s (RCC_CSR=0x%08lx)\r\n",
                   (csr & RCC_CSR_LPWRRSTF) ? " LOWPOWER" : "",
                   (csr & RCC_CSR_WWDGRSTF) ? " WWDG" : "",
                   (csr & RCC_CSR_IWDGRSTF) ? " IWDG" : "",
                   (csr & RCC_CSR_SFTRSTF) ? " SOFTWARE" : "",
                   (csr & RCC_CSR_PORRSTF) ? " POWER-ON" : "",
                   (csr & RCC_CSR_PINRSTF) ? " PIN" : "",
                   (csr & RCC_CSR_BORRSTF) ? " BROWN-OUT" : "",
                   (unsigned long)csr);
}

static void print_banner(clk_source_t clk, uint32_t reset_csr)
{
    const uint32_t pllcfgr = RCC->PLLCFGR;
    const uint32_t pllm = (pllcfgr & RCC_PLLCFGR_PLLM) >> RCC_PLLCFGR_PLLM_Pos;
    const uint32_t plln = (pllcfgr & RCC_PLLCFGR_PLLN) >> RCC_PLLCFGR_PLLN_Pos;
    const uint32_t pllp = (((pllcfgr & RCC_PLLCFGR_PLLP) >> RCC_PLLCFGR_PLLP_Pos) + 1U) * 2U;
    const uint32_t pllq = (pllcfgr & RCC_PLLCFGR_PLLQ) >> RCC_PLLCFGR_PLLQ_Pos;
    const int pll_hse = (pllcfgr & RCC_PLLCFGR_PLLSRC) != 0U;
    const uint32_t sws = RCC->CFGR & RCC_CFGR_SWS;
    const uint32_t ccm_used = (uint32_t)((uintptr_t)&_eccmram - (uintptr_t)&_sccmram);

    console_puts("\r\n\r\n");
    console_printf("=== %s %s (git %s) built %s %s ===\r\n", PACKSENSE_FW_NAME,
                   PACKSENSE_FW_VERSION, PACKSENSE_GIT_REV, __DATE__, __TIME__);
    console_printf("[boot] board: RT-Spark HW1.1 | MCU dev_id=0x%03lx rev_id=0x%04lx flash=%u KB\r\n",
                   (unsigned long)(DBGMCU->IDCODE & 0xFFFU),
                   (unsigned long)(DBGMCU->IDCODE >> 16),
                   (unsigned)(*(const volatile uint16_t *)FLASHSIZE_BASE));
    print_reset_cause(reset_csr);
    console_printf("[boot] clock source: %s\r\n", system_clock_source_name(clk));
    console_printf("[boot] RCC: SWS=%s PLLSRC=%s M=%lu N=%lu P=%lu Q=%lu flash_latency=%lu WS\r\n",
                   (sws == RCC_CFGR_SWS_PLL) ? "PLL" : (sws == RCC_CFGR_SWS_HSE) ? "HSE" : "HSI",
                   pll_hse ? "HSE" : "HSI", (unsigned long)pllm, (unsigned long)plln,
                   (unsigned long)pllp, (unsigned long)pllq,
                   (unsigned long)(FLASH->ACR & FLASH_ACR_LATENCY));
    /* These are computed by HAL from the RCC registers and HSE_VALUE; the real
       rate is checked against the PC clock by tools/check_heartbeat.py. */
    console_printf("[boot] clocks (computed, HSE_VALUE=%lu): SYSCLK=%lu HCLK=%lu PCLK1=%lu PCLK2=%lu Hz\r\n",
                   (unsigned long)HSE_VALUE, (unsigned long)HAL_RCC_GetSysClockFreq(),
                   (unsigned long)HAL_RCC_GetHCLKFreq(), (unsigned long)HAL_RCC_GetPCLK1Freq(),
                   (unsigned long)HAL_RCC_GetPCLK2Freq());
    console_printf("[boot] HAL init OK: HAL v%lu.%lu.%lu, HAL tick on TIM6 (prio %lu), NVIC grouping 4\r\n",
                   (unsigned long)(HAL_GetHalVersion() >> 24),
                   (unsigned long)((HAL_GetHalVersion() >> 16) & 0xFFU),
                   (unsigned long)((HAL_GetHalVersion() >> 8) & 0xFFU),
                   (unsigned long)HAL_GetTickPrio());
    console_printf("[boot] FPU: CPACR CP10/CP11=%s\r\n",
                   ((SCB->CPACR & (0xFUL << 20)) == (0xFUL << 20)) ? "full access" : "OFF");
    console_printf("[boot] FreeRTOS %s: tick %lu Hz, heap %u B (heap_4, %u B usable), max syscall prio %d\r\n",
                   tskKERNEL_VERSION_NUMBER, (unsigned long)configTICK_RATE_HZ,
                   (unsigned)configTOTAL_HEAP_SIZE, (unsigned)xPortGetFreeHeapSize(),
                   configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY);
    console_printf("[boot] CCM RAM: %lu of 65536 B used (.ccmram)\r\n", (unsigned long)ccm_used);
}

/*
 * heap_4 sets up its free list, and the counters behind xPortGetFreeHeapSize()
 * and xPortGetMinimumEverFreeHeapSize(), only on the first pvPortMalloc().
 * Everything in M1 is statically allocated, so without this both read 0 (seen
 * on the first on-board run). pvPortMalloc(0) would return NULL and trip the
 * malloc-failed hook, so allocate and free one byte. The minimum-ever value
 * then includes this one short-lived minimum block.
 */
static void heap_init_for_stats(void)
{
    void *p = pvPortMalloc(1U);
    vPortFree(p);
}

int main(void)
{
    const uint32_t reset_csr = RCC->CSR;

    ccmram_zero();
    enable_fault_handlers();

    if (HAL_Init() != HAL_OK) {
        app_panic("HAL_Init failed", NULL);
    }
    HAL_NVIC_SetPriorityGrouping(NVIC_PRIORITYGROUP_4); /* all bits pre-emption */

    const clk_source_t clk = system_clock_config();
    __HAL_RCC_CLEAR_RESET_FLAGS();

    led_init();
    buzzer_init();
    lcd_early_init(); /* backlight off, panel held in reset until `lcd` */
    sd_early_init();  /* card-detect input */
    console_init();
    heap_init_for_stats();
    print_banner(clk, reset_csr);

    console_rtos_init();
    i2c_bus_init();
    diag_cmds_register();
    buttons_init();
    io_cmds_register();
    lcd_cmds_register();
    rtc_cmds_register();
    storage_cmds_register();
    wifi_cmds_register();
    selftest_cmds_register();
    /* Holding UP while the board resets starts CAPTURE mode (PRD 5.1), so
       trials can be recorded on a power bank without a console. */
    const bool capture_key = HAL_GPIO_ReadPin(KEYS_PORT, KEY_UP_PIN) == GPIO_PIN_RESET;
    if (capture_key) {
        console_puts("[boot] UP held: starting in CAPTURE mode\r\n");
    }
    wdg_init(reset_csr);
    app_mode_cmds_register();
    sensor_tasks_create();
    capture_init();
    infer_task_create();
    evlog_task_create();
    ui_task_create(reset_csr, capture_key);
    tasks_create();

    console_puts("[boot] starting scheduler\r\n");
    vTaskStartScheduler();

    /* Only reached if the idle/timer task could not be created. */
    app_panic("vTaskStartScheduler returned", NULL);
}
