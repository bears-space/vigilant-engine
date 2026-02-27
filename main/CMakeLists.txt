idf_component_register(
    SRCS "main.c" "narrowband.cpp"
    INCLUDE_DIRS "."
    REQUIRES vigilant_engine esp_timer driver
)
