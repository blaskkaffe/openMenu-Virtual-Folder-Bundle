# Portable pieces of the PS2-style menu (no KOS dependency): built for the Dreamcast and for the host tests.
add_library(ps2_engine STATIC src/ps2_pack.c src/ps2_draw.c src/ps2_ui.c)
target_include_directories(ps2_engine PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
if (NOT CMAKE_CROSSCOMPILING)
    target_link_libraries(ps2_engine PUBLIC m)
endif ()

if (BUILD_PC)
    add_executable(ps2_engine_test test/ps2_engine_test.c)
    target_link_libraries(ps2_engine_test PRIVATE ps2_engine)
    add_test(NAME ps2_engine COMMAND ps2_engine_test)
    add_executable(ps2_preview tools/ps2_preview.c)
    target_link_libraries(ps2_preview PRIVATE ps2_engine)
endif ()
