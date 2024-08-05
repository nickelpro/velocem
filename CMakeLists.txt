cmake_minimum_required(VERSION 3.25)

include(cmake/BootstrapVcpkg.cmake)

project(velocem LANGUAGES C CXX VERSION 0.0.10)

option(VELOCEM_USE_IO_URING "Use io_uring on Linux" ON)
option(VELOCEM_USE_IPO "Use interprocedural optimization if supported" ON)
set(VELOCEM_WIN_SDK_VER 0x0A00 CACHE STRING "Windows SDK macro version")

find_package(Python3 3.12 REQUIRED COMPONENTS Development.Module)
find_package(asio CONFIG REQUIRED)
find_package(llhttp CONFIG REQUIRED)

Python3_add_library(velocem MODULE)
target_link_libraries(velocem PRIVATE asio::asio llhttp::llhttp_static Python3::Module)
target_compile_features(velocem PRIVATE cxx_std_23)
set_target_properties(velocem PROPERTIES CXX_VISIBILITY_PRESET hidden)

if(VELOCEM_USE_IPO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT ipo OUTPUT ipo_out)

  if(ipo)
    set_property(TARGET velocem PROPERTY INTERPROCEDURAL_OPTIMIZATION ON)
  else()
    message(WARNING "IPO is not supported: ${ipo_out}")
  endif()
endif()

if(WIN32)
  target_compile_definitions(velocem PRIVATE _WIN32_WINDOWS=${VELOCEM_WIN_SDK_VER})
endif()

if(LINUX AND VELOCEM_USE_IO_URING)
  target_compile_definitions(velocem PRIVATE
    ASIO_DISABLE_EPOLL
    ASIO_HAS_IO_URING
  )

  find_package(PkgConfig)
  pkg_check_modules(liburing REQUIRED IMPORTED_TARGET liburing>=2.0)
  target_link_libraries(velocem PRIVATE PkgConfig::liburing)
endif()

if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL GNU)
  target_compile_options(velocem PRIVATE
    -Wall -Wextra -Wno-missing-field-initializers -Wno-cast-function-type
  )
elseif(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL MSVC)
  target_compile_options(velocem PRIVATE /W3)
endif()

add_subdirectory(src)

install(
  TARGETS velocem
  LIBRARY DESTINATION ${PY_BUILD_CMAKE_PACKAGE_NAME}-${PY_BUILD_CMAKE_PACKAGE_VERSION}.data/platlib
)
