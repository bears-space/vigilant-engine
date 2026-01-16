#include "ws_mgr.h"
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define MAX_WS_CLIENTS 8

static httpd_handle_t s_httpd = NULL;
static SemaphoreHandle_t s_mux;
static int s_clients[MAX_WS_CLIENTS];
static size_t s_client_count = 0;

typedef struct {
    httpd_handle_t hd;
    int fd;
    httpd_ws_type_t type;
    size_t len;
    uint8_t *payload;
} ws_send_job_t;

static void ws_send_job(void *arg)
{
    ws_send_job_t *job = (ws_send_job_t *)arg;

    httpd_ws_frame_t frame = {
        .type = job->type,
        .payload = job->payload,
        .len = job->len,
    };

    (void) httpd_ws_send_frame_async(job->hd, job->fd, &frame);

    free(job->payload);
    free(job);
}

esp_err_t ws_mgr_init(httpd_handle_t server)
{
    s_httpd = server;
    if (!s_mux) s_mux = xSemaphoreCreateMutex();
    return (s_httpd && s_mux) ? ESP_OK : ESP_FAIL;
}

esp_err_t ws_mgr_add_client(int sockfd)
{
    if (!s_mux) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_mux, portMAX_DELAY);

    // schon drin?
    for (size_t i = 0; i < s_client_count; i++) {
        if (s_clients[i] == sockfd) {
            xSemaphoreGive(s_mux);
            return ESP_OK;
        }
    }

    if (s_client_count >= MAX_WS_CLIENTS) {
        xSemaphoreGive(s_mux);
        return ESP_ERR_NO_MEM;
    }

    s_clients[s_client_count++] = sockfd;
    xSemaphoreGive(s_mux);
    return ESP_OK;
}

esp_err_t ws_mgr_remove_client(int sockfd)
{
    if (!s_mux) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_mux, portMAX_DELAY);

    for (size_t i = 0; i < s_client_count; i++) {
        if (s_clients[i] == sockfd) {
            s_clients[i] = s_clients[s_client_count - 1];
            s_client_count--;
            break;
        }
    }

    xSemaphoreGive(s_mux);
    return ESP_OK;
}

esp_err_t ws_mgr_send_text(int sockfd, const char *text)
{
    if (!s_httpd || !text) return ESP_ERR_INVALID_STATE;

    size_t len = strlen(text);

    ws_send_job_t *job = calloc(1, sizeof(*job));
    if (!job) return ESP_ERR_NO_MEM;

    job->payload = malloc(len);
    if (!job->payload) {
        free(job);
        return ESP_ERR_NO_MEM;
    }

    memcpy(job->payload, text, len);

    job->hd = s_httpd;
    job->fd = sockfd;
    job->type = HTTPD_WS_TYPE_TEXT;
    job->len = len;

    // WICHTIG: wird im HTTPD-Server-Task ausgeführt
    esp_err_t err = httpd_queue_work(s_httpd, ws_send_job, job);
    if (err != ESP_OK) {
        free(job->payload);
        free(job);
    }
    return err;
}

esp_err_t ws_mgr_broadcast_text(const char *text)
{
    if (!s_mux) return ESP_ERR_INVALID_STATE;

    // snapshot der fds ziehen, dann ohne lock senden
    int fds[MAX_WS_CLIENTS];
    size_t n = 0;

    xSemaphoreTake(s_mux, portMAX_DELAY);
    n = s_client_count;
    for (size_t i = 0; i < n; i++) fds[i] = s_clients[i];
    xSemaphoreGive(s_mux);

    for (size_t i = 0; i < n; i++) {
        (void) ws_mgr_send_text(fds[i], text);
    }
    return ESP_OK;
}
