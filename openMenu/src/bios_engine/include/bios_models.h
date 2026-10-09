/*
 * bios_models: extra 3D models in the style of the BIOS menu models, built in code (no ROM data).
 * They are plain nj_objects, so the scene draws them like any ROM model. A model id is passed
 * where the scripts pass a ROM model index; ids start at BMODEL_BASE.
 *
 * Texture slots (poly->tex, i.e. slot within the object's texlist):
 *   BMODEL_TEX_FRONT (0)  the front picture, e.g. the disc artwork or box art
 *   BMODEL_TEX_BACK  (1)  the back picture (nothing is placed there yet)
 * Everything else is untextured. The mapping of a slot to a real image is up to the renderer.
 *
 * Units match the ROM icon models (about +-5 for a menu icon). The models face +z, y is up.
 */
#ifndef BIOS_MODELS_H
#define BIOS_MODELS_H

#include "nj_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BMODEL_BASE 0x100
enum {
    BMODEL_PHONE = BMODEL_BASE,  /* classic telephone, the colour of the music note, white dial disc */
    BMODEL_GLOBE,                /* low poly earth in green and blue on a small stand */
    BMODEL_CASE_WHITE,           /* CD jewel case with a white spine */
    BMODEL_CASE_PAL,             /* CD jewel case in the blue style of the PAL Dreamcast cases */
    BMODEL_END
};
#define BMODEL_COUNT (BMODEL_END - BMODEL_BASE)

#define BMODEL_TEX_FRONT 0
#define BMODEL_TEX_BACK 1
#define BMODEL_NOTE_COLOR 0xFF7F66FFu /* material colour of the music note (ROM model 2) */

/* Build model `id`. Returns 0, or -1 for an unknown id or out of memory. Free with nj_object_free(). */
int bmodel_build(int id, nj_object* out);

#ifdef __cplusplus
}
#endif

#endif /* BIOS_MODELS_H */
