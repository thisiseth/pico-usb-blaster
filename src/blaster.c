#include "blaster.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"

// change these according to your setup

// if onboard LED / single external LED
#define ACTIVE_LED_PIN PICO_DEFAULT_LED_PIN

// for boards with WS2812 rgb
//#define ACTIVE_LED_WS2812_PIN   16
//#define ACTIVE_LED_WS2812_COLOR_OFF 0x00080000
//#define ACTIVE_LED_WS2812_COLOR_ON 0x08000000

// if level shifter is used
//#define LEVEL_SHIFTER_OE_PIN    15

// pins are mapped sequentially starting from TCK_DCLK_PIN
//
// #  I/O  pin name
// 11  O  TCK_DCLK_PIN 
// 12  O  TMS_nCONFIG_PIN
// 13  O  nCE_PIN
// 14  O  nCS_PIN
// 15  O  TDI_ASDI_PIN
// 16  I  TDO_CONF_DONE_PIN
// 17  I  DATAOUT_nSTATUS_PIN

#define TCK_DCLK_PIN 11

// ^^^^^^^^^^^^^

#define nCS_PIN             (TCK_DCLK_PIN + 3)
#define TDI_ASDI_PIN        (TCK_DCLK_PIN + 4)
#define TDO_CONF_DONE_PIN   (TCK_DCLK_PIN + 5)
#define DATAOUT_nSTATUS_PIN (TCK_DCLK_PIN + 6)

#ifdef ACTIVE_LED_WS2812_PIN
    #include "ws2812.h"
    #if !defined(ACTIVE_LED_WS2812_COLOR_OFF) || !defined(ACTIVE_LED_WS2812_COLOR_ON)
        #error ws2812 colors not defined
    #endif
#endif

#define SHIFT_MODE_FLAG(b)  (!!((b) & 0b10000000))
#define READ_FLAG(b)        (!!((b) & 0b01000000))
#define OE_FLAG(b)          (!!((b) & 0b00100000))
#define PAYLOAD(b)             ((b) & 0b00111111)

static bool initialized = false;
static bool output_enabled = false;
static int shift_bytes_left = 0;
static bool shift_read_set;

// rx/tx byte to signal relations in bitbang mode
//
// rx 
// bitmask | name            | JTAG | AS        | PS
// --------+-----------------+------+-----------+-----------
// 0x01    | TCK_DCLK        | TCK  | DCLK      | DCLK
// 0x02    | TMS_nCONFIG     | TMS  | nCONFIG   | nCONFIG
// 0x04    | nCE             | -    | nCE       | -
// 0x08    | nCS             | -    | nCS       | -
// 0x10    | TDI_ASDI        | TDI  | ASDI      | DATA0 
// 0x20    |            Output Enable/LED active
//
// tx
// 0x01    | TDO_CONF_DONE   | TDO  | CONF_DONE | CONF_DONE
// 0x02    | DATAOUT_nSTATUS | -    | DATAOUT   | nSTATUS

static void blaster_init(void)
{
#ifdef ACTIVE_LED_PIN
    gpio_init(ACTIVE_LED_PIN);
    gpio_set_dir(ACTIVE_LED_PIN, true);
#endif
#ifdef ACTIVE_LED_WS2812_PIN
    ws2812_init(ACTIVE_LED_WS2812_PIN);
    ws2812_set(ACTIVE_LED_WS2812_COLOR_OFF);
#endif

    gpio_init_mask(0b1111111U << TCK_DCLK_PIN); //init 7 pins as input starting from TCK
    
#ifdef LEVEL_SHIFTER_OE_PIN
    gpio_init(LEVEL_SHIFTER_OE_PIN);
    gpio_set_dir(LEVEL_SHIFTER_OE_PIN, true);
    gpio_set_dir_masked(0b0011111U << TCK_DCLK_PIN, 0xFFFFFFFF); //always output if shifter is used
#endif

    initialized = true;
}

// :)
static inline void delay_5_cycles(void) 
{
    __asm__ volatile 
    (
        "nop\n\t" // 1
        "nop\n\t" // 2
        "nop\n\t" // 3
        "nop\n\t" // 4
        "nop"     // 5
    );
}

static inline void output_enable(bool enable)
{
    if (output_enabled == enable)
        return;

    output_enabled = enable;

#ifdef ACTIVE_LED_PIN
    gpio_put(ACTIVE_LED_PIN, enable);
#endif
#ifdef ACTIVE_LED_WS2812_PIN
    ws2812_set(enable ? ACTIVE_LED_WS2812_COLOR_ON : ACTIVE_LED_WS2812_COLOR_OFF);
#endif

#ifdef LEVEL_SHIFTER_OE_PIN
    gpio_put(LEVEL_SHIFTER_OE_PIN, enable);

    //~400ns
    delay_5_cycles();
    delay_5_cycles();
    delay_5_cycles();
    delay_5_cycles();
    delay_5_cycles();
    delay_5_cycles();
    delay_5_cycles();
    delay_5_cycles();
#else
    gpio_set_dir_masked(0b0011111U << TCK_DCLK_PIN, enable ? 0xFFFFFFFF : 0); //lower 5 of 7 pins are output/high-z (input), remaining 2 are input only
#endif
}

static inline uint8_t bitbang(uint8_t data)
{
    uint8_t ret = (!!gpio_get(TDO_CONF_DONE_PIN)) | ((!!gpio_get(DATAOUT_nSTATUS_PIN)) << 1);
    delay_5_cycles();
    gpio_put_masked(0b0011111U << TCK_DCLK_PIN, ((uint32_t)data) << TCK_DCLK_PIN);
    return ret;
}

static inline uint8_t shift(uint8_t data)
{
    uint8_t ret = 0;
    bool nCS = gpio_get_out_level(nCS_PIN);

    for (int i = 0; i < 8; ++i)
    {
        gpio_put(TDI_ASDI_PIN, data & 0b1);

        ret >>= 1;
        
        if (gpio_get(nCS ? TDO_CONF_DONE_PIN : DATAOUT_nSTATUS_PIN))
            ret |= 0b10000000;

        gpio_put(TCK_DCLK_PIN, true);
        delay_5_cycles();
        delay_5_cycles();
        gpio_put(TCK_DCLK_PIN, false);

        data >>= 1;
    }

    return ret;
}

void blaster_reset(void)
{
    if (!initialized)
        blaster_init();

    shift_bytes_left = 0;
    output_enable(false);
    gpio_put_masked(0b11111U << TCK_DCLK_PIN, 0);
}

int blaster_process(uint8_t rxBuf[], int rxCount, uint8_t txBuf[])
{
    int txCount = 0;

    for (int i = 0; i < rxCount; ++i)
    {
        uint8_t b = rxBuf[i];

        if (shift_bytes_left > 0) // shift mode active
        {
            uint8_t input = shift(b);

            if (shift_read_set)
            {
                txBuf[txCount] = input;
                ++txCount;
            }

            --shift_bytes_left;
        }
        else if (SHIFT_MODE_FLAG(b)) // shift mode activated
        {
            shift_read_set = READ_FLAG(b);
            shift_bytes_left = PAYLOAD(b);
            gpio_put(TCK_DCLK_PIN, false);
        }
        else // bitbang mode
        {
            output_enable(OE_FLAG(b));
            uint8_t input = bitbang(b);

            if (READ_FLAG(b))
            {
                txBuf[txCount] = input;
                ++txCount;
            }
        }
    }

    return txCount;
}
