#include <stdio.h>
#include "status_led.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "led_strip.h"
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
    MODE_RGB,
    MODE_BLINK
} led_mode_t;

static struct {
    uint32_t on_ms, off_ms;
    uint8_t state;
    uint8_t gpio;
    bool running;
} s_blink = {0};

uint8_t led_on = 1;
uint8_t led_off = 0;

static TaskHandle_t s_blink_task = NULL;


void configure_led()
{
    // Don't initialize if VE_ENABLE_STATUS_LED is unset
    if (!ENABLE_LED) {
        return;
    }

    // Change definitions according to VE_INVERT_STATUS_LED
    if (INVERT_LED) {
        led_on = 0;
        led_off = 1;
    }

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
}

static void blink_led(uint8_t led_gpio)
{
    // Set the GPIO level according to the state
    gpio_set_level(led_gpio, s_blink.state ? led_on : led_off);
}

static void blink_task(void *arg)
{
    // LED to blink
    uint8_t led_gpio = (uint8_t)(intptr_t)arg;

    while (s_blink.running) {
        blink_led(led_gpio);
        s_blink.state = !s_blink.state;
        s_blink.state ? vTaskDelay(s_blink.on_ms / portTICK_PERIOD_MS) :  vTaskDelay(s_blink.off_ms / portTICK_PERIOD_MS);
    }
    s_blink_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t status_led_blink_start(uint32_t on_ms, uint32_t off_ms, uint8_t led_gpio)
{
    // Stop any other blinking first
    status_led_blink_stop();

    //Set to on already and start task immediately after
    s_blink.gpio = led_gpio;
    s_blink.on_ms = on_ms;
    s_blink.off_ms = off_ms;
    s_blink.state = 1;
    s_blink.running = true;

    // Start task with low priority
    BaseType_t ok = xTaskCreate(blink_task, "status_led_blink", 4096, (void *)(intptr_t)led_gpio, 5, &s_blink_task);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t status_led_blink_stop(void)
{
    // Terminate task and set GPIO to off
    if (s_blink_task != NULL) {
        vTaskDelete(s_blink_task);
        s_blink_task = NULL;
        gpio_set_level(s_blink.gpio, led_off);
    }
    
    s_blink.running = false;
    return ESP_OK;
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
    
    // Blink mode
#elif defined(CONFIG_VE_STATUS_LED_MODE_BLINK)
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