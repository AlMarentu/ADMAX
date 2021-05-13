cmake_minimum_required(VERSION 3.1.0)
project(ADMAX LANGUAGES CXX)
project(ADMAX VERSION 0.0.1)

# alternative OpenSSL
if(APPLE)
    # hombrew Version
    set (CMAKE_PREFIX_PATH /usr/local/opt/openssl)
endif()
message(STATUS "CMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}")

# Options for MObs
option(BUILD_SHARED_LIBS "Build shared lib" ON)
option(BUILD_MONGO_INTERFACE "Build the Mongo database modules" OFF)
option(BUILD_MARIA_INTERFACE "Build the Maria database modules" OFF)
option(BUILD_SQLITE_INTERFACE "Build the SQLite database modules" ON)
option(BUILD_INFORMIX_INTERFACE "Build the Informix database modules" OFF)

if(NOT DEFINED CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 11)
endif()


include_directories(extern/MObs)
#find_library(MOBS_LIBRARIES mobs PATHS extern/mobs/MObs/cmake-build-debug/mobs NO_DEFAULT_PATH)
set(MOBS_LIBRARIES mobs)
add_subdirectory(extern/MObs/mobs)

add_subdirectory(Server)

add_subdirectory(DocMngr)
