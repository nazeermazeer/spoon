cmake_minimum_required(VERSION 3.15)
project(MyFFmpegProject CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Keep the compiler database in the build directory so editors and clangd use
# the same include paths as the actual build.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 1. Locate the FFmpeg package via the vcpkg toolchain
find_package(FFMPEG REQUIRED)

add_executable(main src/main.cpp)

# FFmpeg's headers require this macro when included from C++.
target_compile_definitions(main PRIVATE __STDC_CONSTANT_MACROS)

# 2. Tell CMake where to find FFmpeg's header files (.h)
target_include_directories(main PRIVATE ${FFMPEG_INCLUDE_DIRS})

# 3. Tell CMake where to look for the compiled library binaries (.dylib)
target_link_directories(main PRIVATE ${FFMPEG_LIBRARY_DIRS})

# 4. Link the necessary FFmpeg libraries to your executable
target_link_libraries(main PRIVATE ${FFMPEG_LIBRARIES})
