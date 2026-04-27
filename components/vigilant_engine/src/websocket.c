#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char* TAG_WS = "ws";

#define MAX_WS_PAYLOAD (8 * 1024)
#define LOG_HISTORY_LINES 200
#define LOG_LINE_MAX 256
#define MAX_WS_CLIENTS 8
#define MAX_PENDING_SENDS_PER_CLIENT 3

typedef struct {
    int fd;
    bool active;
    uint32_t generation;
    uint8_t pending_sends;
} ws_client_t;

typedef struct {
    httpd_handle_t hd;
    int fd;
    uint32_t generation;
    char* payload;
    size_t len;
} ws_send_arg_t;

static httpd_handle_t s_server_handle = NULL;
static ws_client_t s_clients[MAX_WS_CLIENTS];

static char s_log_lines[LOG_HISTORY_LINES][LOG_LINE_MAX];
static size_t s_log_head = 0;   // next write position
static size_t s_log_count = 0;  // number of valid lines
static SemaphoreHandle_t s_ws_mutex = NULL;
static uint32_t s_next_generation = 1;

static vprintf_like_t s_orig_vprintf = NULL;

// Forward declarations
static void send_log_history(int fd);
static esp_err_t ws_queue_send_text(int fd, const char* text);

static bool ws_client_is_connected(int fd) {
    if (!s_server_handle || fd < 0) {
        return false;
    }

    return httpd_ws_get_fd_info(s_server_handle, fd) ==
           HTTPD_WS_CLIENT_WEBSOCKET;
}

static uint32_t next_generation(void) {
    uint32_t generation = s_next_generation++;
    return generation;
}

static SemaphoreHandle_t ensure_mutex(void) {
    if (!s_ws_mutex) {
        s_ws_mutex = xSemaphoreCreateMutex();
    }
    return s_ws_mutex;  // returns NULL on failure, but we check for that in
                        // callers
}

static uint32_t ws_clients_add(int fd) {
    ensure_mutex();
    if (!s_ws_mutex) return 0;

    xSemaphoreTake(s_ws_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_WS_CLIENTS; ++i) {
        if (s_clients[i].active && s_clients[i].fd == fd) {
            s_clients[i].generation = next_generation();
            s_clients[i].pending_sends = 0;
            uint32_t generation = s_clients[i].generation;
            xSemaphoreGive(s_ws_mutex);
            return generation;
        }
    }

    for (int i = 0; i < MAX_WS_CLIENTS; ++i) {
        if (!s_clients[i].active) {
            s_clients[i].fd = fd;
            s_clients[i].active = true;
            s_clients[i].generation = next_generation();
            s_clients[i].pending_sends = 0;
            uint32_t generation = s_clients[i].generation;
            xSemaphoreGive(s_ws_mutex);
            return generation;
        }
    }
    xSemaphoreGive(s_ws_mutex);

    ESP_LOGW(TAG_WS, "WS client table full, dropping fd=%d", fd);
    return 0;
}

static bool ws_clients_remove_generation(int fd, uint32_t generation) {
    if (!s_ws_mutex) return false;
    bool removed = false;
    xSemaphoreTake(s_ws_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_WS_CLIENTS; ++i) {
        if (s_clients[i].active && s_clients[i].fd == fd &&
            (generation == 0 || s_clients[i].generation == generation)) {
            s_clients[i].active = false;
            s_clients[i].fd = -1;
            s_clients[i].generation = 0;
            s_clients[i].pending_sends = 0;
            removed = true;
        }
    }
    xSemaphoreGive(s_ws_mutex);
    return removed;
}

static void ws_clients_remove(int fd) {
    (void)ws_clients_remove_generation(fd, 0);
}

static bool ws_clients_mark_send_queued(int fd, uint32_t* generation) {
    SemaphoreHandle_t mutex = ensure_mutex();
    if (!mutex) return false;
    if (!s_ws_mutex) return false;

    bool queued = false;
    xSemaphoreTake(s_ws_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_WS_CLIENTS; ++i) {
        if (s_clients[i].active && s_clients[i].fd == fd) {
            if (s_clients[i].pending_sends < MAX_PENDING_SENDS_PER_CLIENT) {
                s_clients[i].pending_sends++;
                *generation = s_clients[i].generation;
                queued = true;
            }
            break;
        }
    }
    xSemaphoreGive(s_ws_mutex);
    return queued;
}

static void ws_clients_mark_send_done(int fd, uint32_t generation) {
    if (!s_ws_mutex) return;

    xSemaphoreTake(s_ws_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_WS_CLIENTS; ++i) {
        if (s_clients[i].active && s_clients[i].fd == fd &&
            s_clients[i].generation == generation) {
            if (s_clients[i].pending_sends > 0) {
                s_clients[i].pending_sends--;
            }
            break;
        }
    }
    xSemaphoreGive(s_ws_mutex);
}

static bool ws_client_generation_is_current(int fd, uint32_t generation) {
    if (!s_ws_mutex) return false;

    bool current = false;
    xSemaphoreTake(s_ws_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_WS_CLIENTS; ++i) {
        if (s_clients[i].active && s_clients[i].fd == fd &&
            s_clients[i].generation == generation) {
            current = true;
            break;
        }
    }
    xSemaphoreGive(s_ws_mutex);
    return current;
}

static void ws_trigger_close_if_current(int fd, uint32_t generation) {
    if (ws_clients_remove_generation(fd, generation) && s_server_handle) {
        httpd_sess_trigger_close(s_server_handle, fd);
    }
}

static bool should_stream_log_line(const char* line) {
    // Avoid feeding HTTPD/WebSocket transport errors back into the WebSocket
    // that just failed. They still print on UART and remain in the history.
    return !strstr(line, " httpd_txrx:") && !strstr(line, " httpd_ws:");
}

static void append_log_line(const char* line) {
    ensure_mutex();
    if (!s_ws_mutex) return;

    xSemaphoreTake(s_ws_mutex, portMAX_DELAY);
    strncpy(s_log_lines[s_log_head], line, LOG_LINE_MAX - 1);
    s_log_lines[s_log_head][LOG_LINE_MAX - 1] = '\0';

    s_log_head = (s_log_head + 1) % LOG_HISTORY_LINES;
    if (s_log_count < LOG_HISTORY_LINES) {
        s_log_count++;
    }
    xSemaphoreGive(s_ws_mutex);
}

static void ws_send_text_async(void* arg) {
    ws_send_arg_t* a = (ws_send_arg_t*)arg;
    if (!a) return;

    if (!ws_client_generation_is_current(a->fd, a->generation) ||
        !ws_client_is_connected(a->fd)) {
        ws_clients_mark_send_done(a->fd, a->generation);
        ws_trigger_close_if_current(a->fd, a->generation);
        free(a->payload);
        free(a);
        return;
    }

    httpd_ws_frame_t frame = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t*)a->payload,
        .len = a->len,
    };

    esp_err_t ret = httpd_ws_send_frame_async(a->hd, a->fd, &frame);
    ws_clients_mark_send_done(a->fd, a->generation);
    if (ret != ESP_OK) {
        ws_trigger_close_if_current(a->fd, a->generation);
    }

    free(a->payload);
    free(a);
}

static esp_err_t ws_queue_send_text(int fd, const char* text) {
    if (!s_server_handle || !text) {
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t generation = 0;
    if (!ws_clients_mark_send_queued(fd, &generation)) {
        return ESP_ERR_INVALID_STATE;
    }

    ws_send_arg_t* arg = (ws_send_arg_t*)malloc(sizeof(ws_send_arg_t));
    char* dup = strdup(text);
    if (!arg || !dup) {
        free(arg);
        free(dup);
        ws_clients_mark_send_done(fd, generation);
        return ESP_ERR_NO_MEM;
    }

    arg->hd = s_server_handle;
    arg->fd = fd;
    arg->generation = generation;
    arg->payload = dup;
    arg->len = strlen(dup);

    esp_err_t ret = httpd_queue_work(s_server_handle, ws_send_text_async, arg);
    if (ret != ESP_OK) {
        free(arg->payload);
        free(arg);
        ws_clients_mark_send_done(fd, generation);
    }
    return ret;
}

static void broadcast_log_line(const char* line) {
    ensure_mutex();
    if (!s_ws_mutex) return;

    int fds[MAX_WS_CLIENTS];
    size_t cnt = 0;

    xSemaphoreTake(s_ws_mutex, portMAX_DELAY);
    for (int i = 0; i < MAX_WS_CLIENTS; ++i) {
        if (s_clients[i].active) {
            fds[cnt++] = s_clients[i].fd;
        }
    }
    xSemaphoreGive(s_ws_mutex);

    if (cnt == 0) {
        return;
    }

    cJSON* root = cJSON_CreateObject();
    if (!root) return;
    cJSON_AddStringToObject(root, "type", "log");
    cJSON_AddStringToObject(root, "line", line);
    char* out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!out) return;

    for (size_t i = 0; i < cnt; ++i) {
        ws_queue_send_text(fds[i], out);
    }
    free(out);
}

static int websocket_log_vprintf(const char* fmt, va_list ap) {
    char line[LOG_LINE_MAX];

    va_list ap_copy;
    va_copy(ap_copy, ap);
    vsnprintf(line, sizeof(line), fmt, ap_copy);
    va_end(ap_copy);

    // Strip trailing newline to avoid double spacing on client
    size_t len = strlen(line);
    if (len && line[len - 1] == '\n') {
        line[len - 1] = '\0';
    }

    append_log_line(line);
    if (should_stream_log_line(line)) {
        broadcast_log_line(line);
    }

    if (s_orig_vprintf) {
        return s_orig_vprintf(fmt, ap);
    }

    return vprintf(fmt, ap);
}

static void send_log_history(int fd) {
    ensure_mutex();
    if (!s_ws_mutex) return;

    cJSON* root = cJSON_CreateObject();
    if (!root) return;

    cJSON_AddStringToObject(root, "type", "logs");
    cJSON* arr = cJSON_AddArrayToObject(root, "lines");
    if (!arr) {
        cJSON_Delete(root);
        return;
    }

    xSemaphoreTake(s_ws_mutex, portMAX_DELAY);
    size_t count = s_log_count;
    size_t idx = (s_log_head + LOG_HISTORY_LINES - s_log_count) %
                 LOG_HISTORY_LINES;  // oldest
    for (size_t i = 0; i < count; ++i) {
        cJSON_AddItemToArray(arr, cJSON_CreateString(s_log_lines[idx]));
        idx = (idx + 1) % LOG_HISTORY_LINES;
    }
    xSemaphoreGive(s_ws_mutex);

    char* out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!out) return;

    ws_queue_send_text(fd, out);
    free(out);
}

static esp_err_t ws_send_text(httpd_req_t* req, const char* text) {
    httpd_ws_frame_t out = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t*)text,
        .len = strlen(text),
    };
    return httpd_ws_send_frame(req, &out);
}

static esp_err_t websocket_handler(httpd_req_t* req) {
    esp_err_t ret = ESP_OK;
    httpd_ws_frame_t ws_pkt = {0};
    int fd = httpd_req_to_sockfd(req);

    // Handshake call
    if (req->method == HTTP_GET) {
        uint32_t generation = ws_clients_add(fd);
        if (generation == 0) {
            httpd_sess_trigger_close(req->handle, fd);
            return ESP_FAIL;
        }
        send_log_history(fd);
        ESP_LOGI(TAG_WS, "WebSocket client connected: fd=%d", fd);
        return ESP_OK;
    }

    // 1) First pass: query frame length/type
    ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ws_clients_remove(fd);
        return ret;
    }

    if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE) {
        ws_clients_remove(fd);
        if (ws_pkt.len == 0) {
            return ESP_OK;
        }
    }

    if (ws_pkt.len == 0) {
        return ESP_OK;
    }

    if (ws_pkt.len > MAX_WS_PAYLOAD) {
        ESP_LOGE(TAG_WS, "WS payload too large: %u", (unsigned)ws_pkt.len);
        return ESP_ERR_INVALID_SIZE;
    }

    // 2) Allocate payload buffer (+1 so we can null-terminate text)
    ws_pkt.payload = (uint8_t*)malloc(ws_pkt.len + 1);
    if (!ws_pkt.payload) {
        return ESP_ERR_NO_MEM;
    }

    // 3) Second pass: actually receive frame data
    ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    if (ret != ESP_OK) {
        ws_clients_remove(fd);
        free(ws_pkt.payload);
        return ret;
    }

    if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE) {
        ws_clients_remove(fd);
        free(ws_pkt.payload);
        return ESP_OK;
    }

    if (ws_pkt.type != HTTPD_WS_TYPE_TEXT) {
        free(ws_pkt.payload);
        return ESP_OK;
    }

    ((char*)ws_pkt.payload)[ws_pkt.len] = '\0';

    cJSON* root = cJSON_Parse((char*)ws_pkt.payload);
    if (!root) {
        ws_send_text(req, "{\"type\":\"error\",\"msg\":\"invalid json\"}");
        free(ws_pkt.payload);
        return ESP_OK;
    }

    cJSON* type = cJSON_GetObjectItem(root, "type");
    if (cJSON_IsString(type) && type->valuestring &&
        strcmp(type->valuestring, "get-logs") == 0) {
        send_log_history(fd);
    } else if (cJSON_IsString(type) && type->valuestring &&
               strcmp(type->valuestring, "ping") == 0) {
        ws_send_text(req, "{\"type\":\"pong\"}");
    } else {
        ws_send_text(
            req, "{\"type\":\"error\",\"msg\":\"unknown or missing type\"}");
    }

    cJSON_Delete(root);
    free(ws_pkt.payload);
    return ESP_OK;
}

void websocket_init_log_capture(void) {
    if (s_orig_vprintf) {
        return;  // already hooked
    }

    s_orig_vprintf = esp_log_set_vprintf(websocket_log_vprintf);
    ensure_mutex();
}

void websocket_client_closed(int fd) { ws_clients_remove(fd); }

/**
 * ✅ This symbol must exist (non-static), because your http_server.c links
 * against it.
 */
esp_err_t websocket_register_handlers(httpd_handle_t server) {
    s_server_handle = server;
    websocket_init_log_capture();

    httpd_uri_t ws = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = websocket_handler,
        .user_ctx = NULL,
        .is_websocket = true,
    };

    esp_err_t ret = httpd_register_uri_handler(server, &ws);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_WS, "register ws handler failed: %d", ret);
        return ret;
    }

    ESP_LOGI(TAG_WS, "ws handler registered at %s", ws.uri);
    return ESP_OK;
}
