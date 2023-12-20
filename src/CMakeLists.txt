target_sources(velocem PRIVATE ModVelocem.c Server.c constants.c util.cpp)
target_sources(velocem PRIVATE FILE_SET HEADERS FILES util.h)

add_subdirectory(GILBalm)
