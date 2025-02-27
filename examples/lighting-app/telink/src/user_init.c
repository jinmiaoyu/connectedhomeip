/*
 * Copyright (c) 2025 Telink Semiconductor (Shanghai) Co., Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "user_init.h"

void debug_gpio_init(void)
{
    gpio_function_en(GPIO_PE4);
    gpio_output_en(GPIO_PE4);
    gpio_set_low_level(GPIO_PE4);
}

void profiling_pulse(gpio_pin_e pin)
{
    gpio_set_high_level(pin);
    gpio_set_low_level(pin);
}

void pulse(void)
{
    profiling_pulse(GPIO_PE4);
}
