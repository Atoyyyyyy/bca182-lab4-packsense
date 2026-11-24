/*
 * lcd_cmds.h: DIAG console commands for the LCD (M2 bring-up, PRD 8.6).
 */
#ifndef LCD_CMDS_H
#define LCD_CMDS_H

/* Register the commands with the CLI. Call before the scheduler starts. */
void lcd_cmds_register(void);

#endif /* LCD_CMDS_H */
