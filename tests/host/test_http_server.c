#include "fake_http_server.h"
#include "http_server.h"
#include "unity.h"

void setUp(void) {
    fake_http_server_reset();
    http_server_stop();
    fake_http_server_reset();
}

void tearDown(void) {
    fake_http_server.stop_result = ESP_OK;
    http_server_stop();
}

// test function
static void test_http_server_init(void) {
    esp_err_t err = http_server_start();
    TEST_ASSERT_EQUAL(ESP_OK, err);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_http_server_init);

    return UNITY_END();
}
