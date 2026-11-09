/*
# SPDX-AI-Disclosure: ai-generated
# SPDX-AI-Model: gpt-6
# SPDX-AI-Provider: OpenAI
# SPDX-AI-Scope: Internal SPI transport interface.
# SPDX-AI-Date: 2026-10-13
*/

#pragma once

#include "tcan4550.h"

/* Initialization/deinitialization require exclusive application ownership. */
esp_err_t tcan4550_spi_init(const tcan4550_config_t* config);
esp_err_t tcan4550_spi_deinit(void);
esp_err_t tcan4550_spi_error(void);
