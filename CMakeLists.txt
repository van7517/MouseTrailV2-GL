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

add_executable(MouseTrailV2 WIN32
  src/main.cpp
  src/config.cpp
  src/trail.cpp
  src/platform_win.cpp
  src/tray_ui.cpp
  app.rc
)

target_include_directories(MouseTrailV2 PRIVATE src ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(MouseTrailV2 PRIVATE glfw opengl32 user32 gdi32 shell32 ole32 comctl32)

if(MSVC)
  set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} /I\"${CMAKE_SOURCE_DIR}/src\" /I\"${CMAKE_SOURCE_DIR}\"")
  target_compile_options(MouseTrailV2 PRIVATE /W3 /utf-8)
  target_compile_definitions(MouseTrailV2 PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
  set_target_properties(MouseTrailV2 PROPERTIES WIN32_EXECUTABLE TRUE)
else()
  target_compile_options(MouseTrailV2 PRIVATE -Wall -Wextra)
  target_compile_definitions(MouseTrailV2 PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
  set_target_properties(MouseTrailV2 PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
