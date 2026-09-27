# OpenCrossing-Xbox — original Xbox build (nxdk).
# Configure inside the SDK image with the nxdk toolchain file; see xbox/build.sh.
cmake_minimum_required(VERSION 3.16)
project(ac_xbox C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_EXTENSIONS ON)

set(DECOMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(PC_DIR      "${DECOMP_ROOT}/pc")
set(XBOX_DIR    "${CMAKE_CURRENT_SOURCE_DIR}")

# Kill switch for the whole-tree level (docs/perf.md). Default = upstream-proven -O2.
set(XBOX_OPT "-O2" CACHE STRING "Optimization level for the whole tree")

add_compile_definitions(
    TARGET_PC          # "not GameCube": LE/32-bit fixes. Must stay.
    TARGET_XBOX
    VERSION=0
    F3DEX_GBI_2
    NDEBUG
    BUGFIXES
    _LANGUAGE_C
    PC_GX_MAX_VERTS=16384   # 65536 x 96 B = 6 MB of BSS on PC; see docs/memory.md
)

add_compile_options(
    ${XBOX_OPT}
    -fno-strict-aliasing
    -fwrapv
    # clang's MSVC mode applies "Microsoft include search" (includer's includer
    # dirs), which resolves "types.h" to include/dolphin/types.h. Off.
    -fno-ms-compatibility
    "SHELL:-include ${XBOX_DIR}/include/xbox_prelude.h"
    # nxdk-cc puts its libc dirs first; repeat them last so the decomp's
    # include/libc shadows can #include_next the real header.
    "SHELL:-idirafter $ENV{NXDK_DIR}/lib/pdclib/platform/xbox/include"
    "SHELL:-idirafter $ENV{NXDK_DIR}/lib/pdclib/include"
    "SHELL:-idirafter $ENV{NXDK_DIR}/lib/xboxrt/libc_extensions"
)

# xbox/include first: it shadows pc/include/pc_platform.h and glad/gl.h.
include_directories(
    ${XBOX_DIR}/include
    ${DECOMP_ROOT}/include
    ${DECOMP_ROOT}/src
    ${DECOMP_ROOT}
    ${PC_DIR}/include
    ${PC_DIR}/lib/fixnes
    ${PC_DIR}/lib/glad/include
    $ENV{NXDK_DIR}/lib/sdl/SDL2/include
    $ENV{NXDK_DIR}/lib/zlib/zlib
)
add_compile_definitions(XBOX)   # what nxdk's sdl2.pc adds

# ---------------------------------------------------------------------------
# Game sources: same selection as pc/CMakeLists.txt (keep in sync).
# ---------------------------------------------------------------------------
file(GLOB_RECURSE GAME_C_SOURCES CONFIGURE_DEPENDS "${DECOMP_ROOT}/src/*.c")
file(GLOB_RECURSE GAME_CXX_SOURCES CONFIGURE_DEPENDS "${DECOMP_ROOT}/src/*.cpp")

foreach(pat
        ".*/AUS/.*"
        ".*/dolphin/gx/.*" ".*/dolphin/os/.*" ".*/dolphin/vi/.*" ".*/dolphin/pad/.*"
        ".*/dolphin/card/.*" ".*/dolphin/dvd/.*" ".*/dolphin/ai/.*" ".*/dolphin/ar/.*"
        ".*/dolphin/dsp/.*" ".*/dolphin/exi/.*" ".*/dolphin/si/.*" ".*/dolphin/db/.*"
        ".*/dolphin/mtx/.*" ".*/dolphin/base/.*" ".*/dolphin/gba/.*" ".*/dolphin/amcstubs/.*"
        ".*/dolphin/OdemuExi2/.*" ".*/dolphin/odenotstub/.*"
        ".*/TRK_MINNOW_DOLPHIN/.*" ".*/Runtime\\.PPCEABI\\.H/.*" ".*/MSL_C\\.PPCEABI\\.bare\\.H/.*"
        ".*/src/furniture/.*" ".*/src/data/model/obj_e_boat/.*"
        ".*/ac_npc_shop_common\\.c$" ".*/ac_animal_logo_misc\\.c$" ".*/m_item_debug\\.c$"
        ".*/actor/npc/.*_anime\\.c$" ".*/osreport\\.c$" ".*/Famicom/.*" ".*/MSL_C/.*"
        ".*/jaudio_NES/internal/dsp_cardunlock\\.c$" ".*/jaudio_NES/internal/dsp_GBAKey\\.c$"
        ".*/libc64/malloc\\.c$" ".*/libc64/sprintf\\.c$" ".*/libc64/aprintf\\.c$"
        ".*/libultra/.*" ".*/ReconfigBATs\\.c$" ".*/executor\\.c$" ".*/ef_effect_lib\\.c$"
        ".*/m_card\\.c$" ".*/emu64_utility\\.c$")
    list(FILTER GAME_C_SOURCES EXCLUDE REGEX "${pat}")
endforeach()
foreach(pat ".*/AUS/.*" ".*/dolphin/.*" ".*/TRK_MINNOW_DOLPHIN/.*" ".*/Runtime\\.PPCEABI\\.H/.*"
        ".*/MSL_C\\.PPCEABI\\.bare\\.H/.*" ".*/Famicom/.*" ".*/emu64_print\\.cpp$"
        ".*/jsyswrapper_ext\\.cpp$" ".*/jsyswrapper_main\\.cpp$")
    list(FILTER GAME_CXX_SOURCES EXCLUDE REGEX "${pat}")
endforeach()
list(APPEND GAME_CXX_SOURCES
    ${DECOMP_ROOT}/src/static/Famicom/famicom.cpp
    ${DECOMP_ROOT}/src/static/Famicom/famicom_nesinfo.cpp
    ${DECOMP_ROOT}/src/static/Famicom/ks_nes_core.cpp
    ${DECOMP_ROOT}/src/static/Famicom/ks_nes_draw.cpp
)

set_source_files_properties("${DECOMP_ROOT}/src/main.c" PROPERTIES COMPILE_DEFINITIONS "main=ac_entry")
set_source_files_properties("${DECOMP_ROOT}/src/static/boot.c" PROPERTIES COMPILE_DEFINITIONS "main=boot_main")
set_source_files_properties(
    "${DECOMP_ROOT}/src/static/libforest/emu64/emu64.c"
    "${DECOMP_ROOT}/src/static/jaudio_NES/internal/ja_calc.c"
    "${DECOMP_ROOT}/src/static/jaudio_NES/internal/jammain_2.c"
    "${DECOMP_ROOT}/src/static/jaudio_NES/game/game64.c"
    PROPERTIES LANGUAGE CXX)
set(CXX_FROM_C
    "${DECOMP_ROOT}/src/static/libforest/emu64/emu64.c"
    "${DECOMP_ROOT}/src/static/jaudio_NES/internal/ja_calc.c"
    "${DECOMP_ROOT}/src/static/jaudio_NES/internal/jammain_2.c"
    "${DECOMP_ROOT}/src/static/jaudio_NES/game/game64.c")
add_compile_options($<$<COMPILE_LANGUAGE:C>:-Dnullptr=NULL>)

# clang (not GCC): no -fpermissive. Downgrade the errors GCC tolerated to warnings,
# then silence warnings for decomp code (we don't own it).
set(DECOMP_FLAGS
    -w
    -include "${XBOX_DIR}/include/xbox_decomp_prelude.h"
    -Wno-error=implicit-function-declaration
    -Wno-error=implicit-int
    -Wno-error=int-conversion
    -Wno-error=incompatible-pointer-types
    -Wno-error=incompatible-function-pointer-types
    -Wno-error=return-type
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-c++11-narrowing>
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-register>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
)

# ---------------------------------------------------------------------------
# Platform: pc/src files reused as-is, plus xbox/src replacements.
# ---------------------------------------------------------------------------
set(PC_REUSED
    pc_gx.c pc_gx_texture.c pc_gbi_runtime.c pc_os.c pc_dvd.c
    pc_card.c pc_vi.c pc_misc.c pc_mtx.c pc_m_card.c pc_save_bswap.c
    pc_settings.c pc_profiler.c pc_pause_menu.c pc_settings_menu.c pc_menu_util.c
    pc_text_draw.c pc_nes_fixnes.c pc_stubs.c pc_stubs_cpp.cpp pc_assets.c pc_disc.c
    pc_pad.c pc_keybindings.c pc_typing.c)
list(TRANSFORM PC_REUSED PREPEND "${PC_DIR}/src/")
list(APPEND PC_REUSED "${DECOMP_ROOT}/src/static/dolphin/pad/Padclamp.c")

# pc_pad.c's stick reads go through xbox/src/xbox_pad_axis.c (spike filter).
set_property(SOURCE "${PC_DIR}/src/pc_pad.c" APPEND PROPERTY COMPILE_DEFINITIONS
    "SDL_GameControllerGetAxis=xbox_controller_axis")

# Debug: scripted pad input from PADRead call N on (xbox/src/xbox_autopad.c).
set(XBOX_AUTOPAD "" CACHE STRING "debug: inject START/A presses from this PADRead call on")
if(XBOX_AUTOPAD)
    set_property(SOURCE "${PC_DIR}/src/pc_pad.c" APPEND PROPERTY COMPILE_DEFINITIONS "PADRead=xbox_pad_read_real")
    set_property(SOURCE "${XBOX_DIR}/src/xbox_autopad.c" APPEND PROPERTY COMPILE_DEFINITIONS "XBOX_DBG_AUTOPAD=${XBOX_AUTOPAD}")
endif()

file(GLOB XBOX_SOURCES CONFIGURE_DEPENDS "${XBOX_DIR}/src/*.c" "${XBOX_DIR}/src/*.cpp")
list(APPEND XBOX_SOURCES "${PC_DIR}/lib/glad/src/gl.c")

set(FIXNES_DIR ${PC_DIR}/lib/fixnes)
file(GLOB FIXNES_MAPPER_SOURCES ${FIXNES_DIR}/mapper/*.c)
set(FIXNES_SOURCES
    ${FIXNES_DIR}/cpu.c ${FIXNES_DIR}/ppu.c ${FIXNES_DIR}/apu.c ${FIXNES_DIR}/mem.c
    ${FIXNES_DIR}/input.c ${FIXNES_DIR}/mapper.c ${FIXNES_DIR}/mapperList.c
    ${FIXNES_DIR}/vrc_irq.c ${FIXNES_DIR}/audio.c ${FIXNES_DIR}/fm2play.c
    ${FIXNES_DIR}/audio_fds.c ${FIXNES_DIR}/audio_mmc5.c ${FIXNES_DIR}/audio_vrc6.c
    ${FIXNES_DIR}/audio_vrc7.c ${FIXNES_DIR}/audio_n163.c ${FIXNES_DIR}/audio_s5b.c
    ${FIXNES_MAPPER_SOURCES})
# No DO_INLINE_ATTRIBS: with _MSC_VER defined it becomes __forceinline, which
# emits no out-of-line copy for the cross-TU callers in pc_nes_fixnes.c.
set_source_files_properties(${FIXNES_SOURCES} PROPERTIES
    COMPILE_OPTIONS "-w"
    COMPILE_DEFINITIONS "COL_TEX_BSWAP;COL_GL_BSWAP")

set_source_files_properties(${GAME_C_SOURCES} ${GAME_CXX_SOURCES} PROPERTIES
    COMPILE_OPTIONS "${DECOMP_FLAGS}")
# CMake does not know nxdk-cxx is clang, so it won't add -x c++ for LANGUAGE CXX .c files.
set_source_files_properties(${CXX_FROM_C} PROPERTIES
    COMPILE_OPTIONS "-xc++;-Wno-register;${DECOMP_FLAGS}")
set_source_files_properties(${PC_REUSED} ${XBOX_SOURCES} PROPERTIES
    COMPILE_OPTIONS "-Wno-unused-parameter;-Wno-sign-compare;-Wno-comment;-Wno-unused-function;-Wno-error=implicit-function-declaration;-Wno-error=incompatible-pointer-types;-Wno-error=int-conversion")

# Object-only target for triage (XBOX_OBJS_ONLY=ON builds without linking).
option(XBOX_OBJS_ONLY "Compile every TU, skip the link" OFF)
set(ALL_SOURCES ${XBOX_SOURCES} ${PC_REUSED} ${FIXNES_SOURCES} ${GAME_C_SOURCES} ${GAME_CXX_SOURCES})
if(XBOX_OBJS_ONLY)
    add_library(ac_objs OBJECT ${ALL_SOURCES})
else()
    add_executable(ac_xbox ${ALL_SOURCES})
    target_link_libraries(ac_xbox
        $ENV{NXDK_DIR}/lib/libSDL2.lib
        $ENV{NXDK_DIR}/lib/libpbkit.lib
        $ENV{NXDK_DIR}/lib/libzlib.lib
        $ENV{NXDK_DIR}/lib/winmm.lib
        $ENV{NXDK_DIR}/lib/libnxdk_automount_d.lib)
    # -include:_automount_d_drive pulls in nxdk's D: -> XBE-folder mount
    # (the nxdk Makefile does this; without it D:\ lists nothing).
    target_link_options(ac_xbox PRIVATE -stack:1048576 -include:_automount_d_drive -map:${CMAKE_BINARY_DIR}/ac_xbox.map)
    add_custom_command(TARGET ac_xbox POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/xbe
        COMMAND $ENV{NXDK_DIR}/tools/cxbe/cxbe "-OUT:${CMAKE_BINARY_DIR}/xbe/default.xbe"
                "-TITLE:OpenCrossing"
                $<TARGET_FILE:ac_xbox>
        COMMENT "cxbe -> xbe/default.xbe")
endif()
