
set (CMAKE_CXX_STANDARD 11)
set (CMAKE_PREFIX_PATH "/usr/local/opt/qt5/lib/cmake")
set (CMAKE_AUTOMOC ON)
set (CMAKE_AUTORCC ON)
set (CMAKE_AUTOUIC ON)

set (QT_VERSION 5)
set (REQUIRED_LIBS Core Gui Widgets Network)
set (REQUIRED_LIBS_QUALIFIED Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Network)

#set(MOBS_LIBRARIES ${CMAKE_SOURCE_DIR}/../MObs/cmake-build-debug/mobs/libmobs.dylib)


include_directories(${CMAKE_SOURCE_DIR}/Server)
include_directories(${CMAKE_SOURCE_DIR}/DocMngr)
include_directories(/usr/local/include/poppler/qt5)
set (MOBS_LIBRARIES ${MOBS_LIBRARIES})

add_executable (DocMngr main.cpp mainwindow.cpp mainwindow.h mrpccli.cpp mrpccli.h viewer.cpp viewer.h)

find_package (Qt${QT_VERSION} COMPONENTS ${REQUIRED_LIBS} REQUIRED)
#find_library(Poppler-qt5 Poppler)

target_link_libraries (DocMngr  ${REQUIRED_LIBS_QUALIFIED} ${MOBS_LIBRARIES} /usr/local/lib/libpoppler-qt5.dylib )
