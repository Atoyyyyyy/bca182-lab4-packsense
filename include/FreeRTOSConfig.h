/*
 * FreeRTOSConfig.h: kernel configuration for PackSense (FreeRTOS-Kernel V11.1.0,
 * GCC/ARM_CM4F port) on the STM32F407ZGT6. Values follow PRD section 6.1.
 *
 * Interrupt priority rules (STM32F4: 4 priority bits, NVIC_PRIORITYGROUP_4):
 *   0..4   never masked by the kernel; must NOT call any FreeRTOS API
 *          (TIM6 HAL tick runs at 4, see include/stm32f4xx_hal_conf.h)
 *   5..15  may call ...FromISR() APIs; masked inside kernel critical sections
 *   15     SysTick and PendSV (kernel)
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#if defined(__GNUC__) && !defined(__ASSEMBLER__)
#include <stdint.h>
extern uint32_t SystemCoreClock;
void app_assert_failed(const char *file, int line);
void rtstats_timer_init(void);
uint64_t rtstats_counter(void);
#endif

/* ---- Scheduler ---------------------------------------------------------- */
#define configUSE_PREEMPTION                    1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1
#define configUSE_TICKLESS_IDLE                 0
#define configCPU_CLOCK_HZ                      (SystemCoreClock)
#define configTICK_RATE_HZ                      ((TickType_t)1000)
#define configMAX_PRIORITIES                    7
#define configMINIMAL_STACK_SIZE                ((uint16_t)128) /* words */
#define configMAX_TASK_NAME_LEN                 16
#define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_32_BITS
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_TIME_SLICING                  1
#define configSTACK_DEPTH_TYPE                  uint32_t
#define configMESSAGE_BUFFER_LENGTH_TYPE        size_t
#define configENABLE_BACKWARD_COMPATIBILITY     0
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS 0

/* ---- Synchronisation primitives ---------------------------------------- */
#define configUSE_TASK_NOTIFICATIONS            1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES   1
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_COUNTING_SEMAPHORES           1
#define configUSE_QUEUE_SETS                    0
#define configQUEUE_REGISTRY_SIZE               8

/* ---- Memory ------------------------------------------------------------- */
#define configSUPPORT_STATIC_ALLOCATION         1
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configKERNEL_PROVIDED_STATIC_MEMORY     1 /* idle/timer task buffers */
/* Start value from PRD 6.1 (~40 KB). Resize from measured heap minimum
   (`diag mem`, M2+). The heap lives in main SRAM (DMA-capable). */
#define configTOTAL_HEAP_SIZE                   ((size_t)(40U * 1024U))
#define configAPPLICATION_ALLOCATED_HEAP        0
/* newlib (vsnprintf) is called from several tasks: give each task its own
   reent struct. */
#define configUSE_NEWLIB_REENTRANT              1

/* ---- Hooks and checks --------------------------------------------------- */
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     1 /* extends the run-time counter */
#define configCHECK_FOR_STACK_OVERFLOW          2
#define configUSE_MALLOC_FAILED_HOOK            1
#define configUSE_DAEMON_TASK_STARTUP_HOOK      0
#define configCHECK_HANDLER_INSTALLATION        1

#define configASSERT(x)                                                        \
    do {                                                                       \
        if ((x) == 0) {                                                        \
            app_assert_failed(__FILE__, __LINE__);                             \
        }                                                                      \
    } while (0)

/* ---- Run-time statistics (DWT CYCCNT, extended to 64 bits) -------------- */
#define configGENERATE_RUN_TIME_STATS           1
#define configRUN_TIME_COUNTER_TYPE             uint64_t
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS() rtstats_timer_init()
#define portGET_RUN_TIME_COUNTER_VALUE()        rtstats_counter()
#define configUSE_TRACE_FACILITY                1
#define configUSE_STATS_FORMATTING_FUNCTIONS    0

/* ---- Software timers ---------------------------------------------------- */
#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               2 /* PRD 6.3 task table */
#define configTIMER_QUEUE_LENGTH                10
#define configTIMER_TASK_STACK_DEPTH            256

/* ---- Interrupt priorities ----------------------------------------------- */
#ifdef __NVIC_PRIO_BITS
#define configPRIO_BITS                         __NVIC_PRIO_BITS
#else
#define configPRIO_BITS                         4
#endif
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY      15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
#define configKERNEL_INTERRUPT_PRIORITY \
    (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

/* ---- Optional API functions --------------------------------------------- */
#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_xTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_uxTaskGetStackHighWaterMark     1
#define INCLUDE_xTaskGetIdleTaskHandle          1
#define INCLUDE_eTaskGetState                   1
#define INCLUDE_xTimerPendFunctionCall          1
#define INCLUDE_xTaskGetHandle                  1

/* ---- Map the port's handlers onto the CMSIS vector names ---------------- */
/* The HAL/Cube versions of these three handlers must not exist anywhere else
   (src/stm32f4xx_it.c deliberately does not define them). */
#define vPortSVCHandler     SVC_Handler
#define xPortPendSVHandler  PendSV_Handler
#define xPortSysTickHandler SysTick_Handler

#endif /* FREERTOS_CONFIG_H */
