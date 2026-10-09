/*
 * bmodel_obj: write the built-in models of bios_models.h as Wavefront OBJ (+ MTL) files, to look at
 * them in Blender or any viewer.   bmodel_obj outdir
 * Materials named front_art / back_art are the two picture slots (texture 0 and 1).
 */
#include <stdio.h>
#include <string.h>

#include "bios_models.h"

static const char* const names[BMODEL_COUNT] = {"phone", "globe", "case_white", "case_pal"};

int
main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s outdir\n", argv[0]);
        return 1;
    }
    for (int id = BMODEL_BASE; id < BMODEL_END; id++) {
        nj_object obj;
        if (bmodel_build(id, &obj) != 0) {
            return 1;
        }
        const nj_mesh* m = obj.nodes[0].mesh;
        char path[512];
        snprintf(path, sizeof(path), "%s/%s.obj", argv[1], names[id - BMODEL_BASE]);
        FILE* o = fopen(path, "w");
        snprintf(path, sizeof(path), "%s/%s.mtl", argv[1], names[id - BMODEL_BASE]);
        FILE* mt = fopen(path, "w");
        if (!o || !mt) {
            return 1;
        }
        fprintf(o, "mtllib %s.mtl\n", names[id - BMODEL_BASE]);
        for (int i = 0; i < m->nverts; i++) {
            fprintf(o, "v %g %g %g\n", m->verts[i].pos.x, m->verts[i].pos.y, m->verts[i].pos.z);
        }
        for (int i = 0; i < m->nverts; i++) {
            fprintf(o, "vn %g %g %g\n", m->verts[i].nrm.x, m->verts[i].nrm.y, m->verts[i].nrm.z);
        }
        int uvn = 0;
        for (int p = 0; p < m->npolys; p++) {
            const nj_poly* poly = &m->polys[p];
            char mat[32];
            if (poly->tex == BMODEL_TEX_FRONT) {
                strcpy(mat, "front_art");
            } else if (poly->tex == BMODEL_TEX_BACK) {
                strcpy(mat, "back_art");
            } else {
                snprintf(mat, sizeof(mat), "mat%d", p);
            }
            fprintf(mt, "newmtl %s\nKd %g %g %g\nd %g\n\n", mat, ((poly->diffuse >> 16) & 255) / 255.0, ((poly->diffuse >> 8) & 255) / 255.0,
                    (poly->diffuse & 255) / 255.0, (poly->diffuse >> 24) / 255.0);
            fprintf(o, "usemtl %s\n", mat);
            for (int t = 0; t < poly->ntris; t++) {
                if (poly->has_uv) {
                    for (int k = 0; k < 3; k++) {
                        fprintf(o, "vt %g %g\n", poly->corners[t * 3 + k].u, 1.0f - poly->corners[t * 3 + k].v);
                    }
                }
                fprintf(o, "f");
                for (int k = 0; k < 3; k++) {
                    int vi = poly->corners[t * 3 + k].idx + 1;
                    if (poly->has_uv) {
                        fprintf(o, " %d/%d/%d", vi, ++uvn, vi);
                    } else {
                        fprintf(o, " %d//%d", vi, vi);
                    }
                }
                fprintf(o, "\n");
            }
        }
        fclose(o);
        fclose(mt);
        nj_object_free(&obj);
    }
    return 0;
}
