/*
 * io_cmds.c: console commands for the indicator LED (D4, red + blue), the
 * buzzer and the arrow keys.
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
#include "drivers/led.h"

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

static const cli_cmd_t s_cmds[] = {
    {"led", "[auto | red|blue on|off]", "indicator LED state / drive it by hand", cmd_led},
    {"buzzer", "chirp|alarm|error | beep [ms] | off", "play a buzzer pattern", cmd_buzzer},
    {"keys", "[seconds]", "print debounced arrow-key events (default 20 s)", cmd_keys},
};

void io_cmds_register(void)
{
    cli_register(s_cmds, sizeof s_cmds / sizeof s_cmds[0]);
}
