#pragma once

#include <stdint.h>

void ws2812_init(int pin);
void ws2812_set(uint32_t color);