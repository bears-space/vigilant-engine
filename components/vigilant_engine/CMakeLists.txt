set(requires esp-tls nvs_flash esp_netif esp_http_server esp_http_client driver esp_driver_gpio esp_driver_i2c)
idf_build_get_property(target IDF_TARGET)
idf_build_get_property(build_dir BUILD_DIR)
idf_build_get_property(ve_build_web VE_BUILD_WEB)
idf_build_get_property(ve_vigilant_html VE_VIGILANT_HTML)

# Derive frontend path relative to this component to avoid environment-dependent PROJECT_SOURCE_DIR.
get_filename_component(repo_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
get_filename_component(frontend_dir "${CMAKE_CURRENT_LIST_DIR}/../../vigilant-engine-frontend" ABSOLUTE)
set(vigilant_html_from_build "${build_dir}/static/vigilant/vigilant.html")
set(vigilant_html "")

if(ve_vigilant_html)
    if(IS_ABSOLUTE "${ve_vigilant_html}")
        set(vigilant_html "${ve_vigilant_html}")
    else()
        get_filename_component(vigilant_html "${repo_root}/${ve_vigilant_html}" ABSOLUTE)
    endif()

    if(NOT EXISTS "${vigilant_html}")
        message(FATAL_ERROR
            "VE_VIGILANT_HTML points to '${vigilant_html}', but that file does not exist."
        )
    endif()

    message(STATUS "Using prebuilt Vigilant UI from ${vigilant_html}")
elseif(EXISTS "${vigilant_html_from_build}")
    set(vigilant_html "${vigilant_html_from_build}")
    message(STATUS "Using existing Vigilant UI build artifact at ${vigilant_html}")
endif()

function(run_npm_step working_dir step_label)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "PATH=${NODE_BIN_DIR}:$ENV{PATH}" "${NPM_COMMAND}" ${ARGN}
        WORKING_DIRECTORY "${working_dir}"
        RESULT_VARIABLE step_result
        OUTPUT_VARIABLE step_stdout
        ERROR_VARIABLE step_stderr
    )
    if(NOT step_result EQUAL 0)
        string(JOIN " " step_command "${NPM_COMMAND}" ${ARGN})
        message(FATAL_ERROR
            "${step_label} failed (exit ${step_result}) in ${working_dir}.\n"
            "command: ${step_command}\n"
            "stdout:\n${step_stdout}\n"
            "stderr:\n${step_stderr}\n"
        )
    endif()
endfunction()

# Auto-build the frontend when allowed and no prebuilt HTML was supplied.
if(NOT vigilant_html)
    if(NOT ve_build_web)
        message(FATAL_ERROR
            "Vigilant UI HTML is missing and VE_BUILD_WEB=OFF.\n"
            "Provide -DVE_VIGILANT_HTML=/path/to/vigilant.html or place a prebuilt file at ${vigilant_html_from_build}."
        )
    endif()

    if(NOT EXISTS "${frontend_dir}/package.json")
        message(FATAL_ERROR
            "Frontend directory not found at ${frontend_dir}. "
            "Is the repo checked out with subdir vigilant-engine-frontend?"
        )
    endif()

    # Locate npm; refresh the cache if the stored path vanished (common when using nvm).
    find_program(NPM_COMMAND npm)
    if(NPM_COMMAND AND NOT EXISTS "${NPM_COMMAND}")
        message(WARNING "Cached npm path (${NPM_COMMAND}) no longer exists; re-searching PATH")
        unset(NPM_COMMAND CACHE)
        find_program(NPM_COMMAND npm)
    endif()
    if(NOT NPM_COMMAND)
        message(FATAL_ERROR "npm not found in PATH. Install Node.js or set VE_BUILD_WEB=OFF with VE_VIGILANT_HTML.")
    endif()

    find_program(NODE_COMMAND node)
    if(NODE_COMMAND AND NOT EXISTS "${NODE_COMMAND}")
        unset(NODE_COMMAND CACHE)
        find_program(NODE_COMMAND node)
    endif()
    if(NOT NODE_COMMAND)
        message(FATAL_ERROR "node not found in PATH. Install Node.js or set VE_BUILD_WEB=OFF with VE_VIGILANT_HTML.")
    endif()

    get_filename_component(NODE_BIN_DIR "${NODE_COMMAND}" DIRECTORY)
    message(STATUS "Building Vigilant UI with npm at ${NPM_COMMAND}")
    message(STATUS "Vigilant UI not found at ${vigilant_html_from_build}; running npm run build:vigilant")

    if(EXISTS "${repo_root}/package.json")
        run_npm_step("${repo_root}" "Repo npm install" install)
    endif()
    run_npm_step("${frontend_dir}" "Frontend npm install" install)
    run_npm_step("${frontend_dir}" "npm run build:vigilant" run build:vigilant)

    if(NOT EXISTS "${vigilant_html_from_build}")
        message(FATAL_ERROR "Expected generated UI at ${vigilant_html_from_build} but it was not created.")
    endif()

    set(vigilant_html "${vigilant_html_from_build}")
endif()

if(${target} STREQUAL "linux")
    list(APPEND requires esp_stubs)
else()
    list(APPEND requires esp_wifi esp_eth)
endif()

set(vigilant_engine_srcs
    "src/http_server.c"
    "src/ota_http.c"
    "src/vigilant.c"
    "src/status_led.c"
    "src/websocket.c"
    "src/master.c"
)

if(CONFIG_VE_ENABLE_I2C)
    list(APPEND vigilant_engine_srcs "src/i2c.c")
endif()

idf_component_register(
    SRCS
        ${vigilant_engine_srcs}
    INCLUDE_DIRS
        "include"
    EMBED_FILES
        ${vigilant_html}
    REQUIRES
        ${requires}
        app_update
)
