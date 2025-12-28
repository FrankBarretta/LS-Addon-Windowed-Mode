cmake_minimum_required(VERSION 3.15)
project(LS_Windowed)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(minhook)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        docking
)
FetchContent_MakeAvailable(imgui)

# Source files
set(SOURCES
    main.cpp
    dxgi_proxy.cpp
    logger.cpp
)

# Create the DLL
add_library(LS_Windowed SHARED ${SOURCES})

# Include MinHook headers
target_include_directories(LS_Windowed PRIVATE 
    ${MINHOOK_DIR}/include
    ${imgui_SOURCE_DIR}
    ../../../LosslessProxy/src
)

# Add ImGui source files
target_sources(LS_Windowed PRIVATE
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
)

# Link DirectX libraries and MinHook
target_link_libraries(LS_Windowed
    d3d11.lib
    dxgi.lib
    minhook
)

# Set output directory
set_target_properties(LS_Windowed PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/../LS_Windowed"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/../LS_Windowed"
)

# Copy to addons directory after build
add_custom_command(TARGET LS_Windowed POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/../../LS_Windowed"
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:LS_Windowed> "${CMAKE_SOURCE_DIR}/../../LS_Windowed/"
    COMMENT "Copying LS_Windowed.dll to addons directory"
)
