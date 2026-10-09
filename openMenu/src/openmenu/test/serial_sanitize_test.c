/* Host test for serial_sanitize: the remap tables must work without anyone calling the init function.
 * gcc -I../../external/uthash/include -I../../openmenu_shared/include/texture serial_sanitize_test.c
 *     ../../openmenu_shared/src/texture/serial_sanitize.c */
#include <stdio.h>
#include <string.h>

#include "serial_sanitize.h"

static int failures;

#define EXPECT(call, in, want)                                                                                         \
    do {                                                                                                               \
        const char* got = call(in);                                                                                    \
        if (strcmp(got, want)) {                                                                                       \
            printf("FAIL: %s(%s) = %s, want %s\n", #call, in, got, want);                                              \
            failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

int
main(void) {
    /* regional duplicates share art and meta */
    EXPECT(serial_santize_art, "T45001D09", "T45001D05");
    EXPECT(serial_santize_meta, "T45001D09", "T45001D05");
    EXPECT(serial_santize_art, "MK5109506", "MK5109505");
    EXPECT(serial_santize_art, "T8103N18", "T8103N50");
    /* missing meta: only meta is redirected, art keeps the serial */
    EXPECT(serial_santize_meta, "T8102D", "T8101N");
    EXPECT(serial_santize_art, "T8102D", "T8102D");
    EXPECT(serial_santize_meta, "MK5117850", "MK51178");
    /* not in a table: unchanged */
    EXPECT(serial_santize_art, "T8119N", "T8119N");
    EXPECT(serial_santize_meta, "T8119N", "T8119N");
    EXPECT(serial_santize_art, "", "");
    if (failures) {
        printf("serial_sanitize: %d failures\n", failures);
        return 1;
    }
    printf("serial_sanitize: all checks passed\n");
    return 0;
}
