# midiplay: small SDL2 player for the exported GoldenEye music (MIDI + SoundFont 2), with hot reload.
# Stand-alone: cmake -S tools_pc/midiplay -B build-midiplay && cmake --build build-midiplay
cmake_minimum_required(VERSION 3.16)
project(midiplay C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

set(TSF_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../port/third_party/tinysoundfont")

# SDL2: a CMake package when installed that way, else the repository's FindSDL2.cmake
# (honours $SDL2DIR, which is how the MinGW cross image finds it).
find_package(SDL2 CONFIG QUIET)
if(NOT TARGET SDL2::SDL2)
  list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake")
  find_package(SDL2 REQUIRED)
endif()

# the game's own sequencer (pure C), so the loops play as in the game
add_executable(midiplay midiplay.c "${CMAKE_CURRENT_SOURCE_DIR}/../../port/src/musicseq.c")
target_include_directories(midiplay PRIVATE "${TSF_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../../port/include")
if(TARGET SDL2::SDL2)
  target_link_libraries(midiplay PRIVATE SDL2::SDL2)
else()
  target_include_directories(midiplay SYSTEM PRIVATE "${SDL2_INCLUDE_DIR}")
  target_link_libraries(midiplay PRIVATE ${SDL2_LIBRARY})
endif()
if(NOT WIN32)
  target_link_libraries(midiplay PRIVATE m)
endif()
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(midiplay PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
