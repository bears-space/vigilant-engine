cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED ENV{IDF_PATH} OR "$ENV{IDF_PATH}" STREQUAL "")
  message(FATAL_ERROR
    "IDF_PATH is not set. Open the recovery firmware from an exported ESP-IDF shell or use the VS Code ESP-IDF extension. "
    "Generic CMake Tools launches do not load the ESP-IDF environment automatically.")
endif()

if(NOT DEFINED IDF_TARGET AND NOT DEFINED ENV{IDF_TARGET})
  set(IDF_TARGET "esp32c6")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)

# ---- Web UI (Vite) build integration ----
option(VE_BUILD_WEB "Build Vite UI before compiling firmware" ON)

get_filename_component(VE_REPO_ROOT "${CMAKE_SOURCE_DIR}/.." ABSOLUTE)
set(VE_NODE_ROOT_DIR "${VE_REPO_ROOT}")
set(VE_WEB_DIR "${VE_REPO_ROOT}/vigilant-engine-frontend")
set(VE_RECOVERY_HTML "${VE_REPO_ROOT}/build/static/recovery/index.html")

idf_build_set_property(VE_BUILD_WEB "${VE_BUILD_WEB}")
idf_build_set_property(VE_RECOVERY_HTML "${VE_RECOVERY_HTML}")

if(VE_BUILD_WEB)
  file(GLOB_RECURSE VE_WEB_SOURCES CONFIGURE_DEPENDS
    "${VE_NODE_ROOT_DIR}/package.json"
    "${VE_NODE_ROOT_DIR}/package-lock.json"
    "${VE_NODE_ROOT_DIR}/pnpm-lock.yaml"
    "${VE_NODE_ROOT_DIR}/yarn.lock"
    "${VE_WEB_DIR}/index.html"
    "${VE_WEB_DIR}/vigilant.html"
    "${VE_WEB_DIR}/vite.config.*"
    "${VE_WEB_DIR}/package.json"
    "${VE_WEB_DIR}/package-lock.json"
    "${VE_WEB_DIR}/pnpm-lock.yaml"
    "${VE_WEB_DIR}/yarn.lock"
    "${VE_WEB_DIR}/src/*"
  )

  find_program(NPM_EXECUTABLE npm)
  if(NOT NPM_EXECUTABLE)
    message(FATAL_ERROR "npm not found. Install Node.js/npm or set VE_BUILD_WEB=OFF.")
  endif()

  add_custom_command(
    OUTPUT "${VE_RECOVERY_HTML}"

    COMMAND "${CMAKE_COMMAND}" -E chdir "${VE_NODE_ROOT_DIR}" "${NPM_EXECUTABLE}" install
    COMMAND "${CMAKE_COMMAND}" -E chdir "${VE_WEB_DIR}" "${NPM_EXECUTABLE}" install
    COMMAND "${CMAKE_COMMAND}" -E chdir "${VE_WEB_DIR}" "${NPM_EXECUTABLE}" run build:recovery

    DEPENDS ${VE_WEB_SOURCES}
    COMMENT "Building Vite UI (recovery)"
    VERBATIM
  )

  add_custom_target(ve_web ALL DEPENDS "${VE_RECOVERY_HTML}")
endif()

# Keep project() after ve_web so the main component can depend on the generated
# build/static/recovery/index.html before ESP-IDF creates the embed rule.
project(vigilant-engine-recovery)
