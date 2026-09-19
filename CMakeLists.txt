cmake_minimum_required(VERSION 3.16)

project(Ytgst VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Quick Network)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET gstreamer-1.0 gstreamer-gl-1.0)

qt_standard_project_setup(REQUIRES 6.2)

qt_add_executable(ytgst
    src/main.cpp
)

qt_add_qml_module(ytgst
    URI Ytgst
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/PlayerWindow.qml
        qml/SearchBar.qml
        qml/VideoCard.qml
        qml/Spinner.qml
    SOURCES
        src/youtube.cpp
        src/videomodel.cpp
        src/player.cpp
)

target_sources(ytgst PRIVATE
    src/youtube.h
    src/videomodel.h
    src/player.h
)

target_link_libraries(ytgst PRIVATE Qt6::Quick Qt6::Network PkgConfig::GST)

target_include_directories(ytgst PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

qt_finalize_executable(ytgst)

install(TARGETS ytgst BUNDLE DESTINATION .)