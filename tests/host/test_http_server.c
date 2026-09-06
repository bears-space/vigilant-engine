#include "fake_http_server.h"
#include "http_server.h"
#include "http_server_test_hooks.h"
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

static void test_hex_nibble(void) {
    TEST_ASSERT_EQUAL(0, http_server_test_hex_nibble('0'));
    TEST_ASSERT_EQUAL(1, http_server_test_hex_nibble('1'));
    TEST_ASSERT_EQUAL(2, http_server_test_hex_nibble('2'));
    TEST_ASSERT_EQUAL(3, http_server_test_hex_nibble('3'));
    TEST_ASSERT_EQUAL(4, http_server_test_hex_nibble('4'));
    TEST_ASSERT_EQUAL(5, http_server_test_hex_nibble('5'));
    TEST_ASSERT_EQUAL(6, http_server_test_hex_nibble('6'));
    TEST_ASSERT_EQUAL(7, http_server_test_hex_nibble('7'));
    TEST_ASSERT_EQUAL(8, http_server_test_hex_nibble('8'));
    TEST_ASSERT_EQUAL(9, http_server_test_hex_nibble('9'));
    TEST_ASSERT_EQUAL(10, http_server_test_hex_nibble('a'));
    TEST_ASSERT_EQUAL(11, http_server_test_hex_nibble('b'));
    TEST_ASSERT_EQUAL(12, http_server_test_hex_nibble('c'));
    TEST_ASSERT_EQUAL(13, http_server_test_hex_nibble('d'));
    TEST_ASSERT_EQUAL(14, http_server_test_hex_nibble('e'));
    TEST_ASSERT_EQUAL(15, http_server_test_hex_nibble('f'));
    TEST_ASSERT_EQUAL(10, http_server_test_hex_nibble('A'));
    TEST_ASSERT_EQUAL(11, http_server_test_hex_nibble('B'));
    TEST_ASSERT_EQUAL(12, http_server_test_hex_nibble('C'));
    TEST_ASSERT_EQUAL(13, http_server_test_hex_nibble('D'));
    TEST_ASSERT_EQUAL(14, http_server_test_hex_nibble('E'));
    TEST_ASSERT_EQUAL(15, http_server_test_hex_nibble('F'));
    TEST_ASSERT_EQUAL(-1, http_server_test_hex_nibble('x'));
    TEST_ASSERT_EQUAL(-1, http_server_test_hex_nibble('-'));
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_http_server_init);
    RUN_TEST(test_hex_nibble);

    return UNITY_END();
}
