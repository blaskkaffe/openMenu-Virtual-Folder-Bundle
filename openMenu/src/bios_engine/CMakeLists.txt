# Portable engine pieces (no KOS dependency): built for the Dreamcast and for the host tests.
add_library(bios_engine STATIC src/nj_model.c src/bios_models.c src/bios_vm.c src/dcbg.c src/bios_scene.c src/bios_menu.c src/bios_page.c src/bios_list.c src/bios_datetime.c src/bios_files.c src/bios_case.c src/bios_text.c src/bios_surface.c src/bios_filescr.c src/bios_audio.c src/bios_cdplayer.c src/tex_decode.c)
target_include_directories(bios_engine PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(bios_engine PUBLIC bios_rom)
if (NOT CMAKE_CROSSCOMPILING)
    target_link_libraries(bios_engine PUBLIC m)
endif ()

if (BUILD_PC)
    add_executable(bios_engine_test test/bios_engine_test.c)
    target_link_libraries(bios_engine_test PRIVATE bios_engine)
    add_test(NAME bios_engine COMMAND bios_engine_test)
endif ()
