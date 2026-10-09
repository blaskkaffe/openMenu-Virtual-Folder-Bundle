/* Host test for serial_region: gcc -I../src serial_region_test.c ../src/serial_region.c */
#include <stdio.h>

#include "serial_region.h"

static int failures;

#define CHECK(serial, pal)                                                                                             \
    do {                                                                                                               \
        if (serial_is_pal(serial) != (pal)) {                                                                          \
            printf("FAIL: %s should be %s\n", serial, (pal) ? "PAL" : "NTSC");                                          \
            failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

int
main(void) {
    CHECK("MK51035", 0);
    CHECK("MK5103550", 1);
    CHECK("MK5109505", 1);
    CHECK("MK5109518", 1);
    CHECK("HDR0054", 0);
    CHECK("T1215M", 0);
    CHECK("T8119N", 0);
    CHECK("T8102D", 1);
    CHECK("T8111D50", 1);
    CHECK("T45001D05", 1);
    CHECK("T13001D", 1);
    CHECK("T8103N50", 1);
    CHECK("T8103N18", 1);
    CHECK("T8103N", 0);
    CHECK("T1215M50", 0);
    CHECK("17701D", 1);
    CHECK("17701N", 0);
    CHECK("T40903M", 0);
    CHECK("", 0);
    CHECK("T", 0);
    CHECK("1234", 0);
    if (failures) {
        printf("serial_region: %d failures\n", failures);
        return 1;
    }
    printf("serial_region: all checks passed\n");
    return 0;
}
