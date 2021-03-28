cmake_minimum_required(VERSION 3.1...3.16)
project(MObs LANGUAGES CXX)
project(MObs VERSION 0.1.3)

if(NOT DEFINED CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 11)
endif()



add_executable(mrpcsrv mrpcsrv.cpp mrpc.h filestore.cpp filestore.h)
target_link_libraries(mrpcsrv ${MOBS_LIBRARIES})

add_executable(mrpcclient mrpcclient.cpp mrpc.h)
target_link_libraries(mrpcclient ${MOBS_LIBRARIES})






#get_cmake_property(_variableNames VARIABLES)
#list (SORT _variableNames)
#foreach (_variableName ${_variableNames})
#    message(STATUS "${_variableName}=${${_variableName}}")
#endforeach()