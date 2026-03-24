#include <stdio.h>
#include "status_led.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "led_strip.h"
#include "led_strip_rmt.h" // this is ws2812b specific
#include "sdkconfig.h"

#if defined(CONFIG_VE_INVERT_STATUS_LED)
    #define INVERT_LED 1
#else
    #define INVERT_LED 0
#endif

#if defined(CONFIG_VE_ENABLE_STATUS_LED)
    #define ENABLE_LED 1
#else
    #define ENABLE_LED 0
#endif

static const char *TAG = "status_led";

/*
Choose mode depending on board or preference

BLINK mode:
- SLOW   (2s)    = Info
- MEDIUM (700ms) = Warning
- FAST   (100ms) = Error
RGB mode:
- GREEN = Info 
- BLUE  = Warning
- RED   = Error
*/

typedef enum {
    BLINK_OUTPUT_NONE = 0,
    BLINK_OUTPUT_GPIO,
#if defined(CONFIG_VE_LED_TYPE_WS2812B)
    BLINK_OUTPUT_WS2812B,
#endif
} blink_output_t;

static struct {
    uint32_t on_ms, off_ms;
    uint8_t state;
    uint8_t gpio;
    bool running;
    blink_output_t output;
#if defined(CONFIG_VE_LED_TYPE_WS2812B)
    uint8_t red;
    uint8_t green;
    uint8_t blue;
#endif
} s_blink = {0};

uint8_t led_on = 1;
uint8_t led_off = 0;

static TaskHandle_t s_blink_task = NULL;

#if defined(CONFIG_VE_LED_TYPE_WS2812B)
static led_strip_handle_t s_strip = NULL;
static esp_err_t ws2812b_status_led_off(void) { // Clear the LED strip (turn off all LEDs)
    if (!s_strip) return ESP_ERR_INVALID_STATE;
    return led_strip_clear(s_strip);
}

static esp_err_t ws2812b_status_led_set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
#if !ENABLE_LED
    return ESP_OK;
#else
    if (!s_strip) return ESP_ERR_INVALID_STATE;
    esp_err_t err = led_strip_set_pixel(s_strip, 0, r, g, b);
    if (err != ESP_OK) return err;
    return led_strip_refresh(s_strip);
#endif
}

static esp_err_t status_led_blink_start_ws2812b(uint32_t on_ms, uint32_t off_ms,
                                                uint8_t red, uint8_t green, uint8_t blue);
#endif

esp_err_t configure_led()
{
    // Don't initialize if VE_ENABLE_STATUS_LED is unset
    if (!ENABLE_LED) {
        return ESP_OK;
    }

    // Change definitions according to VE_INVERT_STATUS_LED
    if (INVERT_LED) {
        led_on = 0;
        led_off = 1;
    }

    #if defined(CONFIG_VE_LED_TYPE_WS2812B) // Initialization for WS2812B LED strip using RMT peripheral
        ESP_LOGI(TAG, "Initializing WS2812B status LED");
        if (s_strip) return ESP_OK; // already initialized

        led_strip_config_t strip_cfg = {
            .strip_gpio_num = CONFIG_VE_STATUS_WS2812B_PIN,
            .max_leds = 1,
            .led_model = LED_MODEL_WS2812,
            .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
            .flags = {
                .invert_out = INVERT_LED,
            }
        };

        led_strip_rmt_config_t rmt_cfg = {
            .clk_src = RMT_CLK_SRC_DEFAULT,
            .resolution_hz = (10 * 1000 * 1000),
            .mem_block_symbols = 64,
            .flags = {
                .with_dma = false,
            }
        };

        esp_err_t err = led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "led_strip_new_rmt_device failed: %s", esp_err_to_name(err));
            s_strip = NULL;
            return err;
        }

        esp_err_t off_err = ws2812b_status_led_off();
        if (off_err == ESP_OK) {
            ESP_LOGI(TAG, "Done initializing status LED");
        }
        return off_err;
    #elif defined(CONFIG_VE_LED_TYPE_NONE) // Applies initialization for both RGB and BLINK modes, as they both use GPIO output
        #if defined(CONFIG_VE_STATUS_LED_MODE_RGB)
            gpio_reset_pin(CONFIG_VE_STATUS_LED_GPIO_RED);
            gpio_reset_pin(CONFIG_VE_STATUS_LED_GPIO_GREEN);
            gpio_reset_pin(CONFIG_VE_STATUS_LED_GPIO_BLUE);
        
            gpio_set_direction(CONFIG_VE_STATUS_LED_GPIO_RED, GPIO_MODE_OUTPUT);
            gpio_set_direction(CONFIG_VE_STATUS_LED_GPIO_GREEN, GPIO_MODE_OUTPUT);
            gpio_set_direction(CONFIG_VE_STATUS_LED_GPIO_BLUE, GPIO_MODE_OUTPUT);
        
        #elif defined(CONFIG_VE_STATUS_LED_MODE_BLINK)
            gpio_reset_pin(CONFIG_VE_STATUS_LED_GPIO_BLINK);
            gpio_set_direction(CONFIG_VE_STATUS_LED_GPIO_BLINK, GPIO_MODE_OUTPUT);
        #endif
    #else
        #error "No valid VE_LED_TYPE selected"
        ESP_LOGI(TAG, "NO VALID VE_LED_TYPE SELECTED, NOT INITIALIZING STATUS LED");
    #endif
    ESP_LOGI(TAG, "Done initializing status LED");
    return ESP_OK;
}

static esp_err_t blink_apply_state(void)
{
    switch (s_blink.output) {
        case BLINK_OUTPUT_GPIO:
            gpio_set_level(s_blink.gpio, s_blink.state ? led_on : led_off);
            return ESP_OK;
#if defined(CONFIG_VE_LED_TYPE_WS2812B)
        case BLINK_OUTPUT_WS2812B:
            if (s_blink.state) {
                return ws2812b_status_led_set_rgb(s_blink.red, s_blink.green, s_blink.blue);
            }
            return ws2812b_status_led_off();
#endif
        case BLINK_OUTPUT_NONE:
        default:
            return ESP_OK;
    }
}

static esp_err_t blink_apply_off(void)
{
    s_blink.state = 0;
    return blink_apply_state();
}

static void blink_task(void *arg)
{
    (void)arg;

    while (s_blink.running) {
        uint32_t delay_ms = s_blink.state ? s_blink.on_ms : s_blink.off_ms;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));

        if (!s_blink.running) {
            break;
        }

        s_blink.state = !s_blink.state;
        esp_err_t err = blink_apply_state();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to update blink state: %s", esp_err_to_name(err));
            s_blink.running = false;
            break;
        }
    }

    s_blink_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t status_led_blink_start(uint32_t on_ms, uint32_t off_ms, uint8_t led_gpio)
{
    // Stop any other blinking first
    status_led_blink_stop();

    //Set to on already and start task immediately after
    s_blink.output = BLINK_OUTPUT_GPIO;
    s_blink.gpio = led_gpio;
    s_blink.on_ms = on_ms;
    s_blink.off_ms = off_ms;
    s_blink.state = 1;
    s_blink.running = true;

    esp_err_t err = blink_apply_state();
    if (err != ESP_OK) {
        s_blink.running = false;
        s_blink.output = BLINK_OUTPUT_NONE;
        return err;
    }

    // Start task with low priority
    BaseType_t ok = xTaskCreate(blink_task, "status_led_blink", 4096, NULL, 5, &s_blink_task);
    if (ok != pdPASS) {
        blink_apply_off();
        s_blink.running = false;
        s_blink.output = BLINK_OUTPUT_NONE;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

#if defined(CONFIG_VE_LED_TYPE_WS2812B)
static esp_err_t status_led_blink_start_ws2812b(uint32_t on_ms, uint32_t off_ms,
                                                uint8_t red, uint8_t green, uint8_t blue)
{
    status_led_blink_stop();

    s_blink.output = BLINK_OUTPUT_WS2812B;
    s_blink.on_ms = on_ms;
    s_blink.off_ms = off_ms;
    s_blink.state = 1;
    s_blink.running = true;
    s_blink.red = red;
    s_blink.green = green;
    s_blink.blue = blue;

    esp_err_t err = blink_apply_state();
    if (err != ESP_OK) {
        s_blink.running = false;
        s_blink.output = BLINK_OUTPUT_NONE;
        return err;
    }

    BaseType_t ok = xTaskCreate(blink_task, "status_led_blink", 4096, NULL, 5, &s_blink_task);
    if (ok != pdPASS) {
        blink_apply_off();
        s_blink.running = false;
        s_blink.output = BLINK_OUTPUT_NONE;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}
#endif

esp_err_t status_led_blink_stop(void)
{
    // Terminate task and set the active LED output to off
    s_blink.running = false;

    if (s_blink_task != NULL) {
        vTaskDelete(s_blink_task);
        s_blink_task = NULL;
    }

    esp_err_t err = blink_apply_off();
    s_blink.output = BLINK_OUTPUT_NONE;
    return err;
}

esp_err_t status_led_set_state(status_state_t state) {
    // No states set if VE_ENABLE_STATUS_LED unset
#if(!ENABLE_LED)
    return ESP_OK;
#endif

    // RGB mode
#if defined(CONFIG_VE_STATUS_LED_MODE_RGB)
    switch (state) {
        case STATUS_STATE_INFO:
            gpio_set_level(CONFIG_VE_STATUS_LED_GPIO_RED, led_off);
            gpio_set_level(CONFIG_VE_STATUS_LED_GPIO_BLUE, led_off);
            return status_led_blink_start(1000, 1000, CONFIG_VE_STATUS_LED_GPIO_GREEN);
        case STATUS_STATE_WARNING:
            gpio_set_level(CONFIG_VE_STATUS_LED_GPIO_GREEN, led_off);
            gpio_set_level(CONFIG_VE_STATUS_LED_GPIO_RED, led_off);
            return status_led_blink_start(600, 600, CONFIG_VE_STATUS_LED_GPIO_BLUE);
        case STATUS_STATE_ERROR:
            gpio_set_level(CONFIG_VE_STATUS_LED_GPIO_GREEN, led_off);
            gpio_set_level(CONFIG_VE_STATUS_LED_GPIO_BLUE, led_off);
            return status_led_blink_start(300, 300, CONFIG_VE_STATUS_LED_GPIO_RED);
        default:
            return status_led_blink_stop();
        }
#elif defined(CONFIG_VE_LED_TYPE_WS2812B) // WS2812B RGB mode
    switch (state) {
        case STATUS_STATE_INFO:
            return status_led_blink_start_ws2812b(1000, 1000, 0, 255, 0);
        case STATUS_STATE_WARNING:
            return status_led_blink_start_ws2812b(600, 600, 0, 0, 255);
        case STATUS_STATE_ERROR:
            return status_led_blink_start_ws2812b(300, 300, 255, 0, 0);
        default:
            return status_led_blink_stop();
        }
#elif defined(CONFIG_VE_STATUS_LED_MODE_BLINK)
// Blink mode
    switch (state) {
        case STATUS_STATE_INFO:
            return status_led_blink_start(2000, 2000, CONFIG_VE_STATUS_LED_GPIO_BLINK);
        case STATUS_STATE_WARNING:
            return status_led_blink_start(700, 700, CONFIG_VE_STATUS_LED_GPIO_BLINK);
        case STATUS_STATE_ERROR:
            return status_led_blink_start(100, 100, CONFIG_VE_STATUS_LED_GPIO_BLINK);
        default:
            return status_led_blink_stop();
}

#else
    return status_led_blink_stop();
#endif
}
