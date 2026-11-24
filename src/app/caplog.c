/*
 * caplog.c: capture log in the SPI flash (see caplog.h).
 * Only the capture task writes; the CLI reads (dump) while capture is idle.
 */
#include "caplog.h"

#include <math.h>
#include <string.h>

#include "drivers/extflash.h"

_Static_assert(sizeof(caplog_rec_t) == CAPLOG_REC_SIZE, "record must be 32 bytes");

#define RECS_PER_PAGE (EXTFLASH_PAGE / CAPLOG_REC_SIZE)

static caplog_info_t s_info;
static uint32_t s_next;                     /* index of the next record to write */
static uint8_t s_page[EXTFLASH_PAGE];
static uint32_t s_page_fill;                /* records buffered in s_page */
static uint32_t s_page_written;             /* records of s_page already programmed */
static uint8_t s_chk[256];

static inline uint32_t rec_addr(uint32_t index)
{
    return CAPLOG_BASE + index * CAPLOG_REC_SIZE;
}

static uint16_t sum16(const caplog_rec_t *r)
{
    const uint8_t *p = (const uint8_t *)r;
    uint16_t s = 0U;
    for (unsigned i = 0; i < CAPLOG_REC_SIZE - 2U; i++) {
        s = (uint16_t)(s + p[i]);
    }
    return (uint16_t)(s ^ 0xA5A5U);
}

bool caplog_check_ok(const caplog_rec_t *r)
{
    return r->check == sum16(r);
}

static int16_t clamp16(float v)
{
    if (!(v == v)) { /* NaN */
        return 0;
    }
    v = roundf(v);
    if (v > 32767.0f) {
        return 32767;
    }
    if (v < -32768.0f) {
        return -32768;
    }
    return (int16_t)v;
}

static uint16_t clampu16(float v)
{
    if (!(v == v) || v < 0.0f) {
        return 0U;
    }
    v = roundf(v);
    return (v > 65535.0f) ? 65535U : (uint16_t)v;
}

void caplog_pack(const sample_t *s, uint8_t label, uint16_t trial, uint16_t session, caplog_rec_t *r)
{
    r->t_ms = s->t_ms;
    r->a[0] = clamp16(s->ax * CAPLOG_ACC_SCALE);
    r->a[1] = clamp16(s->ay * CAPLOG_ACC_SCALE);
    r->a[2] = clamp16(s->az * CAPLOG_ACC_SCALE);
    r->g[0] = clamp16(s->gx * CAPLOG_GYR_SCALE);
    r->g[1] = clamp16(s->gy * CAPLOG_GYR_SCALE);
    r->g[2] = clamp16(s->gz * CAPLOG_GYR_SCALE);
    r->als = clampu16(s->als / CAPLOG_ALS_LSB);
    r->ps = s->ps;
    r->temp = clamp16(s->temp * 100.0f);
    r->rh = clampu16(s->rh * 100.0f);
    r->label = label;
    r->flags = (uint8_t)((s->flags & 0x3FU) | (s->ps_valid ? CAPLOG_FLAG_PS_VALID : 0U));
    r->trial = trial;
    r->session = session;
    r->check = sum16(r);
}

static bool rec_erased(uint32_t index)
{
    uint32_t t;
    if (extflash_read(rec_addr(index), (uint8_t *)&t, 4) != ST_OK) {
        return true;
    }
    return t == 0xFFFFFFFFUL;
}

status_t caplog_open(void)
{
    memset(&s_info, 0, sizeof s_info);
    s_page_fill = s_page_written = 0U;
    extflash_info_t fi;
    extflash_get_info(&fi);
    if (!fi.initialised) {
        status_t st = extflash_init();
        if (st != ST_OK) {
            return st;
        }
        extflash_get_info(&fi);
    }
    if (fi.size_bytes != 0U && fi.size_bytes < CAPLOG_END + EXTFLASH_BLOCK) {
        return ST_RANGE_ERR;
    }
    /* First erased record: written records form a prefix. */
    uint32_t lo = 0U, hi = CAPLOG_CAPACITY;
    while (lo < hi) {
        const uint32_t mid = lo + (hi - lo) / 2U;
        if (rec_erased(mid)) {
            hi = mid;
        } else {
            lo = mid + 1U;
        }
    }
    s_next = lo;
    s_info.records = lo;
    s_info.capacity = CAPLOG_CAPACITY;
    if (lo > 0U) {
        caplog_rec_t r;
        if (caplog_read(lo - 1U, &r, 1) == ST_OK && caplog_check_ok(&r)) {
            s_info.last_session = r.session;
        } else {
            /* Last record damaged (power cut while programming): find a good one. */
            for (uint32_t i = lo - 1U; i-- > 0U && lo - i < 64U;) {
                if (caplog_read(i, &r, 1) == ST_OK && caplog_check_ok(&r)) {
                    s_info.last_session = r.session;
                    break;
                }
            }
        }
    }
    /* Continue inside a partly written page (its erased tail can still be
       programmed), so the written records stay a gap-free prefix. */
    s_page_fill = s_page_written = s_next % RECS_PER_PAGE;
    s_info.open = true;
    return ST_OK;
}

void caplog_get_info(caplog_info_t *out)
{
    *out = s_info;
}

/* Make sure the sector holding `addr` is erased (called at sector starts). */
static status_t prepare_sector(uint32_t addr)
{
    const uint32_t sec = addr & ~(EXTFLASH_SECTOR - 1U);
    for (uint32_t off = 0U; off < EXTFLASH_SECTOR; off += sizeof s_chk) {
        if (extflash_read(sec + off, s_chk, sizeof s_chk) != ST_OK) {
            return ST_TIMEOUT;
        }
        for (unsigned i = 0; i < sizeof s_chk; i++) {
            if (s_chk[i] != 0xFFU) {
                s_info.erases++;
                return extflash_erase_sector(sec);
            }
        }
    }
    return ST_OK;
}

static status_t program_buffered(void)
{
    if (s_page_fill == s_page_written) {
        return ST_OK;
    }
    const uint32_t first = s_next - s_page_fill; /* index of s_page[0] */
    const uint32_t off = s_page_written * CAPLOG_REC_SIZE;
    const uint32_t len = (s_page_fill - s_page_written) * CAPLOG_REC_SIZE;
    if (s_page_written == 0U && (rec_addr(first) & (EXTFLASH_SECTOR - 1U)) == 0U) {
        status_t st = prepare_sector(rec_addr(first));
        if (st != ST_OK) {
            s_info.write_errors++;
            return st;
        }
    }
    status_t st = extflash_program(rec_addr(first) + off, &s_page[off], len);
    if (st != ST_OK) {
        s_info.write_errors++;
        return st;
    }
    s_page_written = s_page_fill;
    return ST_OK;
}

status_t caplog_append(const caplog_rec_t *r)
{
    if (!s_info.open) {
        return ST_NOT_PRESENT;
    }
    if (s_next >= CAPLOG_CAPACITY) {
        return ST_RANGE_ERR;
    }
    memcpy(&s_page[s_page_fill * CAPLOG_REC_SIZE], r, CAPLOG_REC_SIZE);
    s_page_fill++;
    s_next++;
    s_info.records = s_next;
    s_info.last_session = r->session;
    status_t st = ST_OK;
    if (s_page_fill == RECS_PER_PAGE) {
        st = program_buffered();
        s_page_fill = s_page_written = 0U;
    }
    return st;
}

status_t caplog_flush(void)
{
    if (!s_info.open) {
        return ST_NOT_PRESENT;
    }
    return program_buffered();
}

status_t caplog_read(uint32_t index, caplog_rec_t *r, uint32_t n)
{
    if (index + n > CAPLOG_CAPACITY) {
        return ST_RANGE_ERR;
    }
    return extflash_read(rec_addr(index), (uint8_t *)r, n * CAPLOG_REC_SIZE);
}

status_t caplog_wipe(void)
{
    if (!s_info.open) {
        return ST_NOT_PRESENT;
    }
    const uint32_t used_end = rec_addr(s_info.records);
    for (uint32_t a = CAPLOG_BASE; a < used_end || a == CAPLOG_BASE; a += EXTFLASH_BLOCK) {
        status_t st = extflash_erase_block(a);
        if (st != ST_OK) {
            return st;
        }
        s_info.erases++;
    }
    s_next = 0U;
    s_page_fill = s_page_written = 0U;
    s_info.records = 0U;
    s_info.last_session = 0U;
    return ST_OK;
}
