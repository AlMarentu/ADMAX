
set (CMAKE_CXX_STANDARD 11)

if (CMAKE_HOST_WIN32)
    set (CMAKE_PREFIX_PATH "C:/Qt/5.15.2/mingw81_64")
elseif(CMAKE_HOST_APPLE)
    set (CMAKE_PREFIX_PATH "/usr/local/opt/qt5")
endif()

set (CMAKE_AUTOMOC ON)
set (CMAKE_AUTORCC ON)
set (CMAKE_AUTOUIC ON)

set (QT_VERSION 5)
set (REQUIRED_LIBS Core Gui Widgets Network)
set (REQUIRED_LIBS_QUALIFIED Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Network)


include_directories(${CMAKE_SOURCE_DIR}/Server)
include_directories(${CMAKE_SOURCE_DIR}/DocMngr)

find_library(POPPLER_LIB poppler-qt${QT_VERSION} REQUIRED)
find_path(POPPLER_PATH poppler REQUIRED)
include_directories(${POPPLER_PATH}/poppler/qt${QT_VERSION})
message(STATUS "Poppler=${POPPLER_PATH}/poppler/qt${QT_VERSION} - ${POPPLER_LIB}")

add_executable (DocMngr main.cpp mainwindow.cpp mainwindow.h mrpccli.cpp mrpccli.h viewer.cpp viewer.h)

find_package (Qt${QT_VERSION} COMPONENTS ${REQUIRED_LIBS} REQUIRED)

target_link_libraries (DocMngr  ${REQUIRED_LIBS_QUALIFIED} ${MOBS_LIBRARIES} ${POPPLER_LIB} )
