/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "bsp/board_api.h"
#include "tusb.h"

#include "hardware/clocks.h"
#include "ft245_eeprom.h"
#include "blaster.h"

//--------------------------------------------------------------------+
// MACRO CONSTANT TYPEDEF PROTYPES
//--------------------------------------------------------------------+

//#define TUD_USE_ONBOARD_LED

/* Blink pattern
 * - 250 ms  : device not mounted
 * - 1000 ms : device mounted
 * - 2500 ms : device is suspended
 */
enum
{
    BLINK_NOT_MOUNTED = 250,
    BLINK_MOUNTED = 1000,
    BLINK_SUSPENDED = 2500,

    BLINK_ALWAYS_ON = UINT32_MAX,
    BLINK_ALWAYS_OFF = 0
};

static uint32_t blink_interval_ms = BLINK_NOT_MOUNTED;

//------------- prototypes -------------//
static void led_blinking_task(void);
static void vendor_task(void);

/*------------- MAIN -------------*/
int main(void)
{
    //120 mhz
    set_sys_clock_pll(1440000000, 6, 2);

    board_init();

    // init device stack on configured roothub port
    tud_init(BOARD_TUD_RHPORT);

    if (board_init_after_tusb)
        board_init_after_tusb();

    TU_LOG1("Device running\r\n");

    while (1)
    {
        tud_task(); // tinyusb device task
        vendor_task();
        led_blinking_task();
    }
}

//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+

// Invoked when device is mounted
void tud_mount_cb(void)
{
    blaster_reset();
    blink_interval_ms = BLINK_MOUNTED;
    TU_LOG1("Device mounted\r\n");
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
    blink_interval_ms = BLINK_NOT_MOUNTED;
    TU_LOG1("Device unmounted\r\n");
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us  to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en)
{
    (void)remote_wakeup_en;
    blink_interval_ms = BLINK_SUSPENDED;
    TU_LOG1("Device suspended\r\n");
}

// Invoked when usb bus is resumed
void tud_resume_cb(void)
{
    blaster_reset();
    blink_interval_ms = tud_mounted() ? BLINK_MOUNTED : BLINK_NOT_MOUNTED;
    TU_LOG1("Device resumed\r\n");
}

void tud_reset_cb(void)
{
    blaster_reset();
    TU_LOG1("Device reset\r\n");
}

//--------------------------------------------------------------------+
// Vendor control transfers
//--------------------------------------------------------------------+

static bool handle_vendor_in_request(uint8_t rhport, tusb_control_request_t const *request)
{
    uint8_t bRequest = request->bRequest;
    uint16_t wIndex = request->wIndex;
    uint16_t wLength = request->wLength;

    uint8_t response[2] = {0};

    if (bRequest == 0x90)
    {
        uint16_t address = wIndex * 2;

        if ((address + 1) < FT245_EEPROM_LENGTH)
        {
            response[0] = FT245_EEPROM[address];
            response[1] = FT245_EEPROM[address + 1];
        }

        TU_LOG1("Vendor IN eeprom request wIndex: %d\r\n", wIndex);
    }
    else
    {
        response[0] = 0x36;
        response[1] = 0x83;

        TU_LOG1("Vendor IN unknown request bRequest: %d\r\n", bRequest);
    }

    uint16_t resp_length = (wLength < 2) ? wLength : 2;

    tud_control_xfer(rhport, request, response, resp_length);

    return true;
}

static bool handle_vendor_out_request(uint8_t rhport, tusb_control_request_t const *request)
{
    if (request->wLength > 0)
        tud_control_xfer(rhport, request, NULL, 0);
    else
        tud_control_status(rhport, request);

    TU_LOG1("Vendor OUT request\r\n");

    return true;
}

bool tud_control_request_cb(uint8_t rhport, tusb_control_request_t const *request)
{
    if (request->bmRequestType_bit.type == TUSB_REQ_TYPE_VENDOR)
    {
        if (request->bmRequestType_bit.direction == TUSB_DIR_IN)
            // Vendor IN request (Device to Host)
            return handle_vendor_in_request(rhport, request);
        
        // Vendor OUT request (Host to Device)
        return handle_vendor_out_request(rhport, request);
    }

    return false;
}

//--------------------------------------------------------------------+
// Vendor class
//--------------------------------------------------------------------+

static uint32_t prev_tx_ms = 0;
static uint8_t tx_buf[2 + 64*2] = { 0x31, 0x60 };
static int tx_ready = 0;

static void vendor_task(void)
{
    if (!tud_mounted())
    {
        tx_ready = 0;
        return;
    }

    //each 64 bytes received can theoretically produce 64 payload bytes sent
    if (tud_vendor_available() && tx_ready <= 64) 
    {
        uint8_t buf[64];
        int count = tud_vendor_read(buf, sizeof(buf));

        tx_ready += blaster_process(buf, count, tx_buf + 2 + tx_ready);
    }

    uint32_t now = board_millis();

    if ((tx_ready + 2) >= 64 || (now - prev_tx_ms) >= 10)
    {
        int txCount = tx_ready > 62 ? 62 : tx_ready;

        //other option is to disable fifo completely i.e. don't send anything until it is completely empty
        if (tud_vendor_write_available() < (txCount + 2)) 
            return;

        tud_vendor_write(tx_buf, txCount + 2);
        tud_vendor_write_flush();

        prev_tx_ms = now;
        tx_ready -= txCount;

        if (tx_ready > 0)
            memcpy(tx_buf + 2, tx_buf + 2 + txCount, tx_ready);
    }
}

//--------------------------------------------------------------------+
// BLINKING TASK
//--------------------------------------------------------------------+

#ifdef TUD_USE_ONBOARD_LED

static void led_blinking_task(void)
{
    static uint32_t start_ms = 0;
    static bool led_state = false;

    // Blink every interval ms
    if (board_millis() - start_ms < blink_interval_ms)
        return; // not enough time

    start_ms += blink_interval_ms;

    board_led_write(led_state);
    led_state = 1 - led_state; // toggle
}

#else
static inline void led_blinking_task() {}
#endif