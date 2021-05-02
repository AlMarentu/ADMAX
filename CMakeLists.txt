cmake_minimum_required(VERSION 3.1...3.16)
project(ADMAX LANGUAGES CXX)
project(ADMAX VERSION 0.0.1)

if(NOT DEFINED CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 11)
endif()

include_directories(${CMAKE_SOURCE_DIR}/../MObs)
find_library(MOBS_LIBRARIES mobs PATHS ${CMAKE_SOURCE_DIR}/../MObs/cmake-build-debug/mobs NO_DEFAULT_PATH)
#set(MOBS_LIBRARIES ${CMAKE_SOURCE_DIR}/../MObs/cmake-build-debug/mobs/libmobs.dylib)

add_subdirectory(Server)

add_subdirectory(DocMngr)
