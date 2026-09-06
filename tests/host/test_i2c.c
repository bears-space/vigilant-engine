#include "fake_i2c.h"
#include "i2c.h"
#include "unity.h"

void setUp(void) {
    /* Keep each test independent from earlier bus and fake-driver state. */
    fake_i2c_reset();
    i2c_deinit();
    fake_i2c_reset();
}

void tearDown(void) {
    /* Ensure cleanup still works if a test changes the fake delete result. */
    fake_i2c.delete_result = ESP_OK;
    i2c_deinit();
}

int main(void) {
    UNITY_BEGIN();

    /* Add RUN_TEST(test_name); here for each test you write. */

    return UNITY_END();
}
