#include "bios_case.h"

#include <math.h>
#include <string.h>

#define EASE 0.26f        /* fraction of the remaining way covered per frame: fast at first, then settles */
#define SETTLE_PX 3.0f

static unsigned
hash_tag(const char* s) {
    unsigned h = 2166136261u;
    for (; *s; s++) {
        h = (h ^ (unsigned char)*s) * 16777619u;
    }
    return h;
}

void
bcase_init(bcase* c) {
    memset(c, 0, sizeof(*c));
}

void
bcase_show(bcase* c, int model, const char* tag, int dir) {
    char t[BCASE_TAG_MAX];
    strncpy(t, tag ? tag : "", sizeof(t) - 1);
    t[sizeof(t) - 1] = '\0';
    if (model < 0) {
        t[0] = '\0';
    }
    if ((model < 0 && !c->cur.active) || (c->cur.active && c->cur.model == model && strcmp(c->cur.tag, t) == 0)) {
        return;
    }
    float d = dir >= 0 ? 1.0f : -1.0f;
    if (c->cur.active) {
        c->old = c->cur; /* an older one still flying out is dropped */
        c->old.leaving = 1;
        c->old.target = d > 0 ? BCASE_FLY_DOWN : -BCASE_FLY_UP;
    }
    memset(&c->cur, 0, sizeof(c->cur));
    if (model >= 0) {
        unsigned h = hash_tag(t);
        c->cur.active = 1;
        c->cur.model = model;
        memcpy(c->cur.tag, t, sizeof(c->cur.tag));
        c->cur.y = d > 0 ? -BCASE_FLY_UP : BCASE_FLY_DOWN; /* comes from the side the old one does not leave by */
        for (int k = 0; k < 3; k++) {
            c->cur.phase[k] = (float)((h >> (k * 8)) & 255) / 255.0f * 6.2831853f;
            c->cur.sign[k] = ((h >> (24 + k)) & 1) ? 1.0f : -1.0f;
        }
    }
}

void
bcase_step(bcase* c) {
    c->frame++;
    bcase_item* items[2] = {&c->cur, &c->old};
    for (int i = 0; i < 2; i++) {
        bcase_item* it = items[i];
        if (!it->active) {
            continue;
        }
        it->y += (it->target - it->y) * EASE;
        if (fabsf(it->target - it->y) < SETTLE_PX) {
            it->y = it->target;
            if (it->leaving) {
                memset(it, 0, sizeof(*it));
            }
        }
    }
}

static void
draw_item(const bcase* c, const bcase_item* it, bscene* s, float cx, float cy, float size_px, bcase_bind_fn bind, void* user,
          const bscene_sink* sink) {
    if (!it->active) {
        return;
    }
    /* slow tilt: periods of roughly 9, 12 and 15 seconds at 60 frames per second */
    static const float amp[3] = {13.0f, 30.0f, 6.0f};
    static const float freq[3] = {1.0f / 720.0f, 1.0f / 540.0f, 1.0f / 900.0f};
    float rot[3];
    for (int k = 0; k < 3; k++) {
        rot[k] = it->sign[k] * amp[k] * sinf(6.2831853f * freq[k] * (float)c->frame + it->phase[k]);
    }
    float fly = it->y / BCASE_FLY_PX; /* 0 at rest: lean into the motion while it flies (no roll: it flies straight) */
    rot[0] += 30.0f * (fly > 1.2f ? 1.2f : (fly < -1.2f ? -1.2f : fly));
    if (bind) {
        bind(user, it->tag);
    }
    float scale = size_px * (-BSCENE_PANEL_Z / 4000.0f) / BMODEL_CASE_HEIGHT;
    bscene_draw_model(s, it->model, cx, cy + it->y, scale, rot, sink);
}

void
bcase_draw(const bcase* c, bscene* s, float cx, float cy, float size_px, bcase_bind_fn bind, void* user, const bscene_sink* sink) {
    draw_item(c, &c->old, s, cx, cy, size_px, bind, user, sink);
    draw_item(c, &c->cur, s, cx, cy, size_px, bind, user, sink);
}
