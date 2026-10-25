# Read VERSION.TXT from repo root at build time
set(VERSION_FILE "${CMAKE_SOURCE_DIR}/VERSION.TXT")
if(EXISTS "${VERSION_FILE}")
    file(READ "${VERSION_FILE}" OPENMENU_VERSION)
    string(STRIP "${OPENMENU_VERSION}" OPENMENU_VERSION)
    message(STATUS "openMenu version: ${OPENMENU_VERSION}")
else()
    set(OPENMENU_VERSION "Unknown")
    message(WARNING "VERSION.TXT not found, using default version string")
endif()

set(OPENMENU_SOURCES
        src/main.c
        src/bg_palette.c
        src/clock.c
        src/clock_math.c
        src/gfx.c
        src/history.c
        src/dbglog.c
        src/favorites.c
        src/serial_log.c
        src/input.c
        src/launch.c
        src/qr.c
        src/serial_region.c
        src/serial_vmu.c
        src/sound.c
        src/ui_bios.c
        src/ui_fallback.c
        src/ui_files.c
        src/ui_internet.c
        src/ui_list.c
        src/ui_settings.c
        src/video.c
        src/vmu_files.c
        src/online.c
        src/backend/gdemu_sdk.c
        src/backend/dcnow_net.c
        src/backend/dcnow_fetch.c
        src/backend/dcnow_vmu.c
        src/backend/events_fetch.c
        src/backend/news_fetch.c
        src/backend/news_parse.c
        src/backend/dreampi_link.c
        src/backend/online_games.c
        src/backend/online_time_sync.c
        src/bench.c
        src/backend/serial_port.c
        src/vm2/vm2_api.c
)

add_executable(openmenu ${OPENMENU_SOURCES})

set(GFX_PRESORT 1 CACHE STRING "Draw translucent polygons in submission order instead of PVR autosort")
target_compile_definitions(openmenu PRIVATE GFX_PRESORT=${GFX_PRESORT})
set(GFX_VERTEX_DMA 1 CACHE STRING "Send the TA lists by DMA as the BIOS does (KOS vertex DMA mode), 0 = store queues")
target_compile_definitions(openmenu PRIVATE GFX_VERTEX_DMA=${GFX_VERTEX_DMA})
set(GFX_MAIN_AUTOSORT 1 CACHE STRING "Main menu: let the PVR sort translucent polygons per pixel, as the BIOS does (exact look of the icons)")
target_compile_definitions(openmenu PRIVATE GFX_MAIN_AUTOSORT=${GFX_MAIN_AUTOSORT})
# Switching the sort mode per frame needs KOS's internal PVR state (see gfx.c, set_tile_presort): KOS 2.1.1's
# pvr_set_presort_mode() moves the tile matrix on every call and hangs the PVR.
set(GFX_PVR_INTERNAL_DIR "$ENV{KOS_BASE}/kernel/arch/dreamcast/hardware/pvr")
if (EXISTS "${GFX_PVR_INTERNAL_DIR}/pvr_internal.h")
    target_include_directories(openmenu PRIVATE ${GFX_PVR_INTERNAL_DIR})
    target_compile_definitions(openmenu PRIVATE GFX_PVR_INTERNAL=1)
else ()
    message(WARNING "pvr_internal.h not found under KOS_BASE: the main menu keeps the default sort mode (GFX_PRESORT)")
endif ()

# Pass version string as compile definition
target_compile_definitions(openmenu PRIVATE OPENMENU_BUILD_VERSION="${OPENMENU_VERSION}")

# The commit goes into the serial boot line, so a crash dump can be matched to its openmenu.elf
if (DEFINED ENV{GITHUB_SHA})
    string(SUBSTRING "$ENV{GITHUB_SHA}" 0 7 OPENMENU_GIT_COMMIT)
else ()
    execute_process(COMMAND git rev-parse --short HEAD WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE OPENMENU_GIT_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif ()
if (NOT OPENMENU_GIT_COMMIT)
    set(OPENMENU_GIT_COMMIT "unknown")
endif ()
target_compile_definitions(openmenu PRIVATE OPENMENU_GIT_COMMIT="${OPENMENU_GIT_COMMIT}")

target_include_directories(openmenu
        PRIVATE
        src
)

target_link_libraries(openmenu
        PRIVATE
        bios_engine
        bios_rom
        crayon_savefile
        ini
        openmenu_settings
        openmenu_shared
        uthash
        ppp
)

if (NOT DEFINED KOS_OBJCOPY)
    # Try to find the program using find_program first (more robust than env var)
    # KOS often uses prefixes, check KOS docs for the exact name (e.g., sh-elf-objcopy)
    find_program(KOS_OBJCOPY_PATH NAMES sh-elf-objcopy ${KOS_CC_PREFIX}objcopy HINTS ENV KOS_BIN_DIR ENV KOS_BASE/bin ENV KOS_BASE/toolchains/dc/sh-elf/bin)

    if (KOS_OBJCOPY_PATH)
        set(KOS_OBJCOPY ${KOS_OBJCOPY_PATH} CACHE FILEPATH "Path to KOS objcopy tool")
        message(STATUS "Found KOS objcopy: ${KOS_OBJCOPY}")
        # Fallback to environment variable if find_program fails
    elseif (DEFINED ENV{KOS_OBJCOPY})
        set(KOS_OBJCOPY $ENV{KOS_OBJCOPY} CACHE FILEPATH "Path to KOS objcopy tool from ENV{KOS_OBJCOPY}")
        message(STATUS "Using KOS objcopy from environment variable: ${KOS_OBJCOPY}")
    else ()
        message(FATAL_ERROR "KOS objcopy tool not found. Set KOS_OBJCOPY environment variable or ensure it's in PATH, or adjust find_program HINTS.")
    endif ()
endif ()

if (CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(OPENMENU_BIN_OUTPUT ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/1ST_READ.BIN)
else ()
    set(OPENMENU_BIN_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/1ST_READ.BIN)
endif ()

add_custom_target(openmenu_bin ALL
        COMMAND
        ${KOS_OBJCOPY}
        -R .stack
        -O binary
        $<TARGET_FILE:openmenu>
        ${OPENMENU_BIN_OUTPUT}
        DEPENDS
        openmenu
        BYPRODUCTS
        ${OPENMENU_BIN_OUTPUT}
        COMMENT "Converting openmenu.elf to 1ST_READ.BIN"
        VERBATIM
)