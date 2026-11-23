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
| `Heartbeat` | Toggle red LED (PF12), print one status line per second | 1 | 256 | static, CCM RAM | `xTaskDelayUntil` 500 ms | console mutex | **126** words free (130 of 256 used at peak); `hb_hwm` in `evidence/m1_boot.log`, constant over 190 s |
| `IDLE` | FreeRTOS idle | 0 | 128 | kernel-provided static | always ready | — | TODO(measure) (M2 `diag mem`) |
| `Tmr Svc` | FreeRTOS software timers | 2 | 256 | kernel-provided static | timer command queue | — | TODO(measure) (M2 `diag mem`) |

### M1 on-board results

From `evidence/m1_boot.log` (firmware git `8d71a0c`, RT-Spark HW1.1), checked with
`python tools/check_heartbeat.py evidence/m1_boot.log` → `RESULT: PASS`.

| Quantity | Measured | Source line / check |
|---|---|---|
| Clock source | HSE 8 MHz crystal → PLL M=8 N=336 P=2 Q=7, SYSCLK 168 MHz | `[boot] clock source`, `[boot] RCC:` |
| FreeRTOS tick vs PC clock | 999.93 ticks/s (−0.007 %), worst residual 14 ms over 190 s | least-squares fit over 191 `[hb]` lines |
| Heartbeat period | every 1000 ticks, n=1..191, no gaps | `[hb] tick=` |
| FreeRTOS heap (heap_4) | 40944 B usable; free 40944 B, minimum ever 40928 B | `[boot] FreeRTOS`, `[hb] heap_free= heap_min=` |
| Resets during capture | 2, both `PIN` (RESET key) | `[boot] reset cause:` |

Static memory, from `evidence/m1_build.txt` (linker `--print-memory-usage`):
FLASH 23104 B (2.20 %), RAM 49712 B (37.93 %, includes the 40 KB FreeRTOS heap),
CCM RAM 2192 B (3.34 %).

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
