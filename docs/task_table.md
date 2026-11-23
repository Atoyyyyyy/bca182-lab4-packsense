# FreeRTOS task table

The planned application tasks are in PRD §6.3. This table lists what the firmware
**actually creates** at the current milestone. The *HWM* column (stack
high-water mark = smallest free stack ever, in words) is filled in only from
real logs in `evidence/`.

Kernel settings: `include/FreeRTOSConfig.h` (1 kHz tick, 7 priorities, heap_4
40 KB, stack overflow check method 2).

## M1 (bring-up)

| Task | Function | Prio | Stack (words) | Stack location | Period / activation | Communication | HWM (measured) |
|---|---|---|---|---|---|---|---|
| `Heartbeat` | Toggle red LED (PF12), print one status line per second | 1 | 256 | static, CCM RAM | `xTaskDelayUntil` 500 ms | console mutex | TODO(measure): `hb_hwm` in `evidence/m1_boot.log` |
| `IDLE` | FreeRTOS idle | 0 | 128 | kernel-provided static | always ready | — | TODO(measure) (M2 `diag mem`) |
| `Tmr Svc` | FreeRTOS software timers | 2 | 256 | kernel-provided static | timer command queue | — | TODO(measure) (M2 `diag mem`) |

## Interrupts

| IRQ | NVIC priority | Calls FreeRTOS API? | Purpose |
|---|---|---|---|
| TIM6_DAC | 4 | no (must not) | HAL tick (`HAL_GetTick`, HAL timeouts) |
| USART1 | 6 | not yet (allowed: ≥ 5) | console TX ring buffer |
| SysTick | 15 | kernel | FreeRTOS tick |
| PendSV / SVC | 15 / — | kernel | context switch / first task start |

Rule: an ISR that calls a `...FromISR()` API must have priority number ≥ 5
(`configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY`). The kernel asserts this at
run time (`configASSERT` is on).
