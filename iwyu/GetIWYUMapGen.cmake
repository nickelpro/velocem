include(FetchContent)

FetchContent_Declare(
  pythonMapGen
  URL https://raw.githubusercontent.com/include-what-you-use/include-what-you-use/e4dd555000b824f11783d99b93d4a713fe34dd1d/mapgen/iwyu-mapgen-cpython.py
  URL_HASH SHA512=ef1e39cd9c735ca218d45e1f643cf06f73327446cc1d8ec8ced86503a0db727ae6b53ee879a979b214d9a5fa6a333daf5a6bf4ef821fd4a43ad44b34df3448bc
  DOWNLOAD_NO_EXTRACT TRUE
)
FetchContent_MakeAvailable(pythonMapGen)

execute_process(
  COMMAND ${Python3_EXECUTABLE} ${pythonmapgen_SOURCE_DIR}/iwyu-mapgen-cpython.py ${Python3_INCLUDE_DIRS}
  OUTPUT_FILE ${CMAKE_BINARY_DIR}/python_mappings.imp
  COMMAND_ECHO STDOUT
)

list(APPEND CMAKE_CXX_INCLUDE_WHAT_YOU_USE -Xiwyu --mapping_file=${CMAKE_BINARY_DIR}/python_mappings.imp)
