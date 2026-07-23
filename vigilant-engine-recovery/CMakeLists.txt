cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED ENV{IDF_PATH} OR "$ENV{IDF_PATH}" STREQUAL "")
  message(FATAL_ERROR
    "IDF_PATH is not set. Open the recovery firmware from an exported ESP-IDF shell or use the VS Code ESP-IDF extension. "
    "Generic CMake Tools launches do not load the ESP-IDF environment automatically.")
endif()

if(NOT DEFINED IDF_TARGET AND NOT DEFINED ENV{IDF_TARGET})
  set(IDF_TARGET "esp32s3")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)

# ---- Web UI (Vite) build integration ----
option(VE_BUILD_WEB "Build Vite UI before compiling firmware" ON)
set(VE_RECOVERY_HTML "" CACHE FILEPATH "Path to a prebuilt recovery HTML file")

get_filename_component(VE_REPO_ROOT "${CMAKE_SOURCE_DIR}/.." ABSOLUTE)
set(VE_NODE_ROOT_DIR "${VE_REPO_ROOT}")
set(VE_WEB_DIR "${VE_REPO_ROOT}/vigilant-engine-frontend")
set(VE_RECOVERY_HTML_FROM_BUILD "${VE_REPO_ROOT}/build/static/recovery/index.html")
set(VE_ROOT_PARTITIONS_CSV "${VE_REPO_ROOT}/partitions.csv")
set(VE_RECOVERY_PARTITIONS_CSV "${CMAKE_SOURCE_DIR}/partitions.csv")

if(EXISTS "${VE_ROOT_PARTITIONS_CSV}")
  configure_file("${VE_ROOT_PARTITIONS_CSV}" "${VE_RECOVERY_PARTITIONS_CSV}" COPYONLY)
endif()

idf_build_set_property(VE_BUILD_WEB "${VE_BUILD_WEB}")

if(VE_RECOVERY_HTML)
  if(IS_ABSOLUTE "${VE_RECOVERY_HTML}")
    set(ve_recovery_html_source "${VE_RECOVERY_HTML}")
  else()
    get_filename_component(ve_recovery_html_source "${VE_REPO_ROOT}/${VE_RECOVERY_HTML}" ABSOLUTE)
  endif()

  if(NOT EXISTS "${ve_recovery_html_source}")
    message(FATAL_ERROR
      "VE_RECOVERY_HTML points to '${ve_recovery_html_source}', but that file does not exist."
    )
  endif()

  set(ve_recovery_html "${CMAKE_BINARY_DIR}/static/recovery/index.html")
  get_filename_component(ve_recovery_html_dir "${ve_recovery_html}" DIRECTORY)
  file(MAKE_DIRECTORY "${ve_recovery_html_dir}")
  configure_file("${ve_recovery_html_source}" "${ve_recovery_html}" COPYONLY)

  message(STATUS "Using prebuilt Recovery UI from ${ve_recovery_html_source}")
else()
  set(ve_recovery_html "${VE_RECOVERY_HTML_FROM_BUILD}")
endif()

idf_build_set_property(VE_RECOVERY_HTML "${ve_recovery_html}")

if(VE_BUILD_WEB AND NOT VE_RECOVERY_HTML)
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
    OUTPUT "${ve_recovery_html}"

    COMMAND "${CMAKE_COMMAND}" -E chdir "${VE_NODE_ROOT_DIR}" "${NPM_EXECUTABLE}" install
    COMMAND "${CMAKE_COMMAND}" -E chdir "${VE_WEB_DIR}" "${NPM_EXECUTABLE}" install
    COMMAND "${CMAKE_COMMAND}" -E chdir "${VE_WEB_DIR}" "${NPM_EXECUTABLE}" run build:recovery

    DEPENDS ${VE_WEB_SOURCES}
    COMMENT "Building Vite UI (recovery)"
    VERBATIM
  )

  add_custom_target(ve_web ALL DEPENDS "${ve_recovery_html}")
endif()

# Keep project() after ve_web so the main component can depend on the generated
# build/static/recovery/index.html before ESP-IDF creates the embed rule.
project(vigilant-engine-recovery)
