if(NOT SKIP_VCPKG AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  include(FetchContent)

  if(WIN32)
    set(VCPKG vcpkg.exe)
  elseif(LINUX)
    if(EXISTS "/etc/alpine-release")
      set(VCPKG vcpkg-musl)
    else()
      set(VCPKG vcpkg-glibc)
    endif()
  elseif(APPLE)
    set(VCPKG vcpkg-macos)
  else()
    message(FATAL_ERROR "Cannot bootstrap vcpkg: Unsupported platform")
  endif()

  FetchContent_Declare(vcpkg
    URL https://github.com/microsoft/vcpkg-tool/releases/latest/download/${VCPKG}
    DOWNLOAD_NO_EXTRACT TRUE
  )

  FetchContent_MakeAvailable(vcpkg)
  set(VCPKG ${vcpkg_SOURCE_DIR}/${VCPKG})

  file(CHMOD ${VCPKG} PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
  set(ENV{VCPKG_ROOT} ${vcpkg_SOURCE_DIR})
  execute_process(COMMAND ${VCPKG} bootstrap-standalone)

  if(NOT WIN32)
    file(RENAME ${VCPKG} ${vcpkg_SOURCE_DIR}/vcpkg)
  endif()

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
