/*
 * bios_vm: object and script engine of the BIOS menu, see bios_vm.h.
 * Semantics follow the decompiled opcode handlers and obj_run_scripts_and_draw.
 */
#include "bios_vm.h"

#include <string.h>

/* script_exec result actions (top nibble of a handler's return value) */
#define ACT_NEXT 0x0
#define ACT_YIELD 0x1
#define ACT_JUMP_ABS 0x3
#define ACT_END_TRACK 0x4
#define ACT_ERROR 0x5

#define RET(action, len) (((uint32_t)(action) << 28) | (uint32_t)(len))

typedef struct ctx {
    bvm* vm;
    bvm_obj* obj;
    int track_idx;
    uint32_t pc;
    uint32_t jump;
    int stop; /* frames until the track runs again */
    int object_ended;
} ctx;

static int32_t
op_s32(const ctx* c, uint32_t off) {
    return (int32_t)bios_rom_u32(c->vm->rom, c->pc + off);
}

static uint16_t
op_u16(const ctx* c, uint32_t off) {
    return bios_rom_u16(c->vm->rom, c->pc + off);
}

static uint8_t
op_u8(const ctx* c, uint32_t off) {
    const bios_rom* r = c->vm->rom;
    return (c->pc + off < r->size) ? r->data[c->pc + off] : 0;
}

/* script_read_value: literal, or 0x80000000 + n for object variable n */
static int32_t
op_value(const ctx* c, uint32_t off) {
    int32_t v = op_s32(c, off);
    if (v > -0x7fffff01) {
        return v;
    }
    return ((v & 0xff) < BVM_VARS) ? c->obj->var[v & 0xf] : 0;
}

static float
op_fixed(const ctx* c, uint32_t off) {
    return (float)op_value(c, off) / 256.0f;
}

static uint32_t
op_rel(const ctx* c, uint32_t off) {
    return c->pc + (uint32_t)op_s32(c, off);
}

static bvm_track*
cur_track(ctx* c) {
    return &c->obj->track[c->track_idx];
}

/* ---- Objects ----------------------------------------------------------------- */

static void
link_by_prio(bvm* vm, int slot) {
    int16_t prio = vm->objs[slot].prio;
    int at = vm->count;
    while (at > 0 && vm->objs[vm->order[at - 1]].prio < prio) {
        at--;
    }
    memmove(&vm->order[at + 1], &vm->order[at], sizeof(int) * (size_t)(vm->count - at));
    vm->order[at] = slot;
    vm->count++;
}

static void
unlink_slot(bvm* vm, int slot) {
    for (int i = 0; i < vm->count; i++) {
        if (vm->order[i] == slot) {
            memmove(&vm->order[i], &vm->order[i + 1], sizeof(int) * (size_t)(vm->count - i - 1));
            vm->count--;
            break;
        }
    }
    vm->objs[slot].active = 0;
}

static bvm_obj*
alloc_obj(bvm* vm, uint16_t id, int16_t prio, uint32_t pc) {
    for (int i = 0; i < BVM_MAX_OBJECTS; i++) {
        if (!vm->objs[i].active) {
            bvm_obj* o = &vm->objs[i];
            memset(o, 0, sizeof(*o));
            o->active = 1;
            o->id = id;
            o->prio = prio;
            o->track[0].active = 1;
            o->track[0].wait = 1;
            o->track[0].pc = pc;
            for (int a = 0; a < 3; a++) {
                o->scale_tw[a].cur = 1.0f;
            }
            o->text_attr = 1;
            o->text_colour = 0xFCCC;
            o->text_adv_wide = 22;
            o->text_adv_narrow = 11;
            link_by_prio(vm, i);
            return o;
        }
    }
    return NULL;
}

void
bvm_init(bvm* vm, const bios_rom* rom, const bvm_host* host) {
    memset(vm, 0, sizeof(*vm));
    vm->rom = rom;
    if (host) {
        vm->host = *host;
    }
}

bvm_obj*
bvm_create(bvm* vm, int script, uint16_t id, int16_t prio) {
    uint32_t pc = bios_script_offset(vm->rom, script);
    return pc ? alloc_obj(vm, id, prio, pc) : NULL;
}

bvm_obj*
bvm_find(bvm* vm, uint16_t id) {
    for (int i = 0; i < vm->count; i++) {
        bvm_obj* o = &vm->objs[vm->order[i]];
        if (o->id == id) {
            return o;
        }
    }
    return NULL;
}

void
bvm_kill(bvm* vm, uint16_t id) {
    for (int i = vm->count - 1; i >= 0; i--) {
        if (vm->objs[vm->order[i]].id == id) {
            unlink_slot(vm, vm->order[i]);
        }
    }
}

void
bvm_kill_all(bvm* vm) {
    for (int i = 0; i < BVM_MAX_OBJECTS; i++) {
        vm->objs[i].active = 0;
    }
    vm->count = 0;
}

/* ---- Opcodes ------------------------------------------------------------------- */

static uint32_t
exec_one(ctx* c) {
    bvm_obj* o = c->obj;
    uint8_t op = op_u8(c, 0);

    switch (op) {
        case 0x01: /* end_object */
            unlink_slot(c->vm, (int)(o - c->vm->objs));
            c->object_ended = 1;
            return RET(ACT_END_TRACK, 1);
        case 0x02: /* idle_check */
            if (!c->vm->host.input_active || !c->vm->host.input_active(c->vm->host.user)) {
                return 5;
            }
            c->jump = op_rel(c, 1);
            return RET(ACT_JUMP_ABS, 0);
        case 0x10: /* yield */
            c->stop = 1;
            return RET(ACT_YIELD, 1);
        case 0x11: /* wait n */
            c->stop = op_value(c, 1);
            return RET(ACT_YIELD, 5);
        case 0x13: /* jump */
            c->jump = op_rel(c, 1);
            return RET(ACT_JUMP_ABS, 0);
        case 0x14: { /* switch var, max, table */
            int32_t vi = op_value(c, 1); /* literal variable number, as in the original */
            int32_t idx = (vi >= 0 && vi < BVM_VARS) ? o->var[vi] : -1;
            uint32_t table = op_rel(c, 7);
            if (idx < 0 || idx > op_u16(c, 5)) {
                return 11;
            }
            c->jump = table + bios_rom_u32(c->vm->rom, table + 4u * (uint32_t)idx);
            return RET(ACT_JUMP_ABS, 0);
        }
        case 0x15: { /* loop count */
            bvm_track* t = cur_track(c);
            if (t->loop_depth >= 4) {
                return RET(ACT_ERROR, 0);
            }
            t->loop_ret[t->loop_depth] = c->pc + 9;
            t->loop_var[t->loop_depth] = (int16_t)(bios_rom_u32(c->vm->rom, c->pc + 1) & 0xff);
            o->var[t->loop_var[t->loop_depth] & 0xf] = op_value(c, 5);
            t->loop_depth++;
            return 9;
        }
        case 0x16: { /* endloop */
            bvm_track* t = cur_track(c);
            if (t->loop_depth <= 0) {
                return RET(ACT_ERROR, 0);
            }
            int d = --t->loop_depth;
            int32_t* v = &o->var[t->loop_var[d] & 0xf];
            (*v)--;
            if (*v != 0) {
                c->jump = t->loop_ret[d];
                t->loop_depth++;
                return RET(ACT_JUMP_ABS, 0);
            }
            return 1;
        }
        case 0x17: { /* call */
            bvm_track* t = cur_track(c);
            if (t->call_depth >= 4) {
                return RET(ACT_ERROR, 0);
            }
            t->call_ret[t->call_depth++] = c->pc + 5;
            c->jump = op_rel(c, 1);
            return RET(ACT_JUMP_ABS, 0);
        }
        case 0x18: { /* return */
            bvm_track* t = cur_track(c);
            if (t->call_depth <= 0) {
                return RET(ACT_ERROR, 0);
            }
            c->jump = t->call_ret[--t->call_depth];
            return RET(ACT_JUMP_ABS, 0);
        }
        case 0x19: { /* start_track n rel */
            unsigned n = op_u8(c, 1) & 0xff;
            if (n >= BVM_TRACKS) {
                return RET(ACT_ERROR, 0);
            }
            memset(&o->track[n], 0, sizeof(bvm_track));
            o->track[n].active = 1;
            o->track[n].wait = 1;
            o->track[n].pc = op_rel(c, 2);
            return 6;
        }
        case 0x1a: /* stop this track */
            cur_track(c)->active = 0;
            return RET(ACT_END_TRACK, 0);
        case 0x1b: { /* stop_track n */
            unsigned n = op_u8(c, 1) & 0xff;
            if (n < BVM_TRACKS) {
                o->track[n].active = 0;
            }
            return 2;
        }
        case 0x20: /* set var = v */
            o->var[bios_rom_u32(c->vm->rom, c->pc + 5) & 0xf] = op_value(c, 1);
            return 9;
        case 0x21: { /* arith */
            int32_t d = op_value(c, 2);
            int32_t* v = &o->var[bios_rom_u32(c->vm->rom, c->pc + 6) & 0xf];
            switch (op_u8(c, 1)) {
                case 0: *v += d; break;
                case 1: *v -= d; break;
                case 2: *v *= d; break;
                case 3: *v = d ? *v / d : 0; break;
                case 4:
                case 5: *v = (d < 0) ? (*v >> ((~d & 0x1f) + 1)) : (int32_t)((uint32_t)*v << (d & 0x1f)); break;
                case 6: *v &= d; break;
                case 7: *v |= d; break;
                case 8: *v ^= d; break;
                default: break;
            }
            return 10;
        }
        case 0x22: /* jz */
            if (op_value(c, 1) == 0) {
                c->jump = op_rel(c, 5);
                return RET(ACT_JUMP_ABS, 0);
            }
            return 9;
        case 0x23: /* jnz */
            if (op_value(c, 1) != 0) {
                c->jump = op_rel(c, 5);
                return RET(ACT_JUMP_ABS, 0);
            }
            return 9;
        case 0x24: { /* if a cond b -> rel */
            int32_t a = op_value(c, 2), b = op_value(c, 6);
            int hit = 0;
            switch (op_u8(c, 1)) {
                case 0: hit = a == b; break;
                case 1: hit = a != b; break;
                case 2: hit = b < a; break;
                case 3: hit = a <= b; break;
                case 4: hit = (uint32_t)b < (uint32_t)a; break;
                case 5: hit = (uint32_t)a <= (uint32_t)b; break;
                default: break;
            }
            if (hit) {
                c->jump = op_rel(c, 10);
                return RET(ACT_JUMP_ABS, 0);
            }
            return 14;
        }
        case 0x25: { /* render flag */
            static const struct {
                uint16_t key;
                uint8_t bit;
            } map[] = {{0, 8}, {1, 9}, {2, 11}, {3, 10}, {5, 4}, {6, 16}, {9, 20}, {10, 13}};
            uint16_t key = op_u16(c, 1);
            for (unsigned i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
                if (map[i].key == key) {
                    if (op_u8(c, 3)) {
                        o->flags |= 1u << map[i].bit;
                    } else {
                        o->flags &= ~(1u << map[i].bit);
                    }
                    break;
                }
            }
            return 4;
        }
        case 0x27: { /* set model / motion / texlist */
            uint16_t v = (uint16_t)op_value(c, 2);
            switch (op_u8(c, 1)) {
                case 0: o->model = v; o->texlist = v; break;
                case 1: o->motion = v; break;
                case 2: o->texlist = v; break;
                default: break;
            }
            return 6;
        }
        case 0x2b: /* effect */
            if (c->vm->host.effect) {
                c->vm->host.effect(c->vm->host.user, c->vm, o, op_u8(c, 1), op_u8(c, 2));
            }
            return 3;
        case 0x30: { /* spawn */
            bvm_obj* child = alloc_obj(c->vm, op_u16(c, 5), (int16_t)op_u16(c, 7), op_rel(c, 1));
            if (child) {
                for (int a = 0; a < 3; a++) {
                    child->pos_tw[a].cur = o->pos_tw[a].cur;
                }
            }
            return 9;
        }
        case 0x31: /* kill id */
            bvm_kill(c->vm, op_u16(c, 1));
            return 3;
        case 0x35: { /* attach_to */
            bvm_obj* p = bvm_find(c->vm, (uint16_t)op_value(c, 1));
            if (p) {
                o->parent = p;
                o->flags |= BVM_F_ATTACHED;
            }
            return 5;
        }
        case 0x36: /* detach */
            o->flags &= ~(uint32_t)BVM_F_ATTACHED;
            return 1;
        case 0x37: /* colour */
            for (int i = 0; i < 4; i++) {
                o->color[i] = op_fixed(c, 1 + 4u * (uint32_t)i);
            }
            return 17;
        case 0x50: case 0x52: case 0x54: { /* pos / step / target x y z */
            for (int a = 0; a < 3; a++) {
                float v = op_fixed(c, 1 + 4u * (uint32_t)a);
                if (op == 0x50) o->pos_tw[a].cur = v;
                else if (op == 0x52) o->pos_tw[a].step = v;
                else o->pos_tw[a].target = v;
            }
            return 13;
        }
        case 0x56: /* pos frames x y z */
            for (int a = 0; a < 3; a++) {
                o->pos_tw[a].frames = (int16_t)op_value(c, 1 + 4u * (uint32_t)a);
            }
            return 13;
        case 0x51: case 0x53: case 0x55: case 0x57: { /* single axis forms */
            unsigned axis = op_u8(c, 1);
            if (axis < 3) {
                if (op == 0x57) o->pos_tw[axis].frames = (int16_t)op_value(c, 2);
                else if (op == 0x51) o->pos_tw[axis].cur = op_fixed(c, 2);
                else if (op == 0x53) o->pos_tw[axis].step = op_fixed(c, 2);
                else o->pos_tw[axis].target = op_fixed(c, 2);
            }
            return 6;
        }
        case 0x58: /* scale */
            for (int a = 0; a < 3; a++) {
                o->scale_tw[a].cur = op_fixed(c, 1 + 4u * (uint32_t)a);
            }
            return 13;
        case 0x60: case 0x62: case 0x64: /* rot / step / target */
            for (int a = 0; a < 3; a++) {
                int32_t v = op_value(c, 1 + 4u * (uint32_t)a);
                if (op == 0x60) o->rot_tw[a].cur = v;
                else if (op == 0x62) o->rot_tw[a].step = v;
                else o->rot_tw[a].target = v;
            }
            return 13;
        case 0x66: /* rot frames */
            for (int a = 0; a < 3; a++) {
                o->rot_tw[a].frames = (int16_t)op_value(c, 1 + 4u * (uint32_t)a);
            }
            return 13;
        case 0x61: case 0x63: case 0x65: case 0x67: {
            unsigned axis = op_u8(c, 1);
            if (axis < 3) {
                int32_t v = op_value(c, 2);
                if (op == 0x67) o->rot_tw[axis].frames = (int16_t)v;
                else if (op == 0x61) o->rot_tw[axis].cur = v;
                else if (op == 0x63) o->rot_tw[axis].step = v;
                else o->rot_tw[axis].target = v;
            }
            return 6;
        }
        case 0x68: /* motion frame: start, step, end */
            o->motion_tw.cur = op_fixed(c, 1);
            o->motion_tw.step = op_fixed(c, 5);
            o->motion_tw.target = op_fixed(c, 9);
            return 13;
        case 0x69: { /* one motion frame field */
            float v = op_fixed(c, 2);
            switch (op_u8(c, 1)) {
                case 0: o->motion_tw.cur = v; break;
                case 1: o->motion_tw.step = v; break;
                case 2: o->motion_tw.target = v; break;
                default: break;
            }
            return 6;
        }
        /* text surface (content is drawn by the host) */
        case 0x70:
            o->text_w = op_value(c, 1);
            o->text_h = op_value(c, 5);
            return 9;
        case 0x71: o->text_w = o->text_h = 0; return 1;
        case 0x72: o->text_attr = op_u16(c, 1); return 3;
        case 0x73:
            if (op_u8(c, 1) == 0) o->text_colour = op_u16(c, 2);
            else o->text_bg = op_u16(c, 2);
            return 4;
        case 0x74:
            o->text_adv_wide = op_value(c, 1);
            o->text_adv_narrow = op_value(c, 5);
            return 9;
        case 0x75: return 1; /* clear: nothing to keep */
        case 0x7b:
            o->text_off[0] = op_fixed(c, 1);
            o->text_off[1] = op_fixed(c, 5);
            o->text_off[2] = op_fixed(c, 9);
            return 13;
        /* grid meshes */
        case 0x80:
            o->mesh_cw = op_value(c, 1);
            o->mesh_ch = op_value(c, 5);
            o->mesh_cols = op_value(c, 9);
            o->mesh_rows = op_value(c, 13);
            return 17;
        case 0x81: o->mesh_cols = o->mesh_rows = 0; return 1;
        case 0x83: return 1;
        case 0x84: o->mesh_tex = op_value(c, 1); return 5;
        case 0x85: o->mesh_mode = op_u8(c, 1); return 2;
        /* window panels */
        case 0x86: o->panel_w = op_value(c, 1); o->panel_h = op_value(c, 5); return 9;
        case 0x87: o->panel_w = o->panel_h = 0; return 1;
        case 0x88: o->rect_w = op_value(c, 1); o->rect_h = op_value(c, 5); return 9;
        default: break;
    }
    return RET(ACT_ERROR, 0);
}

/* script_exec: run one track until it yields. Returns non-zero when the object ended. */
static int
script_exec(ctx* c) {
    for (int budget = 0; budget < BVM_STEP_BUDGET; budget++) {
        if (c->pc >= c->vm->rom->size) {
            c->vm->error = 1;
            c->vm->error_pc = c->pc;
            c->stop = 1;
            return 0;
        }
        uint32_t r = exec_one(c);
        switch (r >> 28) {
            case ACT_NEXT: c->pc += r & 0x0FFFFFFF; break;
            case ACT_YIELD: c->pc += r & 0x0FFFFFFF; return 0;
            case 2: c->pc += c->jump; break;
            case ACT_JUMP_ABS: c->pc = c->jump; break;
            case ACT_END_TRACK: return (int)(r & 0x0FFFFFFF);
            default: /* error or unknown */
                c->vm->error = 2;
                c->vm->error_pc = c->pc;
                c->stop = 1;
                return 0;
        }
    }
    c->vm->error = 3; /* ran away without yielding */
    c->vm->error_pc = c->pc;
    c->stop = 1;
    return 0;
}

/* ---- Per frame update ------------------------------------------------------------ */

static void
step_ftween(bvm_ftween* t) {
    if (t->frames != 0) {
        int16_t f = t->frames--;
        t->step = (t->target - t->cur) / (float)f;
        if (t->frames == 0) {
            t->step = 0.0f;
            t->cur = t->target;
        }
    }
}

static void
step_itween(bvm_itween* t) {
    if (t->frames != 0) {
        int16_t f = t->frames--;
        t->step = ((int32_t)(int16_t)t->target - (int32_t)(int16_t)t->cur) / f;
        if (t->frames == 0) {
            t->step = 0;
            t->cur = t->target;
        }
    }
}

static void
update_object(bvm* vm, bvm_obj* o) {
    ctx c;
    memset(&c, 0, sizeof(c));
    c.vm = vm;
    c.obj = o;

    for (int t = 0; t < BVM_TRACKS; t++) {
        bvm_track* tr = &o->track[t];
        if (tr->active && --tr->wait == 0) {
            c.track_idx = t;
            c.pc = tr->pc;
            c.stop = 0;
            if (script_exec(&c)) {
                return; /* the object deleted itself */
            }
            tr->pc = c.pc;
            tr->wait = (int16_t)c.stop;
            if (tr->wait == 0 && tr->active) {
                tr->wait = 1; /* a track that asked for no wait still runs next frame */
            }
        }
    }

    for (int a = 0; a < 3; a++) {
        step_ftween(&o->pos_tw[a]);
        o->pos_tw[a].cur += o->pos_tw[a].step;
        o->pos[a] = o->pos_tw[a].cur;
    }
    for (int a = 0; a < 3; a++) {
        step_itween(&o->rot_tw[a]);
        o->rot_tw[a].cur += o->rot_tw[a].step;
        o->rot[a] = o->rot_tw[a].cur;
    }
    for (int a = 0; a < 3; a++) {
        step_ftween(&o->scale_tw[a]);
        o->scale_tw[a].cur += o->scale_tw[a].step;
    }

    o->motion_tw.cur += o->motion_tw.step;
    if (o->motion_tw.target <= o->motion_tw.cur) {
        o->motion_tw.cur = (float)o->var[7];
    }
    if (o->motion_tw.cur < 0.0f) {
        o->motion_tw.cur += o->motion_tw.target;
    }

    if ((o->flags & BVM_F_ATTACHED) && o->parent) {
        for (int a = 0; a < 3; a++) {
            o->pos[a] += o->parent->pos[a];
        }
    }
}

void
bvm_update(bvm* vm) {
    /* order[] can change while scripts spawn and kill objects: iterate over a snapshot */
    int snapshot[BVM_MAX_OBJECTS];
    int n = vm->count;
    memcpy(snapshot, vm->order, sizeof(int) * (size_t)n);
    for (int i = 0; i < n; i++) {
        bvm_obj* o = &vm->objs[snapshot[i]];
        if (o->active) {
            update_object(vm, o);
        }
    }
}
