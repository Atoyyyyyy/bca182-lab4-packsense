/*
 * io_cmds.c: console commands for the indicator LED (D4, red + blue), the
 * buzzer, the arrow keys, the LED matrix and the IR transmitter/receiver.
 *
 * Output lines start with a bracketed tag and t=<ms since boot>, like the
 * sensor commands. `keys` prints one CSV line per debounced event so the log
 * can be checked by a script.
 */
#include "io_cmds.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "app/cli.h"
#include "app/tasks.h"
#include "app_config.h"
#include "drivers/buttons.h"
#include "drivers/buzzer.h"
#include "drivers/console_uart.h"
#include "drivers/ir.h"
#include "drivers/led.h"
#include "drivers/led_matrix.h"
#include "board_pins.h"

#define T_NOW() ((unsigned long)xTaskGetTickCount())

/* ---- led ------------------------------------------------------------------ */

static void led_print_state(void)
{
    console_printf("[led] t=%lu red=%s (%s) blue=%s\r\n", T_NOW(), led_is_on(LED_RED) ? "on" : "off",
                   g_hb_led ? "heartbeat blink" : "manual", led_is_on(LED_BLUE) ? "on" : "off");
}

/* "led", "led auto", "led red|blue on|off" */
static int cmd_led(int argc, char **argv)
{
    if (argc == 0) {
        led_print_state();
        return 0;
    }
    if (argc == 1 && strcmp(argv[0], "auto") == 0) {
        led_set(LED_BLUE, false);
        g_hb_led = true;
        led_print_state();
        return 0;
    }
    if (argc != 2) {
        return 1;
    }
    led_id_t id;
    if (strcmp(argv[0], "red") == 0) {
        id = LED_RED;
    } else if (strcmp(argv[0], "blue") == 0) {
        id = LED_BLUE;
    } else {
        return 1;
    }
    bool on;
    if (strcmp(argv[1], "on") == 0) {
        on = true;
    } else if (strcmp(argv[1], "off") == 0) {
        on = false;
    } else {
        return 1;
    }
    if (id == LED_RED) {
        g_hb_led = false; /* the heartbeat would toggle it back within 0.5 s */
    }
    led_set(id, on);
    led_print_state();
    if (id == LED_RED) {
        console_puts("[led] heartbeat blink paused; 'led auto' resumes it\r\n");
    }
    return 0;
}

/* ---- buzzer --------------------------------------------------------------- */

#define ALARM_DEMO_MS 3000U
#define BEEP_MAX_MS   3000U

/* Wait until the pattern ends (or `max_ms`), checking for a key press. */
static void wait_pattern(uint32_t max_ms)
{
    const TickType_t start = xTaskGetTickCount();
    while (buzzer_is_busy() && (xTaskGetTickCount() - start) < pdMS_TO_TICKS(max_ms)) {
        if (cli_abort_requested()) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    buzzer_stop();
}

/* "buzzer chirp|alarm|error", "buzzer beep [ms]", "buzzer off" */
static int cmd_buzzer(int argc, char **argv)
{
    if (argc < 1 || argc > 2) {
        return 1;
    }
    if (strcmp(argv[0], "off") == 0 && argc == 1) {
        buzzer_stop();
        console_printf("[buzzer] t=%lu off\r\n", T_NOW());
        return 0;
    }
    if (strcmp(argv[0], "beep") == 0) {
        unsigned long ms = 300UL;
        if (argc == 2 && !cli_arg_u32(argv[1], 10UL, BEEP_MAX_MS, &ms)) {
            return 1;
        }
        console_printf("[buzzer] t=%lu beep %lu ms\r\n", T_NOW(), ms);
        buzzer_set(true);
        vTaskDelay(pdMS_TO_TICKS(ms));
        buzzer_set(false);
        console_printf("[buzzer] t=%lu done\r\n", T_NOW());
        return 0;
    }
    if (argc != 1) {
        return 1;
    }
    for (int p = 0; p < (int)BUZ_PATTERN_COUNT; p++) {
        if (strcmp(argv[0], buzzer_pattern_name((buzzer_pattern_t)p)) != 0) {
            continue;
        }
        console_printf("[buzzer] t=%lu play %s%s\r\n", T_NOW(), argv[0],
                       p == (int)BUZ_ALARM ? " (stops after 3 s)" : "");
        buzzer_play((buzzer_pattern_t)p);
        wait_pattern(ALARM_DEMO_MS);
        console_printf("[buzzer] t=%lu done\r\n", T_NOW());
        return 0;
    }
    return 1;
}

/* ---- keys ----------------------------------------------------------------- */

/* "keys [seconds]": print debounced key events until the time is up or a
   character arrives on the console. */
static int cmd_keys(int argc, char **argv)
{
    unsigned long secs = 20UL;
    if (argc > 1) {
        return 1;
    }
    if (argc == 1 && !cli_arg_u32(argv[0], 1UL, 600UL, &secs)) {
        return 1;
    }
    buttons_resync();
    console_printf("# keys listen s=%lu debounce_ms=%u long_ms=%u "
                   "cols=t_ms,key,event,held_ms,long,edges\r\n",
                   secs, (unsigned)BUTTON_DEBOUNCE_MS, (unsigned)BUTTON_LONG_PRESS_MS);

    const TickType_t start = xTaskGetTickCount();
    const TickType_t span = pdMS_TO_TICKS(secs * 1000UL);
    unsigned long n = 0UL;
    bool aborted = false;
    while ((xTaskGetTickCount() - start) < span) {
        button_event_t ev;
        if (buttons_get_event(&ev, pdMS_TO_TICKS(100))) {
            n++;
            console_printf("key,%lu,%s,%s,%lu,%u,%lu\r\n", (unsigned long)ev.t_ms, button_name(ev.key),
                           ev.pressed ? "press" : "release", (unsigned long)ev.held_ms,
                           ev.long_press ? 1U : 0U, (unsigned long)ev.edges);
        }
        if (cli_abort_requested()) {
            aborted = true;
            break;
        }
    }
    console_printf("# keys end: %lu events%s\r\n", n, aborted ? " (stopped by a key on the console)" : "");
    for (int k = 0; k < (int)BTN_COUNT; k++) {
        button_stats_t st;
        buttons_get_stats((button_id_t)k, &st);
        console_printf("[keys] %-5s presses=%lu long=%lu raw_edges=%lu dropped=%lu now=%s\r\n",
                       button_name((button_id_t)k), (unsigned long)st.presses,
                       (unsigned long)st.long_presses, (unsigned long)st.raw_edges,
                       (unsigned long)st.dropped, st.pressed ? "down" : "up");
    }
    return 0;
}

/* ---- LED matrix ----------------------------------------------------------- */

#define MATRIX_MAX_LEVEL 64UL /* keep it dim: 19 LEDs at full white draw a lot */

static bool matrix_ready(void)
{
    if (led_matrix_is_initialised()) {
        return true;
    }
    status_t st = led_matrix_init();
    console_printf("[matrix] t=%lu init %s (%u LEDs, all off)\r\n", T_NOW(), status_str(st),
                   (unsigned)LEDMX_COUNT);
    return st == ST_OK;
}

static void matrix_print_stats(void)
{
    led_matrix_stats_t s;
    led_matrix_get_stats(&s);
    console_printf("[matrix] t=%lu frames=%lu timeouts=%lu dma_errors=%lu last_wire_us=%lu\r\n", T_NOW(),
                   (unsigned long)s.frames, (unsigned long)s.timeouts, (unsigned long)s.dma_errors,
                   (unsigned long)s.last_wire_us);
}

static bool parse_rgb(char **argv, uint8_t rgb[3])
{
    for (int i = 0; i < 3; i++) {
        unsigned long v;
        if (!cli_arg_u32(argv[i], 0UL, MATRIX_MAX_LEVEL, &v)) {
            return false;
        }
        rgb[i] = (uint8_t)v;
    }
    return true;
}

static int matrix_show_report(const char *what)
{
    status_t st = led_matrix_show();
    console_printf("[matrix] t=%lu %s: %s\r\n", T_NOW(), what, status_str(st));
    return 0;
}

/* "matrix": init on first use + counters. */
static int cmd_matrix(int argc, char **argv)
{
    (void)argv;
    if (argc != 0) {
        return 1;
    }
    if (matrix_ready()) {
        matrix_print_stats();
    }
    return 0;
}

/* "matrix fill r g b" (0..64 each), "matrix off" */
static int cmd_matrix_fill(int argc, char **argv)
{
    uint8_t c[3];
    if (argc != 3 || !parse_rgb(argv, c)) {
        return 1;
    }
    if (!matrix_ready()) {
        return 0;
    }
    led_matrix_fill(c[0], c[1], c[2]);
    console_printf("[matrix] t=%lu fill r=%u g=%u b=%u\r\n", T_NOW(), c[0], c[1], c[2]);
    return matrix_show_report("sent");
}

static int cmd_matrix_off(int argc, char **argv)
{
    (void)argv;
    if (argc != 0) {
        return 1;
    }
    if (!matrix_ready()) {
        return 0;
    }
    led_matrix_fill(0, 0, 0);
    return matrix_show_report("all off");
}

/* "matrix set <i> r g b": one LED, the others keep their colour. */
static int cmd_matrix_set(int argc, char **argv)
{
    unsigned long idx;
    uint8_t c[3];
    if (argc != 4 || !cli_arg_u32(argv[0], 0UL, LEDMX_COUNT - 1U, &idx) || !parse_rgb(&argv[1], c)) {
        return 1;
    }
    if (!matrix_ready()) {
        return 0;
    }
    led_matrix_set((unsigned)idx, c[0], c[1], c[2]);
    console_printf("[matrix] t=%lu set %lu r=%u g=%u b=%u\r\n", T_NOW(), idx, c[0], c[1], c[2]);
    return matrix_show_report("sent");
}

/* "matrix chase [ms]": light LED 0, 1, ... 18 one at a time (dim white),
   so a person can see that every LED in the chain works and in which order
   they are wired. */
static int cmd_matrix_chase(int argc, char **argv)
{
    unsigned long ms = 300UL;
    if (argc > 1 || (argc == 1 && !cli_arg_u32(argv[0], 20UL, 5000UL, &ms))) {
        return 1;
    }
    if (!matrix_ready()) {
        return 0;
    }
    unsigned ok = 0U;
    for (unsigned i = 0; i < LEDMX_COUNT; i++) {
        led_matrix_fill(0, 0, 0);
        led_matrix_set(i, 16, 16, 16);
        if (led_matrix_show() == ST_OK) {
            ok++;
        }
        console_printf("[matrix] t=%lu chase led=%u\r\n", T_NOW(), i);
        vTaskDelay(pdMS_TO_TICKS(ms));
        if (cli_abort_requested()) {
            break;
        }
    }
    led_matrix_fill(0, 0, 0);
    (void)led_matrix_show();
    console_printf("[matrix] t=%lu chase done: %u of %u frames sent OK, all off\r\n", T_NOW(), ok,
                   (unsigned)LEDMX_COUNT);
    matrix_print_stats();
    return 0;
}

/* ---- IR ------------------------------------------------------------------- */

static bool ir_ready(void)
{
    if (ir_is_initialised()) {
        return true;
    }
    status_t st = ir_init();
    console_printf("[ir] t=%lu init %s: carrier %lu Hz on PB1 (TIM8_CH3N), receiver PF8 level=%s\r\n", T_NOW(),
                   status_str(st), (unsigned long)ir_carrier_hz(),
                   HAL_GPIO_ReadPin(IR_RX_PORT, IR_RX_PIN) == GPIO_PIN_SET ? "high (idle)" : "LOW");
    return st == ST_OK;
}

static int cmd_ir(int argc, char **argv)
{
    (void)argv;
    if (argc != 0) {
        return 1;
    }
    (void)ir_ready();
    return 0;
}

/* "ir loop [bursts] [dry]": NEC-like bursts (560 us carrier, 1690 us gap)
   from the board's own IR LED, counted by the board's own receiver. "dry"
   runs the same timing with PB1 held low (LED dark): a control, so any
   pulses then would be electrical crosstalk, not light. */
static int cmd_ir_loop(int argc, char **argv)
{
    unsigned long n = 20UL;
    bool dry = false;
    if (argc >= 1 && strcmp(argv[argc - 1], "dry") == 0) {
        dry = true;
        argc--;
    }
    if (argc > 1 || (argc == 1 && !cli_arg_u32(argv[0], 1UL, 64UL, &n))) {
        return 1;
    }
    if (!ir_ready()) {
        return 0;
    }
    ir_loop_result_t r;
    if (dry) {
        ir_tx_connect(false);
    }
    status_t st = ir_loopback((unsigned)n, 560U, 1690U, &r);
    if (dry) {
        ir_tx_connect(true);
    }
    console_printf("[ir] t=%lu loop%s %s: bursts=%u detected=%u extra_edges=%u idle_edges=%u "
                   "delay=%lu us low=%lu us (burst 560 us)\r\n",
                   T_NOW(), dry ? " DRY (LED held off)" : "", status_str(st), r.bursts, r.detected, r.extra_edges, r.idle_edges,
                   (unsigned long)r.mean_delay_us, (unsigned long)r.mean_low_us);
    return 0;
}

/* "ir rx [seconds]": show what the receiver sees (point a remote at it). */
static int cmd_ir_rx(int argc, char **argv)
{
    unsigned long s = 10UL;
    if (argc > 1 || (argc == 1 && !cli_arg_u32(argv[0], 1UL, 60UL, &s))) {
        return 1;
    }
    if (!ir_ready()) {
        return 0;
    }
    console_printf("[ir] t=%lu listening %lu s\r\n", T_NOW(), s);
    ir_listen_result_t r;
    (void)ir_listen(s * 1000UL, &r);
    console_printf("[ir] t=%lu rx: edges=%u frames=%u%s\r\n", T_NOW(), r.edges, r.frames,
                   r.overflow ? " (ring overflow: some edges lost)" : "");
    for (unsigned f = 0; f < r.frames && f < 8U; f++) {
        console_printf("[ir] frame %u: edges=%u first_low=%lu us\r\n", f, r.frame_edges[f],
                       (unsigned long)r.first_low_us[f]);
    }
    return 0;
}

static const cli_cmd_t s_cmds[] = {
    {"led", "[auto | red|blue on|off]", "indicator LED state / drive it by hand", cmd_led},
    {"buzzer", "chirp|alarm|error | beep [ms] | off", "play a buzzer pattern", cmd_buzzer},
    {"keys", "[seconds]", "print debounced arrow-key events (default 20 s)", cmd_keys},
    {"matrix", "", "LED matrix counters (inits it on first use)", cmd_matrix},
    {"matrix fill", "<r> <g> <b>", "all 19 LEDs one colour, 0..64 each", cmd_matrix_fill},
    {"matrix off", "", "all LEDs off", cmd_matrix_off},
    {"matrix set", "<i> <r> <g> <b>", "one LED (0..18)", cmd_matrix_set},
    {"matrix chase", "[ms]", "light LED 0..18 one at a time (default 300 ms each)", cmd_matrix_chase},
    {"ir", "", "IR transmitter/receiver init (carrier frequency, receiver level)", cmd_ir},
    {"ir loop", "[bursts] [dry]", "IR LED -> own receiver self-test (dry = LED held off, control)", cmd_ir_loop},
    {"ir rx", "[seconds]", "show IR remote pulses seen by the receiver (default 10 s)", cmd_ir_rx},
};

void io_cmds_register(void)
{
    cli_register(s_cmds, sizeof s_cmds / sizeof s_cmds[0]);
}
