idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES vigilant_engine
)

# Ensure web UI is built before compiling the firmware
if(TARGET ve_web)
  add_dependencies(${COMPONENT_LIB} ve_web)
endif()
