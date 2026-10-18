cmake_minimum_required(VERSION 3.16)

project(Ytgst VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# WaylandCompositor: the video sink is drawn inside the player window through a
# Wayland compositor of our own (see PlayerWindow.qml), so waylandsink can be
# used for the picture without opening a window of its own.
find_package(Qt6 6.8 REQUIRED COMPONENTS Quick Network Widgets WaylandCompositor)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET gstreamer-1.0 gstreamer-gl-1.0)

# --- Configurable compile flags -----------------------------------------
# Absolute path to yt-dlp. Leave empty for automatic detection (PATH).
set(YTGST_YTDLP_PATH "" CACHE FILEPATH "Path to yt-dlp (empty = auto/PATH)")
if(YTGST_YTDLP_PATH STREQUAL "")
    find_program(_ytgst_ytdlp NAMES yt-dlp)
    if(_ytgst_ytdlp)
        set(YTGST_YTDLP_PATH "${_ytgst_ytdlp}")
    endif()
endif()
message(STATUS "yt-dlp: ${YTGST_YTDLP_PATH}")

# Preferred GStreamer video decoder. It is named explicitly in the video chain
# (see buildPipeline() in src/player.cpp) and its rank is raised, so a build for
# hardware without it does not fall back to the wrong decoder.
# Examples: v4l2slh264dec (V4L2 stateless, STM32MP25), vah264dec (Intel VA-API),
#           nvdec_h264, avdec_h264 (software).
set(YTGST_VIDEO_DECODER "vah264dec" CACHE STRING "Preferred GStreamer video decoder")
message(STATUS "Video decoder: ${YTGST_VIDEO_DECODER}")

# Preferred GStreamer audio decoder. The audio stream goes through a playbin,
# which picks a decoder itself, so this only raises the rank of the named one
# (see main.cpp). Empty = leave GStreamer's own choice alone.
# Examples: avdec_aac, faad, aac_hw_decoder.
set(YTGST_AUDIO_DECODER "" CACHE STRING "Preferred GStreamer audio decoder (empty = auto)")
message(STATUS "Audio decoder: ${YTGST_AUDIO_DECODER}")

# Audio sink the audio playbin should use. Empty = pick the first of
# pulsesink/pipewiresink/autoaudiosink/alsasink that exists.
# Examples: pulsesink, alsasink, pipewiresink.
set(YTGST_AUDIO_SINK "" CACHE STRING "GStreamer audio sink (empty = auto)")
message(STATUS "Audio sink: ${YTGST_AUDIO_SINK}")

# Video sink chain. Empty = the embedded qml6glsink chain that draws into the
# QML scene graph (glupload ! glcolorconvert ! capsfilter ! qml6glsink).
# Example: waylandsink, to draw through a window of its own.
set(YTGST_VIDEO_SINK "" CACHE STRING "GStreamer video sink chain (empty = qml6glsink)")
message(STATUS "Video sink: ${YTGST_VIDEO_SINK}")

# VA-API driver set in LIBVA_DRIVER_NAME. Empty = automatic.
# Examples: i965 (Haswell/Broadwell), iHD (newer Intel), radeonsi, nvidia.
set(YTGST_VAAPI_DRIVER "" CACHE STRING "LIBVA_DRIVER_NAME (e.g. i965 or iHD)")

qt_standard_project_setup(REQUIRES 6.8)

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

target_link_libraries(ytgst PRIVATE Qt6::Quick Qt6::Network Qt6::Widgets
    Qt6::WaylandCompositor PkgConfig::GST)

# Pass the configurable values in as compile flags (-D...).
target_compile_definitions(ytgst PRIVATE
    YTGST_YTDLP_PATH="${YTGST_YTDLP_PATH}"
    YTGST_VIDEO_DECODER="${YTGST_VIDEO_DECODER}"
    YTGST_AUDIO_DECODER="${YTGST_AUDIO_DECODER}"
    YTGST_AUDIO_SINK="${YTGST_AUDIO_SINK}"
    YTGST_VIDEO_SINK="${YTGST_VIDEO_SINK}"
    YTGST_VAAPI_DRIVER="${YTGST_VAAPI_DRIVER}"
)

target_include_directories(ytgst PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

qt_finalize_executable(ytgst)

install(TARGETS ytgst BUNDLE DESTINATION .)