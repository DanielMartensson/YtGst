cmake_minimum_required(VERSION 3.16)

project(Ytgst VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Quick Network Widgets)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET gstreamer-1.0 gstreamer-gl-1.0)

# --- Konfigurerbara kompileringsflaggor ---------------------------------
# Absolut sökväg till yt-dlp. Lämnas tom för automatisk upptäckt (PATH).
set(YTGST_YTDLP_PATH "" CACHE FILEPATH "Sökväg till yt-dlp (tom = auto/PATH)")
if(YTGST_YTDLP_PATH STREQUAL "")
    find_program(_ytgst_ytdlp NAMES yt-dlp)
    if(_ytgst_ytdlp)
        set(YTGST_YTDLP_PATH "${_ytgst_ytdlp}")
    endif()
endif()
message(STATUS "yt-dlp: ${YTGST_YTDLP_PATH}")

# Föredragen GStreamer-videodekoder. Prioriteras framför övriga dekodrar.
# Exempel: vah264dec (Intel VA-API), vah265dec, nvdec_h264, avdec_h264.
set(YTGST_VIDEO_DECODER "vah264dec" CACHE STRING "GStreamer-videodekoder som prioriteras")
message(STATUS "Video decoder: ${YTGST_VIDEO_DECODER}")

# VA-API-drivrutin som sätts i LIBVA_DRIVER_NAME. Tom = automatisk.
# Exempel: i965 (Haswell/Broadwell), iHD (nyare Intel), radeonsi, nvidia.
set(YTGST_VAAPI_DRIVER "" CACHE STRING "LIBVA_DRIVER_NAME (t.ex. i965 eller iHD)")

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
    src/ytdlp.h
)

target_link_libraries(ytgst PRIVATE Qt6::Quick Qt6::Network Qt6::Widgets PkgConfig::GST)

# Skicka in de konfigurerbara värdena som kompileringsflaggor (-D...).
target_compile_definitions(ytgst PRIVATE
    YTGST_YTDLP_PATH="${YTGST_YTDLP_PATH}"
    YTGST_VIDEO_DECODER="${YTGST_VIDEO_DECODER}"
    YTGST_VAAPI_DRIVER="${YTGST_VAAPI_DRIVER}"
)

target_include_directories(ytgst PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

qt_finalize_executable(ytgst)

install(TARGETS ytgst BUNDLE DESTINATION .)