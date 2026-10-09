/*
 * nj_model: Ninja chunk-model objects and motions as stored in the BIOS menu
 * image, parsed into plain triangle lists. Portable C, no hardware access.
 *
 * Port of the reference parsers in the boot ROM decompile (njcnk.py/njmotion.py).
 * Only the chunk types the menu models use are handled; anything else makes the
 * load fail instead of producing a broken mesh.
 */
#ifndef NJ_MODEL_H
#define NJ_MODEL_H

#include <stdint.h>

#include "bios_rom.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nj_vec3 {
    float x, y, z;
} nj_vec3;

typedef struct nj_vertex {
    nj_vec3 pos;
    nj_vec3 nrm;
    uint32_t col;     /* ARGB, valid when has_col */
    uint8_t valid;    /* vertex was defined by a vertex chunk */
    uint8_t has_nrm;
    uint8_t has_col;
} nj_vertex;

typedef struct nj_corner {
    uint16_t idx; /* vertex index within the node's mesh */
    float u, v;   /* 0..1, only meaningful when the poly has_uv */
} nj_corner;

typedef struct nj_poly {
    int tex;              /* texture slot within the texlist, -1 when untextured */
    uint32_t diffuse;     /* ARGB, valid when has_diffuse */
    uint8_t has_diffuse;
    uint32_t ambient;     /* ARGB, valid when has_ambient (material chunk bit 1) */
    uint32_t specular;    /* ARGB, valid when has_specular (material chunk bit 2) */
    uint8_t has_ambient;
    uint8_t has_specular;
    uint8_t has_uv;
    uint8_t blend;        /* blend chunk flags (src/dst alpha modes), 0 when absent */
    uint8_t strip_flags;  /* flag byte of the strip chunk header (culling, double side...) */
    int ntris;
    nj_corner* corners;   /* 3 * ntris entries */
} nj_poly;

typedef struct nj_mesh {
    int nverts;           /* highest vertex index + 1 */
    nj_vertex* verts;
    int npolys;
    nj_poly* polys;
} nj_mesh;

/* eval flags of an NJS_OBJECT node */
#define NJ_EVAL_NO_TRANSLATE 0x01
#define NJ_EVAL_NO_ROTATE 0x02
#define NJ_EVAL_NO_SCALE 0x04
#define NJ_EVAL_ZXY 0x20 /* rotate Y, X, Z instead of Z, Y, X */

typedef struct nj_node {
    uint32_t eval;
    nj_vec3 pos;
    nj_vec3 scl;
    int32_t ang[3];  /* Ninja angles, 0x10000 = 360 degrees, sign extended from 16 bits */
    int parent;      /* index into nodes, -1 for the root */
    nj_mesh* mesh;   /* NULL for pure transform nodes */
} nj_node;

typedef struct nj_object {
    int count;
    nj_node* nodes;  /* depth first, parents before children */
} nj_object;

/* Load the object tree at RAM address `addr`. Returns 0, or -1 on any parse problem. */
int nj_object_load(const bios_rom* rom, uint32_t addr, nj_object* out);
void nj_object_free(nj_object* obj);

/* ---- Motions -------------------------------------------------------------- */

typedef struct nj_key {
    uint32_t frame;
    float v[3]; /* position / scale, or angles in Ninja units for rotation */
} nj_key;

typedef struct nj_channel {
    int count;
    nj_key* keys;
} nj_channel;

typedef struct nj_motion_node {
    nj_channel pos, ang, scl;
} nj_motion_node;

typedef struct nj_motion {
    int frames;
    int spline;            /* interpolation flag of the file; sampled linearly either way */
    int count;             /* motion nodes, equal to the object's node count */
    nj_motion_node* nodes;
} nj_motion;

/* Load the motion at `addr` for an object with `node_count` nodes. Returns 0 or -1. */
int nj_motion_load(const bios_rom* rom, uint32_t addr, int node_count, nj_motion* out);
void nj_motion_free(nj_motion* m);

/* ---- Matrices (row-major 4x4, column vectors, like the reference exporter) --- */

typedef struct nj_mat4 {
    float m[4][4];
} nj_mat4;

void nj_mat_identity(nj_mat4* r);
void nj_mat_mul(nj_mat4* r, const nj_mat4* a, const nj_mat4* b); /* r = a * b, r may alias a or b */
nj_vec3 nj_mat_apply(const nj_mat4* m, nj_vec3 p);

/* Object transform as the BIOS builds it: T(pos) * S(scale) * Rx * Ry * Rz (angles in Ninja units). */
void nj_mat_object(nj_mat4* out, const float pos[3], const float scl[3], const int32_t rot[3]);

/* Ninja angle (0x10000 = 360 degrees) to radians */
float nj_ang_to_rad(int32_t a);

/* Local matrix of a node, optionally overridden by motion values. */
void nj_node_matrix(const nj_node* n, const float* pos_override, const float* ang_override, const float* scl_override,
                    nj_mat4* out);

/* World matrices of every node for motion frame `frame` (motion may be NULL for the rest pose). */
void nj_object_pose(const nj_object* obj, const nj_motion* motion, float frame, nj_mat4* world /* [obj->count] */);

#ifdef __cplusplus
}
#endif

#endif /* NJ_MODEL_H */
