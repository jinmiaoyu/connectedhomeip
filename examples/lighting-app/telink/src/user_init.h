/*
 * Copyright (c) 2025 Telink Semiconductor (Shanghai) Co., Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef USER_INIT_H_
#define USER_INIT_H_

/* Enable C linkage for C++ Compilers: */
#if defined(__cplusplus)
extern "C" {
#endif

#include "gpio.h"

void debug_gpio_init(void);
void profiling_pulse(gpio_pin_e pin);
void pulse(void);

/* Disable C linkage for C++ Compilers: */
#if defined(__cplusplus)
}
#endif

#endif /* VENDOR_AUDIO_DEMO_APP_CODEC_H_ */
