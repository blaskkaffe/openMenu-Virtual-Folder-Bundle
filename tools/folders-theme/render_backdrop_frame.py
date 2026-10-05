#!/usr/bin/env python3
"""Renders a frame of openMenu's animated backdrop on the build machine, to check it against the web page's scene and to make the theme
previews. It cuts the scene code (between the "The animated backdrop of themes" comment and "Rounded rectangles as real polygons") out of
openMenu/src/openmenu/src/ui/draw_kos.c, compiles it with stand-ins for the PVR calls (gcc needed), runs it for N frames and draws the
strips it submitted with a small software rasteriser: perspective-correct texture, Gouraud colour, alpha blending, in submission order.

  python3 render_backdrop_frame.py [frame] [out.png]      default: frame 82, backdrop_frame.png (the theme previews use it)

Compared with a screenshot of the web page's scene (Chromium, 640x480) the mean difference was about 1.4 levels out of 255."""
import os
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
UI = os.path.join(HERE, "..", "..", "openMenu", "src", "openmenu", "src", "ui")

HEAD = r"""#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef void* pvr_ptr_t;
typedef struct {struct {int culling;} gen; struct {int uv_clamp;} txr;} pvr_poly_cxt_t; typedef struct {int dummy;} pvr_poly_hdr_t;
typedef struct {uint32_t flags; float x,y,z,u,v; uint32_t argb,oargb;} pvr_vertex_t;
#define PVR_CMD_VERTEX 0xE0000000
#define PVR_CMD_VERTEX_EOL 0xF0000000
#define PVR_LIST_TR_POLY 2
#define PVR_TXRFMT_RGB565 1
#define PVR_TXRFMT_TWIDDLED 2
#define PVR_FILTER_BILINEAR 2
#define PVR_CULLING_NONE 0
#define PVR_UVCLAMP_UV 3
static FILE* out;
static float fsin(float x){return sinf(x);} static float fcos(float x){return cosf(x);}
static int draw_get_list(void){return 0;}
static pvr_ptr_t pvr_mem_malloc(size_t n){return malloc(n);} static void pvr_txr_load(const void* a,pvr_ptr_t b,size_t n){memcpy(b,a,n);}
static void pvr_poly_cxt_col(pvr_poly_cxt_t* c,int l){(void)c;(void)l;} static void pvr_poly_cxt_txr(pvr_poly_cxt_t* c,int l,int f,int w,int h,pvr_ptr_t t,int fl){(void)c;(void)l;(void)f;(void)w;(void)h;(void)t;(void)fl;}
static void pvr_poly_compile(pvr_poly_hdr_t* h,pvr_poly_cxt_t* c){(void)h;(void)c;}
static void pvr_prim(void* p,int n){ if(n==(int)sizeof(pvr_vertex_t)){pvr_vertex_t* v=p; fprintf(out,"V %f %f %f %f %f %08x %d\n",v->x,v->y,v->z,v->u,v->v,v->argb,v->flags==PVR_CMD_VERTEX_EOL);} else fprintf(out,"H\n");}
"""
TAIL = r"""int main(int argc,char**argv){
 out=fopen(argv[1],"w"); int frames=atoi(argv[2]);
 for(int f=0;f<frames;f++){ fprintf(out,"F\n"); draw_backdrop(0); draw_backdrop_scene(); }
 fclose(out); return 0;}"""


def build_and_run(frame, work):
    src = open(os.path.join(UI, "draw_kos.c")).read()
    a = src.index("/* The animated backdrop of themes")
    b = src.index("/* Rounded rectangles as real polygons.")
    code = src[a:b].replace('#include "ui/backdrop_texture.h"', '#include "backdrop_texture.h"')
    with open(os.path.join(work, "scene.c"), "w") as f:
        f.write(HEAD + code + "\n" + TAIL)
    subprocess.check_call(["gcc", "-w", "-I", UI, "-o", os.path.join(work, "scene"), os.path.join(work, "scene.c"), "-lm"])
    dump = os.path.join(work, "frame.txt")
    subprocess.check_call([os.path.join(work, "scene"), dump, str(frame)])
    return dump


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
def render(path, W=640, H=480):
    TEX = texture()
    img = np.zeros((H, W, 3))
    strips=[];cur=None
    for line in open(path):
        if line.startswith('F'): strips=[];cur=None
        elif line.startswith('H'): cur=[];strips.append(cur)
        elif line.startswith('V'):
            p=line.split(); cur.append((float(p[1]),float(p[2]),float(p[3]),float(p[4]),float(p[5]),int(p[6],16)))
            if p[7]=='1': cur=[];strips.append(cur)
    for st in strips:
        for i in range(len(st)-2):
            tri=st[i:i+3]
            xs=[p[0] for p in tri]; ys=[p[1] for p in tri]
            if max(xs)<0 or min(xs)>W or max(ys)<0 or min(ys)>H: continue
            x0,x1=max(0,int(min(xs))),min(W-1,int(max(xs))+1); y0,y1=max(0,int(min(ys))),min(H-1,int(max(ys))+1)
            if x1<=x0 or y1<=y0: continue
            gx,gy=np.meshgrid(np.arange(x0,x1+1)+0.5,np.arange(y0,y1+1)+0.5)
            (ax,ay,aw,au,av,ac),(bx,by,bw,bu,bv,bc),(cx,cy,cw,cu,cv,cc)=tri
            den=(by-cy)*(ax-cx)+(cx-bx)*(ay-cy)
            if abs(den)<1e-12: continue
            l1=((by-cy)*(gx-cx)+(cx-bx)*(gy-cy))/den; l2=((cy-ay)*(gx-cx)+(ax-cx)*(gy-cy))/den; l3=1-l1-l2
            m=(l1>=0)&(l2>=0)&(l3>=0)
            if not m.any(): continue
            ch=lambda c,s:(c>>s)&255
            col=[l1*ch(ac,s)+l2*ch(bc,s)+l3*ch(cc,s) for s in (16,8,0)]; al=(l1*ch(ac,24)+l2*ch(bc,24)+l3*ch(cc,24))/255.0
            sub=img[y0:y1+1,x0:x1+1]
            if aw<0.005:   # the untextured sky
                for k in range(3): sub[:,:,k]=np.where(m,col[k],sub[:,:,k])
                continue
            den2=l1*aw+l2*bw+l3*cw
            u=(l1*au*aw+l2*bu*bw+l3*cu*cw)/den2; v=(l1*av*aw+l2*bv*bw+l3*cv*cw)/den2
            wrap=max(au,bu,cu)>1.01 or min(au,bu,cu)<-0.01
            t=sample(TEX,u,v,wrap)
            for k in range(3):
                c=t[...,k]*col[k]/255.0
                sub[:,:,k]=np.where(m,sub[:,:,k]*(1-al)+c*al,sub[:,:,k])
    return Image.fromarray(np.clip(img,0,255).astype('uint8'))



if __name__ == "__main__":
    frame = int(sys.argv[1]) if len(sys.argv) > 1 else 82
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, "backdrop_frame.png")
    with tempfile.TemporaryDirectory() as work:
        render(build_and_run(frame, work)).save(out)
    print("wrote", out)
