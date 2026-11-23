/*
 * stm32_assert.h: required by the STM32F4 LL drivers when USE_FULL_ASSERT is
 * defined. assert_param() and assert_failed() are already provided by
 * stm32f4xx_hal_conf.h (implementation in src/fault.c), so this only makes sure
 * that header is included.
 */
#ifndef STM32_ASSERT_H
#define STM32_ASSERT_H

#include "stm32f4xx_hal.h"

#endif /* STM32_ASSERT_H */
