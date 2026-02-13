cmake_minimum_required(VERSION 3.16)

#Treats warnings as errors
add_compile_options("-Werror")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)
project(vigilant-engine)

# ---- Web UI (Vite) build integration ----
option(VE_BUILD_WEB "Build Vite UI before compiling firmware" ON)

if(VE_BUILD_WEB)
  find_program(NPM_EXECUTABLE npm)
  if(NOT NPM_EXECUTABLE)
    message(FATAL_ERROR "npm not found. Install Node.js/npm or set VE_BUILD_WEB=OFF.")
  endif()

  # Adjust this to where your Vite project lives inside the ESP-IDF repo
  set(VE_WEB_DIR "${CMAKE_SOURCE_DIR}/vigilant-engine-frontend")

  # The two output HTML files you want Vite to emit
  set(VE_OUT_VIGILANT "${CMAKE_SOURCE_DIR}/main/static/vigilant.html")
  set(VE_OUT_RECOVERY "${CMAKE_SOURCE_DIR}/vigilant-engine-recovery/main/static/index.html")

  # A stamp file to make the build incremental
  set(VE_WEB_STAMP "${CMAKE_BINARY_DIR}/ve_web_build.stamp")

  # Track source changes (CONFIGURE_DEPENDS helps when files are added/removed)
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

    # If you prefer npm ci, swap it in (ci requires a lockfile)
    COMMAND "${NPM_EXECUTABLE}" install
    COMMAND "${NPM_EXECUTABLE}" run build:vigilant
    COMMAND "${NPM_EXECUTABLE}" run build:recovery

    # Mark successful build
    COMMAND "${CMAKE_COMMAND}" -E touch "${VE_WEB_STAMP}"

    DEPENDS ${VE_WEB_SOURCES}
    BYPRODUCTS "${VE_OUT_VIGILANT}" "${VE_OUT_RECOVERY}"
    COMMENT "Building Vite UI (vigilant + recovery) 🧩"
    VERBATIM
  )

  add_custom_target(ve_web ALL DEPENDS "${VE_WEB_STAMP}")
endif()
