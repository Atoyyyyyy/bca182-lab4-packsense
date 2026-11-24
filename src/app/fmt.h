/*
 * fmt.h: fixed-point text for floats.
 *
 * newlib-nano's printf has no %f unless _printf_float is linked, and that path
 * calls newlib's malloc, which is not protected for use from several FreeRTOS
 * tasks. So floats are turned into text here, into a caller-owned buffer.
 */
#ifndef FMT_H
#define FMT_H

#include <stddef.h>

#define FMT_F_BUF 20U

/* Write v with `decimals` digits after the point (0..6), rounded half away
   from zero, e.g. fmt_f(b, sizeof b, -0.0049f, 2) -> "-0.00" is avoided and
   gives "0.00". Returns buf. Non-finite values give "nan"/"inf". */
const char *fmt_f(char *buf, size_t n, float v, unsigned decimals);

/* Scientific notation with 3 significant digits, e.g. "3.14e-6" (for tiny
   error values). Returns buf. */
const char *fmt_e(char *buf, size_t n, float v);

#endif /* FMT_H */
