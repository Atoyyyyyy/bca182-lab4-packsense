/*
 * status.h: result codes shared by every driver (PRD 6.2).
 */
#ifndef STATUS_H
#define STATUS_H

typedef enum {
    ST_OK = 0,
    ST_NACK,        /* device did not acknowledge (absent, or busy) */
    ST_TIMEOUT,     /* transfer or conversion did not finish in time */
    ST_RANGE_ERR,   /* value outside the physical range, or saturated */
    ST_NOT_PRESENT, /* ID/WHO_AM_I check failed, or driver not initialised */
    ST_BUS_ERR,     /* bus error / arbitration lost; the bus was recovered */
    ST_CRC_ERR,     /* checksum mismatch */
} status_t;

static inline const char *status_str(status_t s)
{
    switch (s) {
    case ST_OK:          return "OK";
    case ST_NACK:        return "NACK";
    case ST_TIMEOUT:     return "TIMEOUT";
    case ST_RANGE_ERR:   return "RANGE_ERR";
    case ST_NOT_PRESENT: return "NOT_PRESENT";
    case ST_BUS_ERR:     return "BUS_ERR";
    case ST_CRC_ERR:     return "CRC_ERR";
    default:             return "?";
    }
}

#endif /* STATUS_H */
