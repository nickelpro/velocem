target_sources(velocem PRIVATE
  HTTPParser.cpp
  ModVelocem.cpp
  Request.cpp
  Util.cpp
  WSGI.cpp
)

add_subdirectory(plat)
