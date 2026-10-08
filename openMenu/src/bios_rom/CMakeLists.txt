# Portable (no KOS dependency): built for the Dreamcast and for the host test.
add_library(bios_rom STATIC src/bios_rom.c)
target_include_directories(bios_rom PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

if (BUILD_PC)
    enable_testing()
    add_executable(bios_rom_test test/bios_rom_test.c)
    target_link_libraries(bios_rom_test PRIVATE bios_rom)
    add_test(NAME bios_rom COMMAND bios_rom_test)
endif ()
