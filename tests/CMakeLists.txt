# CTest binaries. One executable per area (each its own process, so the
# game globals start clean), all linking the game core objects (ge007_core)
# plus the same libraries as the shipping executable. The global linker flags
# (no-PIE, -Ttext-segment, ...) are CMAKE_EXE_LINKER_FLAGS and apply here too.
# Not configured on the Switch (see the top-level CMakeLists.txt).

# GE_CORE_LIBS: LIBS plus (Vulkan builds) volk and the SPIR-V pack.
function(ge_add_test name)
  add_executable(${name} ${ARGN} platform/null_host.c $<TARGET_OBJECTS:ge007_core>)
  target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                                             "${CMAKE_CURRENT_SOURCE_DIR}/platform"
                                             "${CMAKE_SOURCE_DIR}/port/src"
                                             "${CMAKE_SOURCE_DIR}/port/ge_render/include")
  target_link_libraries(${name} ${GE_CORE_LIBS})
  # The tests have their own int main(void): keep SDL from renaming it to
  # SDL_main on Windows.
  target_compile_definitions(${name} PRIVATE SDL_MAIN_HANDLED)
  set_target_properties(${name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
  add_test(NAME ${name} COMMAND ${name})
  # No window, no audio device: SDL's dummy drivers (also set by tests/platform).
  set_tests_properties(${name} PROPERTIES
    ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy"
    TIMEOUT 60)
endfunction()

# A PURE test: links only the sources it is given (+ libm), never the core.
# For the code that is pure by design (port/src/input/*.c: no SDL, no game
# globals), so it needs no platform, no ROM, no game state.
function(ge_add_pure_test name)
  add_executable(${name} ${ARGN})
  target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                                             "${CMAKE_SOURCE_DIR}/port/src")
  target_link_libraries(${name} m)
  set_target_properties(${name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES TIMEOUT 60)
endfunction()

# The platform the tests run on: a null graphics backend, a fake joystick feed.
set(GE_TEST_PLATFORM platform/null_platform.c platform/null_gfx.c)

ge_add_test(test_core_link   test_core_link.c)
ge_add_test(test_platform    test_platform.c ${GE_TEST_PLATFORM})
ge_add_test(test_updatecheck test_updatecheck.c)
ge_add_test(test_defaults    test_defaults.c ${GE_TEST_PLATFORM})
ge_add_test(test_sounds      test_sounds.c ${GE_TEST_PLATFORM})

# The first-launch GPU benchmark's decision (port/src/gpuprobe.c): pure, no core.
ge_add_pure_test(test_gpuprobe test_gpuprobe.c ${CMAKE_SOURCE_DIR}/port/src/gpuprobe.c)
# The labels reader (port/src/labels.c): the asset stack is stubbed in the test.
ge_add_pure_test(test_labels test_labels.c ${CMAKE_SOURCE_DIR}/port/src/labels.c)
target_compile_definitions(test_labels PRIVATE LABELS_PURE_TEST)
target_include_directories(test_labels PRIVATE "${CMAKE_SOURCE_DIR}/port/include" "${CMAKE_SOURCE_DIR}/port/assets")
# The level decor's glTF reader (port/src/leveldecor.c): pure, a fixture glTF written by the test.
ge_add_pure_test(test_leveldecor test_leveldecor.c ${CMAKE_SOURCE_DIR}/port/src/leveldecor.c ${CMAKE_SOURCE_DIR}/port/third_party/cjson/cJSON.c)
target_compile_definitions(test_leveldecor PRIVATE LEVELDECOR_PURE_TEST)
set_property(TARGET test_leveldecor PROPERTY INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/port/include" "${CMAKE_SOURCE_DIR}/port/third_party/cgltf" "${CMAKE_SOURCE_DIR}/port/third_party/cjson")
set_property(TARGET test_leveldecor PROPERTY COMPILE_OPTIONS "")   # plain host C: no shadow headers, no forced versioninfo.h (as ge_leveldecor)
# CONSOLE mode precedence (port/include/console.h, header-only): pure, no core.
ge_add_pure_test(test_console test_console.c)

# The port's texture registry (port/src/porttex.c): pure, no core.
ge_add_pure_test(test_porttex test_porttex.c ${CMAKE_SOURCE_DIR}/port/src/porttex.c)

# The game event bus (port/src/portevents.c): pure, no core.
ge_add_pure_test(test_portevents test_portevents.c ${CMAKE_SOURCE_DIR}/port/src/portevents.c)

# Positioned dialogue markers (port/src/saymarker_math.c, #132): pure, no core.
ge_add_pure_test(test_saymarker test_saymarker.c ${CMAKE_SOURCE_DIR}/port/src/saymarker_math.c)

# Character-model subdivision (port/convert/conv_subdiv.c): pure, no core.
ge_add_pure_test(test_subdiv test_subdiv.c ${CMAKE_SOURCE_DIR}/port/convert/conv_subdiv.c)

# Static level export, textures + PNG (port/export/, #150). Built in port/export/'s freestanding
# scope (CMakeLists.txt there); registered here. The PNG self-test always runs; the whole texture
# bank is decoded only when GE_TEST_ROM points at a retail .z64 (skipped otherwise).
add_test(NAME test_export_tex COMMAND test_export_tex)
set_tests_properties(test_export_tex PROPERTIES TIMEOUT 60)

# The fonts reader (port/src/fonts.c, docs/specs/fonts.md): pure, no core (the test includes the file with FONTS_PURE).
ge_add_pure_test(test_fonts test_fonts.c)
target_include_directories(test_fonts PRIVATE "${CMAKE_SOURCE_DIR}/port/include")

# The retail ROM check (port/convert/conv_romcheck.c): pure, no core.
ge_add_pure_test(test_romcheck test_romcheck.c ${CMAKE_SOURCE_DIR}/port/convert/conv_romcheck.c)

# Input strategies (port/src/input/): pure, no core.
set(GE_INPUT_DIR "${CMAKE_SOURCE_DIR}/port/src/input")
ge_add_pure_test(test_input_modern test_input_modern.c
  ${GE_INPUT_DIR}/input_common.c ${GE_INPUT_DIR}/style_modern.c ${GE_INPUT_DIR}/n64bridge.c)

# The shared mission formats (port/mission, port/levelrec): pure, no core.
ge_add_pure_test(test_mission_format test_mission_format.c)
target_link_libraries(test_mission_format ge_mission)
set_property(TARGET test_mission_format PROPERTY INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_SOURCE_DIR}/port/mission" "${CMAKE_SOURCE_DIR}/port/levelrec")
set_property(TARGET test_mission_format PROPERTY COMPILE_OPTIONS "")

# level.rec (port/levelrec): the record layout, plus a structural check of a real
# export when GE_LEVELREC_DIR names one (skipped otherwise). Header only: no core.
ge_add_pure_test(test_levelrec_file test_levelrec_file.c)
set_property(TARGET test_levelrec_file PROPERTY INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_SOURCE_DIR}/port/levelrec")
set_property(TARGET test_levelrec_file PROPERTY COMPILE_OPTIONS "")

if(GE007_ROM_TESTS)
  # Tests that read the ROM: local opt-in only (the ROM is never in the repo).
  # Add them here with ge_add_test() and make them skip unless GE007_ROM names
  # the .z64; none exist yet.
  message(STATUS "GE007_ROM_TESTS=ON: ROM-backed tests enabled (set GE007_ROM=<.z64>)")
endif()

# Mission setup rebuild (port/mission/mission_apply.c) the Luau runtime (mission_lua.cpp) and the thumb.png pixel conversion (mission_thumb.c): pure, no core.
foreach(_t test_mission_apply test_mission_lua test_mission_thumb test_mission_formula test_mission_pkg test_mission_events test_texpacks_json test_ge_png test_ge_tex test_assetstack
           test_mission_api_guard test_mission_api_world test_mission_api_cinema)
  ge_add_pure_test(${_t} ${_t}.c)
  target_link_libraries(${_t} ge_mission)
  set_property(TARGET ${_t} PROPERTY INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/port/mission" "${CMAKE_SOURCE_DIR}/port/levelrec")
  set_property(TARGET ${_t} PROPERTY COMPILE_OPTIONS "")
endforeach()
