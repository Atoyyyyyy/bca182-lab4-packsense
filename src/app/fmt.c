#include "fmt.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>

const char *fmt_f(char *buf, size_t n, float v, unsigned decimals)
{
    static const uint32_t pow10[] = {1U, 10U, 100U, 1000U, 10000U, 100000U, 1000000U};

    if (isnan(v)) {
        (void)snprintf(buf, n, "nan");
        return buf;
    }
    if (isinf(v)) {
        (void)snprintf(buf, n, v > 0.0f ? "inf" : "-inf");
        return buf;
    }
    if (decimals > 6U) {
        decimals = 6U;
    }
    const uint32_t scale = pow10[decimals];
    const int neg = v < 0.0f;
    const float mag = neg ? -v : v;
    if (mag >= 4.0e9f / (float)scale) {
        (void)snprintf(buf, n, neg ? "-big" : "big");
        return buf;
    }
    /* Round in fixed point so 0.0049 with 2 decimals prints 0.00, not -0.00. */
    const uint64_t fixed = (uint64_t)(mag * (float)scale + 0.5f);
    const uint32_t ip = (uint32_t)(fixed / scale);
    const uint32_t fp = (uint32_t)(fixed % scale);
    const char *sign = (neg && fixed != 0U) ? "-" : "";

    if (decimals == 0U) {
        (void)snprintf(buf, n, "%s%lu", sign, (unsigned long)ip);
    } else {
        (void)snprintf(buf, n, "%s%lu.%0*lu", sign, (unsigned long)ip, (int)decimals,
                       (unsigned long)fp);
    }
    return buf;
}
