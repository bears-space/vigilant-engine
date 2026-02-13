#include "status_led.h"
#include "sdkconfig.h"

#include "esp_log.h"
#include "led_strip.h"
#include "led_strip_rmt.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "status_led";

static led_strip_handle_t s_strip = NULL;

static TaskHandle_t s_blink_task = NULL;
static struct {
    uint8_t r, g, b;
    uint32_t on_ms, off_ms;
    bool running;
} s_blink = {0};

static void blink_task(void *arg)
{
    (void)arg;

    while (s_blink.running) {
        // on
        if (s_strip) {
            led_strip_set_pixel(s_strip, 0, s_blink.r, s_blink.g, s_blink.b);
            led_strip_refresh(s_strip);
        }
        vTaskDelay(pdMS_TO_TICKS(s_blink.on_ms));

        // off
        if (s_strip) {
            led_strip_clear(s_strip);
        }
        vTaskDelay(pdMS_TO_TICKS(s_blink.off_ms));
    }

    s_blink_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t status_led_init(const status_led_config_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;
    if (s_strip) return ESP_OK; // already initialized

    led_strip_config_t strip_cfg = {
        .strip_gpio_num = cfg->gpio,
        .max_leds = cfg->max_leds ? cfg->max_leds : 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = {
            .invert_out = cfg->invert_out,
        }
    };

    led_strip_rmt_config_t rmt_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = cfg->resolution_hz ? cfg->resolution_hz : (10 * 1000 * 1000),
        .mem_block_symbols = cfg->mem_block_symbols ? cfg->mem_block_symbols : 64,
        .flags = {
            .with_dma = cfg->with_dma,
        }
    };

    esp_err_t err = led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "led_strip_new_rmt_device failed: %s", esp_err_to_name(err));
        s_strip = NULL;
        return err;
    }

    return status_led_off();
}

esp_err_t status_led_set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    # if !CONFIG_VE_ENABLE_STATUS_LED
    return ESP_OK;
    # else
    if (!s_strip) return ESP_ERR_INVALID_STATE;
    esp_err_t err = led_strip_set_pixel(s_strip, 0, r, g, b);
    if (err != ESP_OK) return err;
    return led_strip_refresh(s_strip);
    # endif
}

esp_err_t status_led_off(void)
{
    if (!s_strip) return ESP_ERR_INVALID_STATE;
    return led_strip_clear(s_strip);
}

esp_err_t status_led_blink_start(uint8_t r, uint8_t g, uint8_t b, uint32_t on_ms, uint32_t off_ms)
{
    if (!s_strip) return ESP_ERR_INVALID_STATE;

    // stop existing blink
    status_led_blink_stop();

    s_blink.r = r;
    s_blink.g = g;
    s_blink.b = b;
    s_blink.on_ms = on_ms ? on_ms : 200;
    s_blink.off_ms = off_ms ? off_ms : 200;
    s_blink.running = true;

    BaseType_t ok = xTaskCreate(blink_task, "status_led_blink", 2048, NULL, 5, &s_blink_task);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t status_led_blink_stop(void)
{
    if (s_blink_task) {
        s_blink.running = false;
        // task deletes itself; give it a moment
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return status_led_off();
}

esp_err_t status_led_deinit(void)
{
    status_led_blink_stop();

    if (!s_strip) return ESP_OK;

    // In IDF, led_strip has del function; depending on version it's:
    // led_strip_del(s_strip);
    // If your IDF doesn't have it, you can omit deinit or gate init once.
#ifdef led_strip_del
    led_strip_del(s_strip);
#endif
    s_strip = NULL;
    return ESP_OK;
}