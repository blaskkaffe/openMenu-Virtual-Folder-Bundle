/* serial_region: see serial_region.h. */
#include <ctype.h>
#include <string.h>

#include "serial_region.h"

int
serial_is_pal(const char* serial) {
    if (!serial || !serial[0]) {
        return 0;
    }
    size_t len = strlen(serial);

    if (!strncmp(serial, "MK", 2)) {
        int digits = 1;
        for (size_t i = 2; i < len; i++) {
            digits &= isdigit((unsigned char)serial[i]) != 0;
        }
        return digits && len - 2 == 7;
    }
    if (!strncmp(serial, "HDR", 3)) {
        return 0;
    }

    /* the last letter of the serial and what follows it */
    size_t at = len;
    while (at > 0 && !isalpha((unsigned char)serial[at - 1])) {
        at--;
    }
    if (at == 0 || at == 1) {
        return 0; /* no suffix letter (a leading T alone is not one) */
    }
    char letter = (char)toupper((unsigned char)serial[at - 1]);
    const char* rest = serial + at;
    if (letter == 'D') {
        return 1;
    }
    return !strcmp(rest, "50") && letter != 'T'; /* T8103N50: a European release of an American serial */
}
