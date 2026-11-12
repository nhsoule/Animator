#pragma once
//----- Anim out -----||
// An update writes part transforms and, optionally, events. Events land in a
// fixed queue the caller owns. Apply the queue after the update loop.
// An update does not touch a sim. Glue (the scene, or the engine) fills the input struct.

#include "anim_math.h"
#include <stdint.h>
#include <string.h>

enum { ANIM_PART_MAX = 8 };
enum { ANIM_EVENT_MAX = 48 };
enum { ANIM_PAYLOAD = 4 };

enum {
    ANIM_EV_NONE = 0,
    ANIM_EV_SPAWN = 1,
    ANIM_EV_REMOVE = 2,
    ANIM_EV_CUSTOM = 3
};

struct Anim_handle {
    uint16_t index;
    uint16_t gen;
};

inline Anim_handle anim_handle_none() {
    Anim_handle h;
    h.index = 0;
    h.gen = 0;
    return h;
}

struct Anim_event {
    int kind;
    int type_id;
    uint16_t index;
    uint16_t gen;
    int custom_id;
    int payload_n;
    float payload[ANIM_PAYLOAD];
};

struct Anim_queue {
    Anim_event ev[ANIM_EVENT_MAX];
    int n;
};

inline void anim_queue_clear(Anim_queue &q) { q.n = 0; }

inline Anim_event *anim_queue_push(Anim_queue &q) {
    if (q.n >= ANIM_EVENT_MAX) return 0;
    Anim_event *e = &q.ev[q.n++];
    memset(e, 0, sizeof(*e));
    return e;
}

inline void anim_spawn(Anim_queue &q, int type_id, const float *payload, int n) {
    Anim_event *e = anim_queue_push(q);
    if (!e) return;
    e->kind = ANIM_EV_SPAWN;
    e->type_id = type_id;
    if (n > ANIM_PAYLOAD) n = ANIM_PAYLOAD;
    if (n < 0) n = 0;
    e->payload_n = n;
    for (int i = 0; i < n; i++) e->payload[i] = payload ? payload[i] : 0.f;
}

inline void anim_remove(Anim_queue &q, int type_id, Anim_handle h) {
    Anim_event *e = anim_queue_push(q);
    if (!e) return;
    e->kind = ANIM_EV_REMOVE;
    e->type_id = type_id;
    e->index = h.index;
    e->gen = h.gen;
}

inline void anim_emit(Anim_queue &q, int custom_id, const float *payload, int n) {
    Anim_event *e = anim_queue_push(q);
    if (!e) return;
    e->kind = ANIM_EV_CUSTOM;
    e->custom_id = custom_id;
    if (n > ANIM_PAYLOAD) n = ANIM_PAYLOAD;
    if (n < 0) n = 0;
    e->payload_n = n;
    for (int i = 0; i < n; i++) e->payload[i] = payload ? payload[i] : 0.f;
}

struct Anim_out {
    Transform part[ANIM_PART_MAX];
    uint8_t part_on[ANIM_PART_MAX];
    int part_n;
    Anim_queue *queue;
};

inline void anim_out_begin(Anim_out &o) {
    Anim_queue *q = o.queue;
    o.part_n = 0;
    o.queue = q;
    for (int i = 0; i < ANIM_PART_MAX; i++) {
        o.part[i] = xf_id();
        o.part_on[i] = 1;
    }
}

inline void anim_out_spawn(Anim_out &o, int type_id, const float *payload, int n) {
    if (o.queue) anim_spawn(*o.queue, type_id, payload, n);
}

inline void anim_out_remove(Anim_out &o, int type_id, Anim_handle h) {
    if (o.queue) anim_remove(*o.queue, type_id, h);
}

inline void anim_out_emit(Anim_out &o, int custom_id, const float *payload, int n) {
    if (o.queue) anim_emit(*o.queue, custom_id, payload, n);
}
