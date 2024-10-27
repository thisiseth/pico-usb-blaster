#pragma once

#include <stdint.h>

void blaster_reset(void);

int blaster_process(uint8_t rxBuf[], int rxCount, uint8_t txBuf[]);

