
set (CMAKE_CXX_STANDARD 11)

if (CMAKE_HOST_WIN32)
    set (CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} C:/poppler-21.04.0 )
endif()

set (CMAKE_AUTOMOC ON)
set (CMAKE_AUTORCC ON)
set (CMAKE_AUTOUIC ON)

set (QT_VERSION 5)
set (REQUIRED_LIBS Core Gui Widgets Network PrintSupport) # Pdf
set (REQUIRED_LIBS_QUALIFIED Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Network Qt5::PrintSupport ) # Qt5::Pdf


include_directories(${CMAKE_SOURCE_DIR}/Server)
include_directories(${CMAKE_SOURCE_DIR}/DocMngr)

find_library(POPPLER_LIB poppler-qt${QT_VERSION} REQUIRED)
find_path(POPPLER_PATH poppler REQUIRED)
set(POPPLER_INCLUDE ${POPPLER_PATH}/poppler/qt${QT_VERSION})
include_directories(${POPPLER_INCLUDE})
if (WIN32)
    set(REQUIRED_LIBS ${REQUIRED_LIBS} Xml)
    set(REQUIRED_LIBS_QUALIFIED ${REQUIRED_LIBS_QUALIFIED} Qt5::Xml)
    find_library(POPPLERMAIN_LIB poppler REQUIRED)
    find_library(FREETYPE_LIB freetype REQUIRED)
    find_library(JPEG_LIB jpeg REQUIRED)
    find_library(TIFF_LIB tiff REQUIRED)
    find_library(PNG_LIB png REQUIRED)
    find_library(Z_LIB zlib REQUIRED)
    set(POPPLER_LIB  ${POPPLER_LIB} ${POPPLERMAIN_LIB} ${FREETYPE_LIB} ${JPEG_LIB} ${TIFF_LIB} ${PNG_LIB} ${Z_LIB} )
# jpeg freetype png tiff zlib
endif()
message(STATUS "Poppler=${POPPLER_INCLUDE} - ${POPPLER_LIB}")

add_executable (DocMngr main.cpp mainwindow.cpp mainwindow.h mrpccli.cpp mrpccli.h viewer.cpp viewer.h)

find_package (Qt${QT_VERSION} COMPONENTS ${REQUIRED_LIBS} REQUIRED)

target_compile_definitions(DocMngr PUBLIC USE_POPPLER)

target_link_libraries (DocMngr  ${MOBS_LIBRARIES} ${POPPLER_LIB} ${REQUIRED_LIBS_QUALIFIED} )
