#include "ws2812.h"
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "ws2812.pio.h"

#define WS2812_PIO pio1
#define WS2812_SM 0
#define WS2812_FREQ 800000

void ws2812_init(int pin)
{
    int offset = pio_add_program(WS2812_PIO, &ws2812_program);
    ws2812_program_init(WS2812_PIO, WS2812_SM, offset, pin, WS2812_FREQ, false);
}

void ws2812_set(uint32_t color)
{
    pio_sm_put_blocking(WS2812_PIO, WS2812_SM, color);
}