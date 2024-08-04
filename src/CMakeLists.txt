target_sources(velocem PRIVATE
  HTTPParser.cpp
  ModVelocem.cpp
  Request.cpp
  Util.cpp
  WSGIApp.cpp
  WSGIServer.cpp
)

add_subdirectory(plat)
