target_sources(velocem PRIVATE
  HTTPParser.cpp
  ModVelocem.cpp
)

target_include_directories(velocem PRIVATE ${CMAKE_CURRENT_LIST_DIR})

add_subdirectory(plat)
add_subdirectory(util)
add_subdirectory(wsgi)
