cmake_minimum_required(VERSION 3.25)

include(vcpkg/BootstrapVcpkg.cmake)

project(velocem LANGUAGES C CXX VERSION 0.0.12)

option(VELOCEM_USE_IO_URING "Use io_uring on Linux" ON)
option(VELOCEM_USE_IPO "Use interprocedural optimization if supported" ON)
option(VELOCEM_STRIP "Run system strip on compiled module" ON)
set(VELOCEM_WIN_SDK_VER 0x0A00 CACHE STRING "Windows SDK macro version")

find_package(absl CONFIG REQUIRED)
find_package(asio CONFIG REQUIRED)
find_package(llhttp CONFIG REQUIRED)
find_package(Python3 3.12 REQUIRED COMPONENTS Development.Module)

Python3_add_library(velocem MODULE)
target_link_libraries(velocem PRIVATE
  absl::flat_hash_map
  asio::asio
  llhttp::llhttp_static
  Python3::Module
)
target_compile_features(velocem PRIVATE cxx_std_23)
target_compile_definitions(velocem PRIVATE ASIO_DISABLE_VISIBILITY)
set_target_properties(velocem PROPERTIES CXX_VISIBILITY_PRESET hidden)

if(VELOCEM_USE_IPO AND NOT(CMAKE_BUILD_TYPE STREQUAL Debug))
  include(CheckIPOSupported)
  check_ipo_supported(RESULT ipo OUTPUT ipo_out)

  if(ipo)
    set_property(TARGET velocem PROPERTY INTERPROCEDURAL_OPTIMIZATION ON)

    if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
      target_compile_options(velocem PRIVATE /Gy /Gw)
      target_link_options(velocem PRIVATE /OPT:ICF=3)
    endif()

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

if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "GNU")
  target_compile_options(velocem PRIVATE
    -Wall -Wextra -Wno-missing-field-initializers -Wno-cast-function-type
  )

  if(NOT APPLE)
    target_link_options(velocem PRIVATE -Wl,--exclude-libs,ALL)
  endif()
elseif(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  target_compile_options(velocem PRIVATE /W3)
endif()

if(VELOCEM_STRIP AND DEFINED CMAKE_STRIP AND NOT(CMAKE_BUILD_TYPE STREQUAL Debug))
  if(APPLE)
    add_custom_command(TARGET velocem POST_BUILD
      COMMAND ${CMAKE_STRIP} -x $<TARGET_FILE:velocem>
    )
  else()
    add_custom_command(TARGET velocem POST_BUILD
      COMMAND ${CMAKE_STRIP} $<TARGET_FILE:velocem>
    )
  endif()
endif()

add_subdirectory(src)

install(
  TARGETS velocem
  LIBRARY DESTINATION ${PY_BUILD_CMAKE_PACKAGE_NAME}-${PY_BUILD_CMAKE_PACKAGE_VERSION}.data/platlib
)
