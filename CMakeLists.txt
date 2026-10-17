cmake_minimum_required(VERSION 3.16)
project(TextInputHost LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(TextInputHost WIN32 main.cpp)

target_compile_definitions(TextInputHost PRIVATE
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(TextInputHost PRIVATE
    gdiplus
    ole32
    winhttp
    shell32
    dwmapi
    uxtheme
)

set_target_properties(TextInputHost PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    OUTPUT_NAME "TextInputHost"
)
