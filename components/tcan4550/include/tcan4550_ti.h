/*
# SPDX-AI-Disclosure: ai-generated
# SPDX-AI-Model: gpt-6
# SPDX-AI-Provider: OpenAI
# SPDX-AI-Scope: TI API session interface and documentation.
# SPDX-AI-Date: 2026-10-13
*/

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "TCAN4550.h"

/**
 * Lock the initialized singleton for direct TI library calls from task context.
 * Pair every successful begin with end, on the same task. Sessions cannot nest.
 * Do not call the tcan4550_* convenience API inside a TI session.
 * TI's void/bool APIs cannot report ESP-IDF transport failures: end returns the
 * first transport error and suppresses subsequent transfers in that session.
 * Callers must validate TI API arguments and maintain its configuration cache.
 */
esp_err_t tcan4550_ti_begin(uint32_t timeout_ms);
esp_err_t tcan4550_ti_end(void);

#ifdef __cplusplus
}
#endif
