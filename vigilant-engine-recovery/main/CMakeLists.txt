idf_build_get_property(recovery_html VE_RECOVERY_HTML)
idf_build_get_property(ve_build_web VE_BUILD_WEB)

if(NOT recovery_html)
    idf_build_get_property(build_dir BUILD_DIR)
    set(recovery_html "${build_dir}/static/recovery/index.html")
endif()

if(NOT ve_build_web AND NOT EXISTS "${recovery_html}")
    message(FATAL_ERROR
        "Recovery UI HTML is missing and VE_BUILD_WEB=OFF.\n"
        "Expected prebuilt file at ${recovery_html}."
    )
endif()

idf_component_register(
    SRCS "recovery_app.c"
    INCLUDE_DIRS "."
    EMBED_FILES
        "${recovery_html}"
    REQUIRES
        esp_wifi
        esp_netif
        esp_event
        nvs_flash
        esp_http_server
        app_update
        esp_partition
        status_led
)

if(VE_RECOVERY_CONFIG_HEADER)
  if(NOT EXISTS "${VE_RECOVERY_CONFIG_HEADER}")
    message(FATAL_ERROR
      "VE_RECOVERY_CONFIG_HEADER points to '${VE_RECOVERY_CONFIG_HEADER}', but that file does not exist."
    )
  endif()

  get_filename_component(ve_recovery_config_dir "${VE_RECOVERY_CONFIG_HEADER}" DIRECTORY)
  target_include_directories(${COMPONENT_LIB} PRIVATE "${ve_recovery_config_dir}")
  target_compile_definitions(${COMPONENT_LIB} PRIVATE VE_RECOVERY_USE_PARENT_CONFIG=1)
endif()

if(TARGET ve_web)
  add_dependencies(${COMPONENT_LIB} ve_web)
endif()
