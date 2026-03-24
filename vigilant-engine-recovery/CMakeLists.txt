# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)
project(vigilant-engine-recovery)

# ---- Web UI (Vite) build integration ----
option(VE_BUILD_WEB "Build Vite UI before compiling firmware" ON)

if(VE_BUILD_WEB)
  find_program(NPM_EXECUTABLE npm)
  if(NOT NPM_EXECUTABLE)
    message(FATAL_ERROR "npm not found. Install Node.js/npm or set VE_BUILD_WEB=OFF.")
  endif()

  get_filename_component(VE_REPO_ROOT "${CMAKE_SOURCE_DIR}/.." ABSOLUTE)
  set(VE_WEB_DIR "${VE_REPO_ROOT}/vigilant-engine-frontend")

  set(VE_OUT_RECOVERY "${CMAKE_SOURCE_DIR}/main/static/index.html")
  set(VE_WEB_STAMP "${CMAKE_BINARY_DIR}/ve_web_build.stamp")

  file(GLOB_RECURSE VE_WEB_SOURCES CONFIGURE_DEPENDS
    "${VE_WEB_DIR}/index.html"
    "${VE_WEB_DIR}/vigilant.html"
    "${VE_WEB_DIR}/vite.config.*"
    "${VE_WEB_DIR}/package.json"
    "${VE_WEB_DIR}/package-lock.json"
    "${VE_WEB_DIR}/pnpm-lock.yaml"
    "${VE_WEB_DIR}/yarn.lock"
    "${VE_WEB_DIR}/src/*"
  )

  add_custom_command(
    OUTPUT "${VE_WEB_STAMP}"
    WORKING_DIRECTORY "${VE_WEB_DIR}"

    COMMAND "${NPM_EXECUTABLE}" install
    COMMAND "${NPM_EXECUTABLE}" run build:recovery

    COMMAND "${CMAKE_COMMAND}" -E touch "${VE_WEB_STAMP}"

    DEPENDS ${VE_WEB_SOURCES}
    BYPRODUCTS "${VE_OUT_RECOVERY}"
    COMMENT "Building Vite UI (recovery)"
    VERBATIM
  )

  add_custom_target(ve_web ALL DEPENDS "${VE_WEB_STAMP}")
endif()
