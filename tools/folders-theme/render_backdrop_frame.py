#!/usr/bin/env python3
"""Renders a frame of one of openMenu's 3D backdrops on the build machine, for the theme previews and to check the scenes. It compiles the
scene's own source file (openMenu/src/openmenu/src/ui/backdrop_<scene>.c) against stand-ins for the PVR calls (gcc needed), runs it for N
frames and draws the strips it submitted with a small software rasteriser: a depth buffer like the PVR's (nearer = greater 1/w),
perspective-correct texture, Gouraud colour and the offset colour (the scenes are opaque).

  python3 render_backdrop_frame.py [frame] [out.png] [scene] [setting]
      scene: waves (default) or synthwave.  setting: waves = clouds percent (default 100); synthwave = palette, sunset (default) or ice.
      default: frame 82, backdrop_frame.png (the still themes' background; the previews use it)

Compared with a screenshot of the web page's scene (Chromium, 640x480) the waves differed by about 1.4 levels out of 255."""
import os
import shutil
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
UI = os.path.join(HERE, "..", "..", "openMenu", "src", "openmenu", "src", "ui")

# palettes the themes give the synthwave scene (THEME.INI scene_*): sky top, sky bottom, sun top, sun bottom, grid, ground, mountain
PALETTES = {
    "sunset": (0x14042E, 0xFF5AA0, 0xFFE23C, 0xFF2A8C, 0xFF46C8, 0x12062A, 0x2A0C4A),
    "ice": (0x04061E, 0x4A78F0, 0xC8FFFF, 0x3C9CFF, 0x3CDCFF, 0x050A24, 0x101450),
}

HEAD = r"""#pragma once
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef void* pvr_ptr_t;
typedef struct {struct {int culling; int specular;} gen; struct {int uv_clamp;} txr; int tex;} pvr_poly_cxt_t; typedef struct {int tex;} pvr_poly_hdr_t;
typedef struct {uint32_t flags; float x,y,z,u,v; uint32_t argb,oargb;} pvr_vertex_t;
#define PVR_CMD_VERTEX 0xE0000000
#define PVR_CMD_VERTEX_EOL 0xF0000000
#define PVR_LIST_TR_POLY 2
#define PVR_TXRFMT_RGB565 1
#define PVR_TXRFMT_TWIDDLED 2
#define PVR_FILTER_BILINEAR 2
#define PVR_CULLING_NONE 0
#define PVR_SPECULAR_ENABLE 1
#define PVR_UVCLAMP_UV 3
extern FILE* g_out;
static float fsin(float x){return sinf(x);} static float fcos(float x){return cosf(x);}
static int draw_get_list(void){return 0;}
static pvr_ptr_t pvr_mem_malloc(size_t n){return malloc(n);} static void pvr_txr_load(const void* a,pvr_ptr_t b,size_t n){memcpy(b,a,n);}
static void pvr_poly_cxt_col(pvr_poly_cxt_t* c,int l){(void)l;c->tex=0;} static void pvr_poly_cxt_txr(pvr_poly_cxt_t* c,int l,int f,int w,int h,pvr_ptr_t t,int fl){(void)l;(void)f;(void)w;(void)h;(void)t;(void)fl;c->tex=1;}
static void pvr_poly_compile(pvr_poly_hdr_t* h,pvr_poly_cxt_t* c){h->tex=c->tex;}
static void pvr_prim(void* p,int n){ if(n==(int)sizeof(pvr_vertex_t)){pvr_vertex_t* v=p; fprintf(g_out,"V %f %f %f %f %f %08x %08x %d\n",v->x,v->y,v->z,v->u,v->v,v->argb,v->oargb,v->flags==PVR_CMD_VERTEX_EOL);} else fprintf(g_out,"H %d\n",((pvr_poly_hdr_t*)p)->tex);}
"""

MAIN = r"""#include "ui/draw_prototypes.h"
#include "ui/backdrop.h"
FILE* g_out;
__attribute__((weak)) void backdrop_waves_draw(const backdrop_params_t* p){(void)p;}
__attribute__((weak)) void backdrop_synthwave_draw(const backdrop_params_t* p){(void)p;}
int main(int argc,char**argv){
 g_out=fopen(argv[1],"w"); int frames=atoi(argv[2]); int scene=atoi(argv[3]);
 backdrop_params_t p; memset(&p,0,sizeof(p)); p.scene=(backdrop_scene_t)scene; p.clouds_percent=atoi(argv[4]);
 if(argc>=12){ p.sky_top=strtoul(argv[5],0,16); p.sky_bottom=strtoul(argv[6],0,16); p.sun_top=strtoul(argv[7],0,16); p.sun_bottom=strtoul(argv[8],0,16); p.grid=strtoul(argv[9],0,16); p.ground=strtoul(argv[10],0,16); p.mountain=strtoul(argv[11],0,16); }
 for(int f=0;f<frames;f++){ fprintf(g_out,"F\n"); if(scene==1) backdrop_synthwave_draw(&p); else backdrop_waves_draw(&p); }
 fclose(g_out); return 0;}"""


SCENE = "waves"
SETTING = "100"


def build_and_run(frame, work):
    os.makedirs(os.path.join(work, "ui"))
    with open(os.path.join(work, "ui", "draw_prototypes.h"), "w") as f:
        f.write(HEAD)
    for name in ("backdrop.h", "backdrop_texture.h"):
        shutil.copy(os.path.join(UI, name), os.path.join(work, "ui", name))
    with open(os.path.join(work, "main.c"), "w") as f:
        f.write(MAIN)
    scene_id = 1 if SCENE == "synthwave" else 0
    subprocess.check_call(["gcc", "-w", "-I", work, "-o", os.path.join(work, "scene"), os.path.join(work, "main.c"),
                           os.path.join(UI, "backdrop_%s.c" % SCENE), "-lm"])
    args = [os.path.join(work, "scene"), os.path.join(work, "frame.txt"), str(frame), str(scene_id), SETTING if SCENE == "waves" else "100"]
    if SCENE == "synthwave":
        args += ["%06x" % c for c in PALETTES[SETTING]]
    subprocess.check_call(args)
    return os.path.join(work, "frame.txt")


def texture():
    """The 64x64 cloud picture, decoded from backdrop_texture.h (RGB565, twiddled)."""
    import re
    import pvr
    words = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{4})", open(os.path.join(UI, "backdrop_texture.h")).read())]
    raw = np.array(words, dtype="<u2")
    header = b"GBIX" + (8).to_bytes(4, "little") + (0).to_bytes(4, "little") + b"\0\0\0\0" + b"PVRT" + (len(raw) * 2 + 8).to_bytes(4, "little") + bytes([1, 1, 0, 0]) + (64).to_bytes(2, "little") + (64).to_bytes(2, "little")
    return pvr.decode(header + raw.tobytes())[0].astype(np.float64)


def sample(TEX, u, v, wrap):
    if wrap: u=u%1.0; v=v%1.0
    else: u=np.clip(u,0,1-1e-6); v=np.clip(v,0,1-1e-6)
    x=u*64-0.5; y=v*64-0.5
    x0=np.floor(x).astype(int); y0=np.floor(y).astype(int); fx=(x-x0)[...,None]; fy=(y-y0)[...,None]
    def px(a,b):
        if wrap: return TEX[b%64,a%64]
        return TEX[np.clip(b,0,63),np.clip(a,0,63)]
    return (px(x0,y0)*(1-fx)*(1-fy)+px(x0+1,y0)*fx*(1-fy)+px(x0,y0+1)*(1-fx)*fy+px(x0+1,y0+1)*fx*fy)
def sample(TEX, u, v, wrap):
    if wrap: u=u%1.0; v=v%1.0
    else: u=np.clip(u,0,1-1e-6); v=np.clip(v,0,1-1e-6)
    x=u*64-0.5; y=v*64-0.5
    x0=np.floor(x).astype(int); y0=np.floor(y).astype(int); fx=(x-x0)[...,None]; fy=(y-y0)[...,None]
    def px(a,b):
        if wrap: return TEX[b%64,a%64]
        return TEX[np.clip(b,0,63),np.clip(a,0,63)]
    return (px(x0,y0)*(1-fx)*(1-fy)+px(x0+1,y0)*fx*(1-fy)+px(x0,y0+1)*(1-fx)*fy+px(x0+1,y0+1)*fx*fy)
def render(path, W=640, H=480):
    TEX = texture()
    img = np.zeros((H, W, 3))
    zbuf = np.zeros((H, W))
    strips = []
    cur = None
    flag = 0
    for line in open(path):
        if line.startswith('F'):
            strips = []; cur = None
        elif line.startswith('H'):
            flag = int(line.split()[1]); cur = []; strips.append((flag, cur))
        elif line.startswith('V'):
            p = line.split(); cur.append((float(p[1]), float(p[2]), float(p[3]), float(p[4]), float(p[5]), int(p[6], 16), int(p[7], 16)))
            if p[8] == '1':
                cur = []; strips.append((flag, cur))
    for tex, st in strips:
        for i in range(len(st) - 2):
            tri = st[i:i + 3]
            xs = [p[0] for p in tri]; ys = [p[1] for p in tri]
            if max(xs) < 0 or min(xs) > W or max(ys) < 0 or min(ys) > H: continue
            x0, x1 = max(0, int(min(xs))), min(W - 1, int(max(xs)) + 1); y0, y1 = max(0, int(min(ys))), min(H - 1, int(max(ys)) + 1)
            if x1 <= x0 or y1 <= y0: continue
            gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
            (ax, ay, aw, au, av, ac, ao), (bx, by, bw, bu, bv, bc, bo), (cx, cy, cw, cu, cv, cc, co) = tri
            den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
            if abs(den) < 1e-12: continue
            l1 = ((by - cy) * (gx - cx) + (cx - bx) * (gy - cy)) / den; l2 = ((cy - ay) * (gx - cx) + (ax - cx) * (gy - cy)) / den; l3 = 1 - l1 - l2
            m = (l1 >= 0) & (l2 >= 0) & (l3 >= 0)
            if not m.any(): continue
            z = l1 * aw + l2 * bw + l3 * cw
            sub = img[y0:y1 + 1, x0:x1 + 1]; zs = zbuf[y0:y1 + 1, x0:x1 + 1]
            m = m & (z > zs)                       # the PVR's depth test: greater 1/w is nearer
            if not m.any(): continue
            ch = lambda c, s: (c >> s) & 255
            col = [l1 * ch(ac, s) + l2 * ch(bc, s) + l3 * ch(cc, s) for s in (16, 8, 0)]; off = [l1 * ch(ao, s) + l2 * ch(bo, s) + l3 * ch(co, s) for s in (16, 8, 0)]
            if not tex:
                for k in range(3): sub[:, :, k] = np.where(m, col[k], sub[:, :, k])
                zs[m] = z[m]
                continue
            den2 = l1 * aw + l2 * bw + l3 * cw
            u = (l1 * au * aw + l2 * bu * bw + l3 * cu * cw) / den2; v = (l1 * av * aw + l2 * bv * bw + l3 * cv * cw) / den2
            wrap = max(au, bu, cu) > 1.01 or min(au, bu, cu) < -0.01
            t = sample(TEX, u, v, wrap)
            for k in range(3):
                c = np.clip(t[..., k] * col[k] / 255.0 + off[k], 0, 255)      # picture x colour + offset colour (an opaque polygon)
                sub[:, :, k] = np.where(m, c, sub[:, :, k])
            zs[m] = z[m]
    return Image.fromarray(np.clip(img, 0, 255).astype('uint8'))


if __name__ == "__main__":
    frame = int(sys.argv[1]) if len(sys.argv) > 1 else 82
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, "backdrop_frame.png")
    SCENE = sys.argv[3] if len(sys.argv) > 3 else "waves"
    SETTING = sys.argv[4] if len(sys.argv) > 4 else ("100" if SCENE == "waves" else "sunset")
    with tempfile.TemporaryDirectory() as work:
        render(build_and_run(frame, work)).save(out)
    print("wrote", out)
