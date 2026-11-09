# SPDX-AI-Disclosure: ai-assisted
# SPDX-AI-Model: gpt-6
# SPDX-AI-Provider: OpenAI
# SPDX-AI-Scope: Build integration and license-notice copying.
# SPDX-AI-Date: 2026-10-13

idf_component_register(
    SRCS "tcan4550.c" "tcan4550_spi.c" "ti/TCAN4550.c"
    INCLUDE_DIRS "include" "ti"
    REQUIRES esp_driver_gpio esp_driver_spi
    PRIV_REQUIRES heap
)

# Include this notice alongside firmware binaries when distributing the build.
configure_file("${CMAKE_CURRENT_LIST_DIR}/ti/LICENSE.txt"
               "${CMAKE_BINARY_DIR}/TCAN4550-TI-LICENSE.txt" COPYONLY)
