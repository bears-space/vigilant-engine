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

static void test_uri_decode(void) {
    char dest[100];
    esp_err_t err;

    err =http_server_test_uri_decode(dest, "Hello%20World%21", 100);
    TEST_ASSERT_EQUAL_STRING("Hello World!", dest);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    err = http_server_test_uri_decode(dest, "simple%20test", 100);
    TEST_ASSERT_EQUAL_STRING("simple test", dest);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    err = http_server_test_uri_decode(dest, "price%3D%24100%26tax%3D5%25", 100);
    TEST_ASSERT_EQUAL_STRING("price=$100&tax=5%", dest);
    TEST_ASSERT_EQUAL(ESP_OK, err);

    err = http_server_test_uri_decode(NULL, "Hello%20World%21", 100);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);

    err = http_server_test_uri_decode(dest, NULL, 100);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);

    err = http_server_test_uri_decode(dest, "%", 100);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);

    err = http_server_test_uri_decode(dest, "%A", 100);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
    
    err = http_server_test_uri_decode(dest, "%GG", 100);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);

    err = http_server_test_uri_decode(dest, "%E0%A", 100);
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, err);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_http_server_init);
    RUN_TEST(test_hex_nibble);
    RUN_TEST(test_uri_decode);

    return UNITY_END();
}
