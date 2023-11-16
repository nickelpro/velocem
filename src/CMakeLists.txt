target_sources(velocem PRIVATE ModVelocem.c Server.c)

add_executable(test2 test2.c)
target_link_libraries(test2 PRIVATE llhttp::llhttp_static libuv::uv_a)

add_subdirectory(Platform)
