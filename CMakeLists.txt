cmake_minimum_required(VERSION 3.24)

if(NOT SKIP_VCPKG AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  include(FetchContent)
  FetchContent_Declare(
    vcpkg
    GIT_REPOSITORY https://github.com/microsoft/vcpkg.git
    GIT_TAG master
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(vcpkg)
  set(CMAKE_TOOLCHAIN_FILE
    ${vcpkg_SOURCE_DIR}/scripts/buildsystems/vcpkg.cmake
    CACHE FILEPATH "Vcpkg toolchain file"
  )
  set(VCPKG_ROOT_DIR ${vcpkg_SOURCE_DIR} CACHE PATH "Vcpkg Root Directory")
endif()

if(DEFINED VCPKG_ROOT_DIR)
  add_custom_target(UpdateVcpkgBaseline
    ${VCPKG_ROOT_DIR}/vcpkg x-update-baseline
  )
endif()

project(velocem LANGUAGES C VERSION 0.0.3)

find_package(Python3 3.12 REQUIRED COMPONENTS Development.Module)
find_package(libuv CONFIG REQUIRED)
find_package(llhttp CONFIG REQUIRED)

Python3_add_library(velocem MODULE)
target_link_libraries(velocem PRIVATE libuv::uv_a llhttp::llhttp_static Python3::Module)

if(
  (CMAKE_C_COMPILER_ID STREQUAL "Clang") AND
  (CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
)
  target_compile_options(velocem PRIVATE "/clang:-std=gnu2x")
else()
  target_compile_features(velocem PRIVATE c_std_23)
endif()

add_subdirectory(src)

install(
  TARGETS velocem
  LIBRARY DESTINATION ${PY_BUILD_CMAKE_PACKAGE_NAME}-${PY_BUILD_CMAKE_PACKAGE_VERSION}.data/platlib
)
