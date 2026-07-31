idf_component_register(
    SRCS
        "main.c"
        "sensor/example_sensor.c"
    INCLUDE_DIRS "."
    REQUIRES vigilant_engine
)
