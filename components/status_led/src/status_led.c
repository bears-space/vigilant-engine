#include "status_led.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "led_strip_rmt.h"  // this is ws2812b specific
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

static const char* TAG = "status_led";

/*
Choose mode depending on board or preference

BLINK mode:
- SLOW   (2s)    = Info
- MEDIUM (700ms) = Warning
- FAST   (100ms) = Error
RGB mode:
- GREEN = Info
- YELLOW = Warning
- RED   = Error
*/

#define LOG_FEEDBACK_QUEUE_LEN 32
#define LOG_INFO_PULSE_MS 35
#define LOG_WARN_PULSE_MS 80
#define LOG_ERROR_BLINK_MS 300

typedef enum {
    BLINK_OUTPUT_NONE = 0,
    BLINK_OUTPUT_GPIO,
#if defined(CONFIG_VE_STATUS_LED_MODE_RGB)
    BLINK_OUTPUT_RGB,
#endif
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
#if defined(CONFIG_VE_LED_TYPE_WS2812B) || \
    defined(CONFIG_VE_STATUS_LED_MODE_RGB)
    uint8_t red;
    uint8_t green;
    uint8_t blue;
#endif
} s_blink = {0};

static uint8_t led_on = 1;
static uint8_t led_off = 0;

static TaskHandle_t s_blink_task = NULL;

#if ENABLE_LED
static SemaphoreHandle_t s_led_mutex = NULL;

typedef enum {
    STATUS_LED_LOG_EVENT_NONE = 0,
    STATUS_LED_LOG_EVENT_INFO,
    STATUS_LED_LOG_EVENT_WARN,
    STATUS_LED_LOG_EVENT_ERROR,
} status_led_log_event_t;

static TaskHandle_t s_log_feedback_task = NULL;
static QueueHandle_t s_log_feedback_queue = NULL;
static vprintf_like_t s_orig_vprintf = NULL;
static bool s_log_hook_installed = false;
static bool s_log_error_blink_started = false;
static volatile bool s_log_error_latched = false;

static esp_err_t status_led_ensure_mutex(void) {
    if (s_led_mutex) {
        return ESP_OK;
    }

    s_led_mutex = xSemaphoreCreateMutex();
    if (!s_led_mutex) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

static esp_err_t status_led_lock(void) {
    esp_err_t err = status_led_ensure_mutex();
    if (err != ESP_OK) {
        return err;
    }

    xSemaphoreTake(s_led_mutex, portMAX_DELAY);
    return ESP_OK;
}

static void status_led_unlock(void) {
    if (s_led_mutex) {
        xSemaphoreGive(s_led_mutex);
    }
}
#else
static esp_err_t status_led_ensure_mutex(void) { return ESP_OK; }

static esp_err_t status_led_lock(void) { return status_led_ensure_mutex(); }

static void status_led_unlock(void) {}
#endif

#if defined(CONFIG_VE_LED_TYPE_WS2812B)
static led_strip_handle_t s_strip = NULL;
static esp_err_t ws2812b_status_led_off(
    void) {  // Clear the LED strip (turn off all LEDs)
    if (!s_strip) return ESP_ERR_INVALID_STATE;
    esp_err_t err = status_led_lock();
    if (err != ESP_OK) {
        return err;
    }

    err = led_strip_clear(s_strip);
    status_led_unlock();
    return err;
}

static esp_err_t ws2812b_status_led_set_rgb(uint8_t r, uint8_t g, uint8_t b) {
#if !ENABLE_LED
    return ESP_OK;
#else
    if (!s_strip) return ESP_ERR_INVALID_STATE;
    esp_err_t err = status_led_lock();
    if (err != ESP_OK) {
        return err;
    }

    err = led_strip_set_pixel(s_strip, 0, r, g, b);
    if (err == ESP_OK) {
        err = led_strip_refresh(s_strip);
    }

    status_led_unlock();
    return err;
#endif
}

static esp_err_t status_led_blink_start_ws2812b(uint32_t on_ms, uint32_t off_ms,
                                                uint8_t red, uint8_t green,
                                                uint8_t blue);
#endif

#if defined(CONFIG_VE_STATUS_LED_MODE_RGB)
static esp_err_t status_led_blink_start_rgb(uint32_t on_ms, uint32_t off_ms,
                                            uint8_t red, uint8_t green,
                                            uint8_t blue);
#endif

#if ENABLE_LED
static esp_err_t status_led_set_rgb_once(uint8_t red, uint8_t green,
                                         uint8_t blue) {
#if (!ENABLE_LED)
    return ESP_OK;
#elif defined(CONFIG_VE_LED_TYPE_WS2812B)
    return ws2812b_status_led_set_rgb(red, green, blue);
#elif defined(CONFIG_VE_STATUS_LED_MODE_RGB)
    esp_err_t err = status_led_lock();
    if (err != ESP_OK) {
        return err;
    }

    gpio_set_level(CONFIG_VE_STATUS_LED_GPIO_RED, red ? led_on : led_off);
    gpio_set_level(CONFIG_VE_STATUS_LED_GPIO_GREEN, green ? led_on : led_off);
    gpio_set_level(CONFIG_VE_STATUS_LED_GPIO_BLUE, blue ? led_on : led_off);
    status_led_unlock();
    return ESP_OK;
#elif defined(CONFIG_VE_STATUS_LED_MODE_BLINK)
    esp_err_t err = status_led_lock();
    if (err != ESP_OK) {
        return err;
    }

    gpio_set_level(CONFIG_VE_STATUS_LED_GPIO_BLINK,
                   (red || green || blue) ? led_on : led_off);
    status_led_unlock();
    return ESP_OK;
#else
    return ESP_OK;
#endif
}

static esp_err_t status_led_off_once(void) {
#if (!ENABLE_LED)
    return ESP_OK;
#elif defined(CONFIG_VE_LED_TYPE_WS2812B)
    return ws2812b_status_led_off();
#else
    return status_led_set_rgb_once(0, 0, 0);
#endif
}

static const char* skip_ansi_sequence(const char* fmt) {
    if (*fmt != '\033') {
        return fmt;
    }

    ++fmt;
    if (*fmt == '[') {
        ++fmt;
        while (*fmt && (*fmt < '@' || *fmt > '~')) {
            ++fmt;
        }
        if (*fmt) {
            ++fmt;
        }
    }

    return fmt;
}

static status_led_log_event_t status_led_log_event_from_format(
    const char* fmt) {
    if (!fmt) {
        return STATUS_LED_LOG_EVENT_NONE;
    }

    while (*fmt) {
        if (*fmt == '\033') {
            fmt = skip_ansi_sequence(fmt);
            continue;
        }

        if (*fmt == ' ' || *fmt == '\t' || *fmt == '\r' || *fmt == '\n') {
            ++fmt;
            continue;
        }

        char level = *fmt;
        char next = fmt[1];
        if ((next == ' ' || next == '(' || next == '\0')) {
            if (level == 'I') {
                return STATUS_LED_LOG_EVENT_INFO;
            }
            if (level == 'W') {
                return STATUS_LED_LOG_EVENT_WARN;
            }
            if (level == 'E') {
                return STATUS_LED_LOG_EVENT_ERROR;
            }
        }

        return STATUS_LED_LOG_EVENT_NONE;
    }

    return STATUS_LED_LOG_EVENT_NONE;
}

static void status_led_queue_log_event(status_led_log_event_t event) {
    if (event == STATUS_LED_LOG_EVENT_NONE || !s_log_feedback_queue) {
        return;
    }

    if (s_log_error_latched) {
        return;
    }

    if (event == STATUS_LED_LOG_EVENT_ERROR) {
        s_log_error_latched = true;
    }

    (void)xQueueSend(s_log_feedback_queue, &event, 0);
}

static int status_led_log_vprintf(const char* fmt, va_list ap) {
    status_led_queue_log_event(status_led_log_event_from_format(fmt));

    if (s_orig_vprintf) {
        return s_orig_vprintf(fmt, ap);
    }

    return vprintf(fmt, ap);
}

static void status_led_start_log_error_blink(void);

static void status_led_pulse(uint8_t red, uint8_t green, uint8_t blue,
                             uint32_t duration_ms) {
    if (status_led_blink_stop() != ESP_OK) {
        return;
    }

    (void)status_led_set_rgb_once(red, green, blue);
    vTaskDelay(pdMS_TO_TICKS(duration_ms));

    if (s_log_error_latched) {
        status_led_start_log_error_blink();
        return;
    }

    (void)status_led_off_once();
}

static void status_led_start_log_error_blink(void) {
    if (s_log_error_blink_started) {
        return;
    }

    s_log_error_blink_started = true;
    (void)status_led_set_state(STATUS_STATE_ERROR);
}

static void status_led_log_feedback_task(void* arg) {
    (void)arg;

    status_led_log_event_t event;
    while (true) {
        if (xQueueReceive(s_log_feedback_queue, &event, portMAX_DELAY) !=
            pdTRUE) {
            continue;
        }

        if (event == STATUS_LED_LOG_EVENT_ERROR || s_log_error_latched) {
            s_log_error_latched = true;
            status_led_start_log_error_blink();
            continue;
        }

        if (event == STATUS_LED_LOG_EVENT_WARN) {
            status_led_pulse(255, 255, 0, LOG_WARN_PULSE_MS);
        } else if (event == STATUS_LED_LOG_EVENT_INFO) {
            status_led_pulse(0, 255, 0, LOG_INFO_PULSE_MS);
        }
    }
}
#endif

esp_err_t status_led_enable_log_feedback(void) {
#if (!ENABLE_LED)
    return ESP_OK;
#else
    if (s_log_hook_installed) {
        return ESP_OK;
    }

    if (!s_log_feedback_queue) {
        s_log_feedback_queue = xQueueCreate(LOG_FEEDBACK_QUEUE_LEN,
                                            sizeof(status_led_log_event_t));
        if (!s_log_feedback_queue) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (!s_log_feedback_task) {
        BaseType_t ok =
            xTaskCreate(status_led_log_feedback_task, "status_led_log", 4096,
                        NULL, 4, &s_log_feedback_task);
        if (ok != pdPASS) {
            vQueueDelete(s_log_feedback_queue);
            s_log_feedback_queue = NULL;
            return ESP_ERR_NO_MEM;
        }
    }

    s_orig_vprintf = esp_log_set_vprintf(status_led_log_vprintf);
    s_log_hook_installed = true;
    return ESP_OK;
#endif
}

esp_err_t configure_led() {
    // Don't initialize if VE_ENABLE_STATUS_LED is unset
    if (!ENABLE_LED) {
        return ESP_OK;
    }

    // Change definitions according to VE_INVERT_STATUS_LED
    if (INVERT_LED) {
        led_on = 0;
        led_off = 1;
    }
#if ENABLE_LED
    esp_err_t mutex_err = status_led_ensure_mutex();
    if (mutex_err != ESP_OK) {
        return mutex_err;
    }
#endif
#if ENABLE_LED
#if defined(CONFIG_VE_LED_TYPE_WS2812B)  // Initialization for WS2812B LED strip
                                         // using RMT peripheral
    ESP_LOGI(TAG, "Initializing WS2812B status LED");
    if (s_strip)
        return status_led_enable_log_feedback();  // already initialized

    led_strip_config_t strip_cfg = {
        .strip_gpio_num = CONFIG_VE_STATUS_WS2812B_PIN,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = {
            .invert_out = INVERT_LED,
        }};

    led_strip_rmt_config_t rmt_cfg = {.clk_src = RMT_CLK_SRC_DEFAULT,
                                      .resolution_hz = (10 * 1000 * 1000),
                                      .mem_block_symbols = 64,
                                      .flags = {
                                          .with_dma = false,
                                      }};

    esp_err_t err = led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "led_strip_new_rmt_device failed: %s",
                 esp_err_to_name(err));
        s_strip = NULL;
        return err;
    }

    esp_err_t off_err = ws2812b_status_led_off();
    if (off_err == ESP_OK) {
        ESP_LOGI(TAG, "Done initializing status LED");
        return status_led_enable_log_feedback();
    }
    return off_err;
#elif defined( \
    CONFIG_VE_LED_TYPE_GENERIC)  // Applies initialization for both RGB and
                                 // BLINK modes, as they both use GPIO output
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
#else
    ESP_LOGI(TAG, "ENABLE_LED is false, skipping status LED initialization");
#endif
    return status_led_enable_log_feedback();
}

static esp_err_t blink_apply_state(void) {
    switch (s_blink.output) {
        case BLINK_OUTPUT_GPIO: {
            esp_err_t err = status_led_lock();
            if (err != ESP_OK) {
                return err;
            }
            gpio_set_level(s_blink.gpio, s_blink.state ? led_on : led_off);
            status_led_unlock();
            return ESP_OK;
        }
#if defined(CONFIG_VE_STATUS_LED_MODE_RGB)
        case BLINK_OUTPUT_RGB:
            if (s_blink.state) {
                return status_led_set_rgb_once(s_blink.red, s_blink.green,
                                               s_blink.blue);
            }
            return status_led_off_once();
#endif
#if defined(CONFIG_VE_LED_TYPE_WS2812B)
        case BLINK_OUTPUT_WS2812B:
            if (s_blink.state) {
                return ws2812b_status_led_set_rgb(s_blink.red, s_blink.green,
                                                  s_blink.blue);
            }
            return ws2812b_status_led_off();
#endif
        case BLINK_OUTPUT_NONE:
        default:
            return ESP_OK;
    }
}

static esp_err_t blink_apply_off(void) {
    s_blink.state = 0;
    return blink_apply_state();
}

static void blink_task(void* arg) {
    (void)arg;

    while (s_blink.running) {
        uint32_t delay_ms = s_blink.state ? s_blink.on_ms : s_blink.off_ms;
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(delay_ms));

        if (!s_blink.running) {
            break;
        }

        s_blink.state = !s_blink.state;
        esp_err_t err = blink_apply_state();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to update blink state: %s",
                     esp_err_to_name(err));
            s_blink.running = false;
            break;
        }
    }

    s_blink_task = NULL;
    vTaskDelete(NULL);
}

static esp_err_t status_led_wait_for_blink_task_stop(TaskHandle_t task) {
    if (!task || task == xTaskGetCurrentTaskHandle()) {
        return ESP_OK;
    }

    xTaskNotifyGive(task);

    for (int i = 0; i < 100; ++i) {
        if (s_blink_task != task) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    return ESP_ERR_TIMEOUT;
}

esp_err_t status_led_blink_start(uint32_t on_ms, uint32_t off_ms,
                                 uint8_t led_gpio) {
    // Stop any other blinking first
    esp_err_t err = status_led_blink_stop();
    if (err != ESP_OK) {
        return err;
    }

    // Set to on already and start task immediately after
    s_blink.output = BLINK_OUTPUT_GPIO;
    s_blink.gpio = led_gpio;
    s_blink.on_ms = on_ms;
    s_blink.off_ms = off_ms;
    s_blink.state = 1;
    s_blink.running = true;

    err = blink_apply_state();
    if (err != ESP_OK) {
        s_blink.running = false;
        s_blink.output = BLINK_OUTPUT_NONE;
        return err;
    }

    // Start task with low priority
    BaseType_t ok = xTaskCreate(blink_task, "status_led_blink", 4096, NULL, 5,
                                &s_blink_task);
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
                                                uint8_t red, uint8_t green,
                                                uint8_t blue) {
    esp_err_t err = status_led_blink_stop();
    if (err != ESP_OK) {
        return err;
    }

    s_blink.output = BLINK_OUTPUT_WS2812B;
    s_blink.on_ms = on_ms;
    s_blink.off_ms = off_ms;
    s_blink.state = 1;
    s_blink.running = true;
    s_blink.red = red;
    s_blink.green = green;
    s_blink.blue = blue;

    err = blink_apply_state();
    if (err != ESP_OK) {
        s_blink.running = false;
        s_blink.output = BLINK_OUTPUT_NONE;
        return err;
    }

    BaseType_t ok = xTaskCreate(blink_task, "status_led_blink", 4096, NULL, 5,
                                &s_blink_task);
    if (ok != pdPASS) {
        blink_apply_off();
        s_blink.running = false;
        s_blink.output = BLINK_OUTPUT_NONE;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}
#endif

#if defined(CONFIG_VE_STATUS_LED_MODE_RGB)
static esp_err_t status_led_blink_start_rgb(uint32_t on_ms, uint32_t off_ms,
                                            uint8_t red, uint8_t green,
                                            uint8_t blue) {
    esp_err_t err = status_led_blink_stop();
    if (err != ESP_OK) {
        return err;
    }

    s_blink.output = BLINK_OUTPUT_RGB;
    s_blink.on_ms = on_ms;
    s_blink.off_ms = off_ms;
    s_blink.state = 1;
    s_blink.running = true;
    s_blink.red = red;
    s_blink.green = green;
    s_blink.blue = blue;

    err = blink_apply_state();
    if (err != ESP_OK) {
        s_blink.running = false;
        s_blink.output = BLINK_OUTPUT_NONE;
        return err;
    }

    BaseType_t ok = xTaskCreate(blink_task, "status_led_blink", 4096, NULL, 5,
                                &s_blink_task);
    if (ok != pdPASS) {
        blink_apply_off();
        s_blink.running = false;
        s_blink.output = BLINK_OUTPUT_NONE;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}
#endif

esp_err_t status_led_blink_stop(void) {
    // Terminate task and set the active LED output to off
    s_blink.running = false;

    TaskHandle_t blink_task = s_blink_task;
    esp_err_t stop_err = status_led_wait_for_blink_task_stop(blink_task);
    if (stop_err != ESP_OK) {
        return stop_err;
    }

    esp_err_t err = blink_apply_off();
    s_blink.output = BLINK_OUTPUT_NONE;
    return err;
}

esp_err_t status_led_set_state(status_state_t state) {
    // No states set if VE_ENABLE_STATUS_LED unset
#if (!ENABLE_LED)
    return ESP_OK;
#endif

#if ENABLE_LED
    if (s_log_error_latched && state != STATUS_STATE_ERROR) {
        return ESP_OK;
    }
#endif

    // RGB mode
#if defined(CONFIG_VE_STATUS_LED_MODE_RGB)
    switch (state) {
        case STATUS_STATE_INFO:
            return status_led_blink_start_rgb(1000, 1000, 0, 255, 0);
        case STATUS_STATE_WARNING:
            return status_led_blink_start_rgb(600, 600, 255, 255, 0);
        case STATUS_STATE_ERROR:
            return status_led_blink_start_rgb(LOG_ERROR_BLINK_MS,
                                              LOG_ERROR_BLINK_MS, 255, 0, 0);
        default:
            return status_led_blink_stop();
    }
#elif defined(CONFIG_VE_LED_TYPE_WS2812B)  // WS2812B RGB mode
    switch (state) {
        case STATUS_STATE_INFO:
            return status_led_blink_start_ws2812b(1000, 1000, 0, 255, 0);
        case STATUS_STATE_WARNING:
            return status_led_blink_start_ws2812b(600, 600, 255, 255, 0);
        case STATUS_STATE_ERROR:
            return status_led_blink_start_ws2812b(
                LOG_ERROR_BLINK_MS, LOG_ERROR_BLINK_MS, 255, 0, 0);
        default:
            return status_led_blink_stop();
    }
#elif defined(CONFIG_VE_STATUS_LED_MODE_BLINK)
    // Blink mode
    switch (state) {
        case STATUS_STATE_INFO:
            return status_led_blink_start(2000, 2000,
                                          CONFIG_VE_STATUS_LED_GPIO_BLINK);
        case STATUS_STATE_WARNING:
            return status_led_blink_start(700, 700,
                                          CONFIG_VE_STATUS_LED_GPIO_BLINK);
        case STATUS_STATE_ERROR:
            return status_led_blink_start(100, 100,
                                          CONFIG_VE_STATUS_LED_GPIO_BLINK);
        default:
            return status_led_blink_stop();
    }

#else
    return status_led_blink_stop();
#endif
}
