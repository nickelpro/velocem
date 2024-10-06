target_sources(velocem PRIVATE
  HTTPParser.cpp
  ModVelocem.cpp

  PRIVATE FILE_SET HEADERS
  BASE_DIRS ${CMAKE_CURRENT_LIST_DIR}

  FILES
  HTTPParser.hpp
  Router.hpp

  plat/plat.hpp

  util/Constants.hpp
  util/Util.hpp

  wsgi/App.hpp
  wsgi/Input.hpp
  wsgi/Request.hpp
  wsgi/Server.hpp
)

add_subdirectory(plat)
add_subdirectory(util)
add_subdirectory(wsgi)
