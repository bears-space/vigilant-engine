#pragma once

/* Deterministic host test configuration; these pins never touch hardware. */
#define CONFIG_VE_ENABLE_I2C 1
#define CONFIG_VE_I2C_SCL_IO 35
#define CONFIG_VE_I2C_SDA_IO 34
#define CONFIG_VE_I2C_FREQ_HZ 100000
