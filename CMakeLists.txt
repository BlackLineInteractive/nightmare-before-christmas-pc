cmake_minimum_required(VERSION 3.15)
project(OogieEngine LANGUAGES C CXX)

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
    include
    ${RAYLIB_INCLUDE_DIRS}
    external/imgui
    external/rlImGui
)

set(IMGUI_SOURCES
    external/imgui/imgui.cpp
    external/imgui/imgui_draw.cpp
    external/imgui/imgui_widgets.cpp
    external/imgui/imgui_tables.cpp
    external/rlImGui/rlImGui.cpp
)

set(ENGINE_SOURCES
    src/Core/VFS.cpp
    src/Loader/CapcomLZ.cpp
    src/Loader/PodLoader.cpp
    src/Loader/SceneLoader.cpp
    src/Rendering/GPUMesh.cpp
    src/Rendering/GPUTexture.cpp
    src/Rendering/Camera.cpp
    src/SG/SceneNode.cpp
    src/SG/StageScene.cpp
    src/UI/UITheme.cpp
    src/UI/Panels/StageBrowserPanel.cpp
    src/UI/Panels/ModelInspectorPanel.cpp
    src/UI/Panels/TextureGalleryPanel.cpp
    src/UI/Panels/DialoguePanel.cpp
    src/Viewer/ViewerApp.cpp
    src/Host/XboxKernelThunks.cpp
    src/Host/OogieHost.cpp
    src/main.cpp
)

add_executable(oogie_viewer
    ${ENGINE_SOURCES}
    ${IMGUI_SOURCES}
)

add_executable(oogie_port
    src/Core/VFS.cpp
    src/Loader/CapcomLZ.cpp
    src/Host/XboxKernelThunks.cpp
    src/Host/OogieHost.cpp
    src/Host/main_host.cpp
)

target_link_libraries(oogie_port PRIVATE
    "-framework OpenGL"
)

target_link_libraries(oogie_viewer PRIVATE
    ${RAYLIB_LIBRARIES}
    "-framework OpenGL"
    "-framework Cocoa"
    "-framework IOKit"
    "-framework CoreVideo"
)
