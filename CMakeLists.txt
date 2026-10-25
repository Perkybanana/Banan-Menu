cmake_minimum_required(VERSION 3.25)
project(BananMenu VERSION 1.0.0 LANGUAGES C CXX ASM_MASM)
set(BANAN_VERSION "1.0.0-dev.4")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT: friends need no VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# /EHa: the overlay catches exceptions with __try (Guarded); C++ destructors, and so mutex unlocks, must still run.
string(REPLACE "/EHsc" "/EHa" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

include(FetchContent)
# Pinned archives + hashes. SOURCE_SUBDIR points nowhere so we only download and build the files we pick below.
FetchContent_Declare(imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.92.9.tar.gz
    URL_HASH SHA256=af97ed649182c39314320514a672b82008ab462b9293fe23d37b30bfa5d05519
    SOURCE_SUBDIR none)
FetchContent_Declare(minhook
    URL https://github.com/TsudaKageyu/minhook/archive/refs/tags/v1.3.4.tar.gz
    URL_HASH SHA256=1aebeae4ca898330c507860acc2fca2eb335fe446a3a2b8444c3bf8b2660a14e
    SOURCE_SUBDIR none)
FetchContent_MakeAvailable(imgui minhook)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
# ImGui's default IME handler talks to the game window from the render thread: we don't need IME.
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_WIN32_DEFAULT_IME_FUNCTIONS)

add_library(minhook STATIC
    ${minhook_SOURCE_DIR}/src/buffer.c
    ${minhook_SOURCE_DIR}/src/hook.c
    ${minhook_SOURCE_DIR}/src/trampoline.c
    ${minhook_SOURCE_DIR}/src/hde/hde64.c)
target_include_directories(minhook PUBLIC ${minhook_SOURCE_DIR}/include)

# The game statically imports winmm.dll, so a winmm.dll next to the exe is loaded at startup.
add_library(BananMenu SHARED src/dllmain.cpp src/overlay.cpp src/menu.cpp src/ue.cpp src/features.cpp src/presets.cpp src/achievements.cpp src/missions.cpp src/esp.cpp src/lang.cpp src/resources.rc src/winmm_stubs.asm)
set_target_properties(BananMenu PROPERTIES OUTPUT_NAME winmm)
target_compile_definitions(BananMenu PRIVATE KM_VERSION="${BANAN_VERSION}" WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(BananMenu PRIVATE $<$<COMPILE_LANGUAGE:C,CXX>:/W4 /utf-8>)
option(KM_DUMPER "Dev build that loads Dumper-7.dll from the game folder (SDK re-dump after a patch)" OFF)
if(KM_DUMPER)
    target_compile_definitions(BananMenu PRIVATE KM_DUMPER)
endif()
target_link_libraries(BananMenu PRIVATE imgui minhook dxgi delayimp)
# Load these only when the overlay initialises, not at game startup.
target_link_options(BananMenu PRIVATE /DELAYLOAD:d3dcompiler_47.dll /DELAYLOAD:dxgi.dll)

enable_testing()
add_executable(spawn_test tests/spawn_test.cpp)
target_compile_options(spawn_test PRIVATE /W4 /utf-8)
add_test(NAME spawn_test COMMAND spawn_test)
add_executable(photo_test tests/photo_test.cpp)
target_compile_options(photo_test PRIVATE /W4 /utf-8)
add_test(NAME photo_test COMMAND photo_test)
add_executable(photo_ue_test tests/photo_ue_test.cpp)
target_link_libraries(photo_ue_test PRIVATE minhook)
target_compile_definitions(photo_ue_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(photo_ue_test PRIVATE /W4 /utf-8)
add_test(NAME photo_ue_test COMMAND photo_ue_test)
add_executable(presets_test tests/presets_test.cpp src/presets.cpp)
target_compile_definitions(presets_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(presets_test PRIVATE /W4 /utf-8)
add_test(NAME presets_test COMMAND presets_test)
add_executable(resources_test tests/resources_test.cpp)
target_compile_options(resources_test PRIVATE /W4 /utf-8)
add_test(NAME resources_test COMMAND resources_test)
add_library(fake_steam_api SHARED tests/fake_steam.cpp)
set_target_properties(fake_steam_api PROPERTIES OUTPUT_NAME steam_api64 RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test-fixtures")
add_executable(achievements_runtime_test tests/achievements_runtime_test.cpp src/achievements.cpp)
target_compile_definitions(achievements_runtime_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(achievements_runtime_test PRIVATE /W4 /utf-8)
add_test(NAME achievements_runtime_test COMMAND achievements_runtime_test $<TARGET_FILE:fake_steam_api>)
add_executable(achievements_test tests/achievements_test.cpp)
target_compile_options(achievements_test PRIVATE /W4 /utf-8)
add_test(NAME achievements_test COMMAND achievements_test)
add_executable(proxy_test tests/proxy_test.cpp)
add_test(NAME proxy_test COMMAND proxy_test $<TARGET_FILE:BananMenu>)
add_executable(ue_cache_test tests/ue_cache_test.cpp)
target_link_libraries(ue_cache_test PRIVATE minhook)
target_compile_definitions(ue_cache_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(ue_cache_test PRIVATE /W4 /utf-8)
add_test(NAME ue_cache_test COMMAND ue_cache_test)

# Minimal DX11/DX12 app that loads the DLL, toggles the menu and saves a screenshot (manual check of the overlay).
add_executable(overlay_host tests/overlay_host.cpp)
target_link_libraries(overlay_host PRIVATE d3d11 d3d12 dxgi)
