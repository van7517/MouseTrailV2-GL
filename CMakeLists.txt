cmake_minimum_required(VERSION 3.20)
project(MouseTrailV2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.4
)
FetchContent_MakeAvailable(glfw)

add_executable(MouseTrailV2
  src/main.cpp
  src/config.cpp
  src/trail.cpp
  src/platform_win.cpp
)

target_include_directories(MouseTrailV2 PRIVATE src)
target_link_libraries(MouseTrailV2 PRIVATE glfw opengl32 user32 gdi32 shell32 ole32)

if(MSVC)
  target_compile_options(MouseTrailV2 PRIVATE /W3 /utf-8)
  target_compile_definitions(MouseTrailV2 PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
else()
  target_compile_options(MouseTrailV2 PRIVATE -Wall -Wextra)
  target_compile_definitions(MouseTrailV2 PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# Copy default config next to binary after build
add_custom_command(TARGET MouseTrailV2 POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_SOURCE_DIR}/config.json
    $<TARGET_FILE_DIR:MouseTrailV2>/config.json
)
