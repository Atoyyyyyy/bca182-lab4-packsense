/*
 * sensor_task.h: SensorTask (PRD 6.3) and EnvTask.
 *
 * SensorTask, priority 5, vTaskDelayUntil 10 ms: IMU every tick (100 Hz),
 * AP3216C every 5th tick (20 Hz; the chip itself only updates about every
 * 225 ms, so values are held in between). Each sample is validated (PRD 6.4),
 * time-stamped and sent to one queue for the active consumer (capture now,
 * InferTask in M5).
 *
 * EnvTask, priority 2, 1 Hz: AHT on the software I2C bus (context only; its
 * latest value is copied into every sample).
 *
 * Both tasks sleep until sensors_start() and stop sampling at sensors_stop().
 */
#ifndef SENSOR_TASK_H
#define SENSOR_TASK_H

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "queue.h"

/* sample_t.flags */
#define SF_IMU_ERR  0x01U /* IMU read failed: motion values invalid */
#define SF_IMU_SAT  0x02U /* an IMU axis at full scale */
#define SF_ALS_ERR  0x04U /* AP3216C read failed: als/ps held from before */
#define SF_ALS_NEW  0x08U /* AP3216C was read on this tick */
#define SF_ENV_ERR  0x10U /* last AHT read failed or out of range */
#define SF_GAP      0x20U /* > 30 ms (3 periods) since the previous sample */

typedef struct {
    uint32_t t_ms;                 /* tick count (1 kHz) when the IMU read finished */
    float ax, ay, az;              /* m/s^2 */
    float gx, gy, gz;              /* deg/s */
    float als;                     /* lux */
    uint16_t ps;                   /* 10-bit raw, higher = closer */
    uint8_t ps_valid;              /* 0 when IR overflow or ALS/PS read error */
    uint8_t flags;                 /* SF_* */
    float temp;                    /* deg C, latest 1 Hz value */
    float rh;                      /* %RH */
} sample_t;

typedef struct {
    bool running;
    uint32_t samples;
    uint32_t imu_errors;
    uint32_t als_errors;
    uint32_t env_errors;
    uint32_t gaps;                 /* samples with SF_GAP */
    uint32_t queue_full;           /* samples dropped: consumer too slow */
    uint32_t max_period_ms;        /* largest IMU-to-IMU interval seen */
    uint32_t env_reads;
} sensor_stats_t;

/* Create both tasks (static, CCM). Call before the scheduler starts. */
void sensor_tasks_create(void);

/* Init the sensors if needed (from the calling task) and start sampling.
   Returns false if the IMU is missing (the queue then stays empty). */
bool sensors_start(void);
void sensors_stop(void);

/* The sample queue (depth SENSOR_QUEUE_LEN). One consumer at a time. */
QueueHandle_t sensors_queue(void);

void sensors_get_stats(sensor_stats_t *out);
void sensors_reset_stats(void);

#endif /* SENSOR_TASK_H */
