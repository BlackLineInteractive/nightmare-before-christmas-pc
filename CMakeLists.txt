cmake_minimum_required(VERSION 3.15)
project(OogieViewer LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig QUIET)
if (PKG_CONFIG_FOUND)
    pkg_check_modules(RAYLIB raylib)
endif()

if (NOT RAYLIB_FOUND)
    set(RAYLIB_INCLUDE_DIRS "/usr/local/include")
    set(RAYLIB_LIBRARIES "/usr/local/lib/libraylib.dylib")
endif()

include_directories(
    ${RAYLIB_INCLUDE_DIRS}
    external/imgui
    external/rlImGui
    src
)

set(IMGUI_SOURCES
    external/imgui/imgui.cpp
    external/imgui/imgui_draw.cpp
    external/imgui/imgui_widgets.cpp
    external/imgui/imgui_tables.cpp
    external/rlImGui/rlImGui.cpp
)

set(APP_SOURCES
    src/main.cpp
    src/PodModel.cpp
    src/TextureManager.cpp
    src/StageScene.cpp
    ${IMGUI_SOURCES}
)

add_executable(oogie_viewer ${APP_SOURCES})

target_link_libraries(oogie_viewer PRIVATE
    ${RAYLIB_LIBRARIES}
    "-framework OpenGL"
    "-framework Cocoa"
    "-framework IOKit"
    "-framework CoreVideo"
)

