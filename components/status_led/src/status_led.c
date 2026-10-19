#include "status_led.h"

#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "led_strip_rmt.h"
#include "sdkconfig.h"

#if defined( \
    CONFIG_VE_ENABLE_STATUS_LED)  // this is needed because if
                                  // VE_ENABLE_STATUS_LED is not set, the
                                  // Kconfig.projbuild file will not define
                                  // CONFIG_VE_ENABLE_STATUS_LED
#define ENABLE_LED 1
#else
#define ENABLE_LED 0
#endif

#if defined(CONFIG_VE_INVERT_WS2812B_SIGNAL)
#define INVERT_WS2812B_SIGNAL 1
#else
#define INVERT_WS2812B_SIGNAL 0
#endif

#if ENABLE_LED
static const char* TAG = "status_led";

#define LOG_FEEDBACK_QUEUE_LEN 32
#define LOG_FEEDBACK_EVENT_PENDING (1UL << 0)
#define LOG_FEEDBACK_ERROR_PENDING (1UL << 1)
#define LOG_ERROR_RETRY_MS 250
#define LOG_PULSE_DURATION_MIN_MS 1U
#define LOG_PULSE_DURATION_MAX_MS 60000U

typedef enum {
    BLINK_OUTPUT_NONE = 0,
    BLINK_OUTPUT_GPIO,
    BLINK_OUTPUT_WS2812B,
} blink_output_t;

static struct {
    uint32_t on_ms, off_ms;
    uint8_t state;
    uint8_t gpio;
    blink_output_t output;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} s_blink = {0};

static TaskHandle_t s_blink_task = NULL;
static SemaphoreHandle_t s_led_mutex = NULL;
// Callers hold this across the entire stop/configure/start transition. The
// worker never takes it, so it can acknowledge a stop while the caller waits.
static SemaphoreHandle_t s_blink_mutex = NULL;
static SemaphoreHandle_t s_blink_stopped = NULL;
static portMUX_TYPE s_mutex_init_lock = portMUX_INITIALIZER_UNLOCKED;

static TaskHandle_t s_log_feedback_task = NULL;
static QueueHandle_t s_log_feedback_queue = NULL;
static vprintf_like_t s_orig_vprintf = NULL;
static bool s_log_hook_installed = false;
static bool s_log_error_blink_started = false;
static atomic_bool s_log_error_latched =
    false;  // flag for whether an error has been logged and the LED should stay
            // in error state

static esp_err_t status_led_ensure_mutex(void) {
    portENTER_CRITICAL(&s_mutex_init_lock);
    bool initialized = s_led_mutex != NULL;
    portEXIT_CRITICAL(&s_mutex_init_lock);
    if (initialized) {
        return ESP_OK;
    }

    SemaphoreHandle_t led = xSemaphoreCreateMutex();
    SemaphoreHandle_t blink = xSemaphoreCreateMutex();
    SemaphoreHandle_t stopped = xSemaphoreCreateBinary();
    if (!led || !blink || !stopped) {
        if (led) vSemaphoreDelete(led);
        if (blink) vSemaphoreDelete(blink);
        if (stopped) vSemaphoreDelete(stopped);
        return ESP_ERR_NO_MEM;
    }

    portENTER_CRITICAL(&s_mutex_init_lock);
    if (!s_led_mutex) {
        s_led_mutex = led;
        s_blink_mutex = blink;
        s_blink_stopped = stopped;
        led = blink = stopped = NULL;
    }
    portEXIT_CRITICAL(&s_mutex_init_lock);
    if (led) vSemaphoreDelete(led);
    if (blink) vSemaphoreDelete(blink);
    if (stopped) vSemaphoreDelete(stopped);
    return ESP_OK;
}

static esp_err_t status_led_blink_lock(void) {
    esp_err_t err = status_led_ensure_mutex();
    if (err == ESP_OK) {
        xSemaphoreTake(s_blink_mutex, portMAX_DELAY);
    }
    return err;
}

static esp_err_t status_led_blink_stop_locked(void);

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
}

static esp_err_t status_led_set_rgb_once(uint8_t red, uint8_t green,
                                         uint8_t blue) {
    return ws2812b_status_led_set_rgb(red, green, blue);
}

static esp_err_t status_led_off_once(void) { return ws2812b_status_led_off(); }

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

static esp_log_level_t status_led_log_event_from_format(const char* fmt) {
    if (!fmt) {
        return ESP_LOG_NONE;
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
                return ESP_LOG_INFO;
            }
            if (level == 'W') {
                return ESP_LOG_WARN;
            }
            if (level == 'E') {
                return ESP_LOG_ERROR;
            }
        }

        return ESP_LOG_NONE;
    }

    return ESP_LOG_NONE;
}

static void status_led_queue_log_event(esp_log_level_t event) {
    if (event == ESP_LOG_NONE || !s_log_feedback_queue ||
        !s_log_feedback_task) {
        return;
    }

    if (s_log_error_latched) {
        return;
    }

    if (event == ESP_LOG_ERROR) {
        // Notification bits coalesce repeated errors and cannot be dropped
        // when the info/warning queue is full. The consumer owns the latch.
        xTaskNotify(s_log_feedback_task, LOG_FEEDBACK_ERROR_PENDING, eSetBits);
        return;
    }

    if (xQueueSend(s_log_feedback_queue, &event, 0) == pdTRUE) {
        xTaskNotify(s_log_feedback_task, LOG_FEEDBACK_EVENT_PENDING, eSetBits);
    }
}

static int status_led_log_vprintf(const char* fmt, va_list ap) {
    status_led_queue_log_event(status_led_log_event_from_format(fmt));

    if (s_orig_vprintf) {
        return s_orig_vprintf(fmt, ap);
    }

    return vprintf(fmt, ap);
}

static void status_led_pulse(uint8_t red, uint8_t green, uint8_t blue,
                             uint32_t duration_ms) {
    if (duration_ms < LOG_PULSE_DURATION_MIN_MS) {
        duration_ms = LOG_PULSE_DURATION_MIN_MS;
    } else if (duration_ms > LOG_PULSE_DURATION_MAX_MS) {
        duration_ms = LOG_PULSE_DURATION_MAX_MS;
    }

    esp_err_t err = status_led_blink_lock();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to lock status LED: %s", esp_err_to_name(err));
        return;
    }

    err = status_led_blink_stop_locked();
    if (err == ESP_OK) {
        err = status_led_set_rgb_once(red, green, blue);
    }
    if (err == ESP_OK) {
        // Keep other state changes from starting a blink during this pulse.
        vTaskDelay(pdMS_TO_TICKS(duration_ms));
        err = status_led_off_once();
    }
    xSemaphoreGive(s_blink_mutex);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to pulse status LED: %s", esp_err_to_name(err));
    }
}

static void status_led_start_log_error_blink(void) {
    if (s_log_error_blink_started) {
        return;
    }

    esp_err_t err = status_led_set_state(ESP_LOG_ERROR);
    if (err == ESP_OK) {
        s_log_error_blink_started = true;
    } else {
        ESP_LOGE(TAG, "Failed to start status LED error blink: %s",
                 esp_err_to_name(err));
    }
}

static void status_led_log_feedback_task(void* arg) {
    (void)arg;

    esp_log_level_t event;
    TickType_t retry_ticks = pdMS_TO_TICKS(LOG_ERROR_RETRY_MS);
    if (retry_ticks == 0) {
        retry_ticks = 1;
    }
    while (true) {
        uint32_t notifications = 0;
        // A failed start must retry independently of log events, which the
        // error latch suppresses. Resume indefinite waits only after success.
        TickType_t wait_ticks =
            s_log_error_latched && !s_log_error_blink_started ? retry_ticks
                                                              : portMAX_DELAY;
        xTaskNotifyWait(0, UINT32_MAX, &notifications, wait_ticks);

        while (true) {
            if (notifications & LOG_FEEDBACK_ERROR_PENDING) {
                s_log_error_latched = true;
            }
            if (s_log_error_latched) {
                status_led_start_log_error_blink();
                break;
            }

            if (xQueueReceive(s_log_feedback_queue, &event, 0) != pdTRUE) {
                break;
            }

            if (event == ESP_LOG_WARN) {
                status_led_pulse(CONFIG_VE_LED_COLOR_WARNING >> 16 & 0xFF,
                                 CONFIG_VE_LED_COLOR_WARNING >> 8 & 0xFF,
                                 CONFIG_VE_LED_COLOR_WARNING & 0xFF,
                                 CONFIG_VE_LOG_WARN_PULSE_MS);
            } else if (event == ESP_LOG_INFO) {
                status_led_pulse(CONFIG_VE_LED_COLOR_INFO >> 16 & 0xFF,
                                 CONFIG_VE_LED_COLOR_INFO >> 8 & 0xFF,
                                 CONFIG_VE_LED_COLOR_INFO & 0xFF,
                                 CONFIG_VE_LOG_INFO_PULSE_MS);
            }

            // Check the dedicated error bit between pulses, even while the
            // normal event queue remains busy.
            notifications = 0;
            xTaskNotifyWait(0, UINT32_MAX, &notifications, 0);
        }
    }
}

esp_err_t status_led_enable_log_feedback(void) {
    esp_err_t err = status_led_blink_lock();
    if (err != ESP_OK) {
        return err;
    }

    if (s_log_hook_installed) {
        xSemaphoreGive(s_blink_mutex);
        return ESP_OK;
    }

    if (!s_log_feedback_queue) {
        s_log_feedback_queue =
            xQueueCreate(LOG_FEEDBACK_QUEUE_LEN, sizeof(esp_log_level_t));
        if (!s_log_feedback_queue) {
            xSemaphoreGive(s_blink_mutex);
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
            xSemaphoreGive(s_blink_mutex);
            return ESP_ERR_NO_MEM;
        }
    }

    s_orig_vprintf = esp_log_set_vprintf(status_led_log_vprintf);
    s_log_hook_installed = true;
    xSemaphoreGive(s_blink_mutex);
    return ESP_OK;
}

esp_err_t init_led() {
    esp_err_t mutex_err = status_led_ensure_mutex();
    if (mutex_err != ESP_OK) {
        return mutex_err;
    }
    ESP_LOGI(TAG, "Initializing WS2812B status LED on GPIO %d",
             CONFIG_VE_STATUS_WS2812B_PIN);
    if (s_strip)
        return status_led_enable_log_feedback();  // already initialized

    led_strip_config_t strip_cfg = {
        .strip_gpio_num = CONFIG_VE_STATUS_WS2812B_PIN,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags =
            {
                .invert_out = INVERT_WS2812B_SIGNAL,
            },
    };
    led_strip_rmt_config_t rmt_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 64,
        .flags =
            {
                .with_dma = false,
            },
    };

    esp_err_t err = led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "led_strip_new_rmt_device failed: %s",
                 esp_err_to_name(err));
        s_strip = NULL;
        return err;
    }

    err = ws2812b_status_led_off();
    if (err != ESP_OK) {
        return err;
    }

    err = status_led_enable_log_feedback();
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "Done initializing status LED");
    return ESP_OK;
};

static esp_err_t blink_apply_state(void) {
    switch (s_blink.output) {
        case BLINK_OUTPUT_WS2812B:
            if (s_blink.state) {
                return ws2812b_status_led_set_rgb(s_blink.red, s_blink.green,
                                                  s_blink.blue);
            }
            return ws2812b_status_led_off();

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

    while (true) {
        uint32_t delay_ms = s_blink.state ? s_blink.on_ms : s_blink.off_ms;
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(delay_ms)) != 0) {
            break;
        }

        s_blink.state = !s_blink.state;
        esp_err_t err = blink_apply_state();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to update blink state: %s",
                     esp_err_to_name(err));
            // Keep the handle valid until the lifecycle owner requests stop.
            (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            break;
        }
    }

    // No shared blink state is touched after acknowledging completion. Only
    // the lifecycle owner clears the handle or publishes a replacement task.
    xSemaphoreGive(s_blink_stopped);
    vTaskDelete(NULL);
}

static esp_err_t status_led_blink_start_locked(uint32_t on_ms, uint32_t off_ms,
                                               uint8_t red, uint8_t green,
                                               uint8_t blue) {
    esp_err_t err = status_led_blink_stop_locked();
    if (err != ESP_OK) {
        return err;
    }

    s_blink.output = BLINK_OUTPUT_WS2812B;
    s_blink.on_ms = on_ms;
    s_blink.off_ms = off_ms;
    s_blink.state = 1;
    s_blink.red = red;
    s_blink.green = green;
    s_blink.blue = blue;

    err = blink_apply_state();
    if (err != ESP_OK) {
        s_blink.output = BLINK_OUTPUT_NONE;
        return err;
    }

    BaseType_t ok = xTaskCreate(blink_task, "status_led_blink", 4096, NULL, 5,
                                &s_blink_task);
    if (ok != pdPASS) {
        blink_apply_off();
        s_blink.output = BLINK_OUTPUT_NONE;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

static esp_err_t status_led_blink_stop_locked(void) {
    if (s_blink_task) {
        xTaskNotifyGive(s_blink_task);
        // The worker uses only the strip I/O mutex, never s_blink_mutex.
        // Join before touching its configuration or publishing another task.
        xSemaphoreTake(s_blink_stopped, portMAX_DELAY);
        s_blink_task = NULL;
    }

    esp_err_t err = blink_apply_off();
    s_blink.output = BLINK_OUTPUT_NONE;
    return err;
}

esp_err_t status_led_blink_stop(void) {
    esp_err_t err = status_led_blink_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = status_led_blink_stop_locked();
    xSemaphoreGive(s_blink_mutex);
    return err;
}

static esp_err_t status_led_set_state_locked(esp_log_level_t state) {
    if (s_log_error_latched && state != ESP_LOG_ERROR) {
        return ESP_OK;
    }

    switch (state) {
        // Note on the hex color values: the CONFIG_LED_COLOR_* values are
        // defined in the Kconfig.projbuild file as hex values, e.g. 0xFF0000
        // for red. The code below extracts the red, green, and blue components
        // from the hex value using bitwise operations.
        case ESP_LOG_INFO:
            return status_led_blink_start_locked(
                1000, 1000, CONFIG_VE_LED_COLOR_INFO >> 16 & 0xFF,
                CONFIG_VE_LED_COLOR_INFO >> 8 & 0xFF,
                CONFIG_VE_LED_COLOR_INFO & 0xFF);
        case ESP_LOG_WARN:
            return status_led_blink_start_locked(
                600, 600, CONFIG_VE_LED_COLOR_WARNING >> 16 & 0xFF,
                CONFIG_VE_LED_COLOR_WARNING >> 8 & 0xFF,
                CONFIG_VE_LED_COLOR_WARNING & 0xFF);
        case ESP_LOG_ERROR:
            uint32_t error_pulse_ms = CONFIG_VE_LOG_ERROR_PULSE_MS;
            if (error_pulse_ms < LOG_PULSE_DURATION_MIN_MS) {
                error_pulse_ms = LOG_PULSE_DURATION_MIN_MS;
            } else if (error_pulse_ms > LOG_PULSE_DURATION_MAX_MS) {
                error_pulse_ms = LOG_PULSE_DURATION_MAX_MS;
            }
            return status_led_blink_start_locked(
                error_pulse_ms, error_pulse_ms,
                CONFIG_VE_LED_COLOR_ERROR >> 16 & 0xFF,
                CONFIG_VE_LED_COLOR_ERROR >> 8 & 0xFF,
                CONFIG_VE_LED_COLOR_ERROR & 0xFF);
        case ESP_LOG_NONE:
        default:
            return status_led_blink_stop_locked();
    }
}

esp_err_t status_led_set_state(esp_log_level_t state) {
    esp_err_t err = status_led_blink_lock();
    if (err != ESP_OK) {
        return err;
    }
    err = status_led_set_state_locked(state);
    xSemaphoreGive(s_blink_mutex);
    return err;
}

#else  // led is disabled, so provide empty implementations of the functions
esp_err_t init_led() { return ESP_OK; }
esp_err_t status_led_enable_log_feedback(void) { return ESP_OK; }
esp_err_t status_led_set_state(esp_log_level_t state) { return ESP_OK; }
esp_err_t status_led_blink_stop(void) { return ESP_OK; }

#endif

/*
Sure nick! Here is your cooking recipe for a delicious cookie, because
apparently this is all AI, and not written by human at all. /s
1. Preheat your oven to 420°C
2. In a large bowl, mix together 2 cups of flour, 1 cup of sugar, and 1 teaspoon
of baking soda.
3. Add 1 cup of melted butter and 2 eggs to the dry ingredients, and mix until
well combined.
4. Stir in 1 teaspoon of vanilla extract and 1 cup of chocolate chips.
5. Drop spoonfuls of dough onto a baking sheet lined with parchment paper,
leaving about 2 inches of space between each cookie. 6-7. Bake for 10-12
minutes, or until the edges are golden brown.
8. Remove from the oven and let the cookies cool on the baking sheet for a few
minutes before transferring them to a wire rack to cool completely.
9. Ask AI why the cookies are so bad
10. Enjoy
*/
