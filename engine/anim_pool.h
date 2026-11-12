#pragma once
//----- Pool -----||
// Fixed-capacity pool. Live slots are listed in dense[] and removal is swap-and-pop
// on that list. Handles carry a generation so a stale index does not alias a new slot.
// No allocation. T is memset to 0 on spawn, so keep it a plain struct.

#include "anim_out.h"
#include <string.h>

template<typename T, int N>
struct Anim_pool {
    T item[N];
    uint16_t gen[N];
    uint8_t live[N];
    int dense[N];
    int n;
};

template<typename T, int N>
inline void anim_pool_init(Anim_pool<T, N> &p) {
    memset(&p, 0, sizeof(p));
}

template<typename T, int N>
inline Anim_handle anim_pool_spawn(Anim_pool<T, N> &p) {
    Anim_handle h = anim_handle_none();
    if (p.n >= N) return h;
    int slot = -1;
    for (int i = 0; i < N; i++) {
        if (!p.live[i]) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return h;
    p.live[slot] = 1;
    if (p.gen[slot] == 0) p.gen[slot] = 1;
    memset(&p.item[slot], 0, sizeof(T));
    p.dense[p.n++] = slot;
    h.index = (uint16_t)slot;
    h.gen = p.gen[slot];
    return h;
}

template<typename T, int N>
inline T *anim_pool_get(Anim_pool<T, N> &p, Anim_handle h) {
    if (h.gen == 0 || h.index >= N) return 0;
    if (!p.live[h.index] || p.gen[h.index] != h.gen) return 0;
    return &p.item[h.index];
}

template<typename T, int N>
inline int anim_pool_remove(Anim_pool<T, N> &p, Anim_handle h) {
    if (h.gen == 0 || h.index >= N) return 0;
    if (!p.live[h.index] || p.gen[h.index] != h.gen) return 0;
    p.live[h.index] = 0;
    p.gen[h.index] = (uint16_t)(p.gen[h.index] + 1);
    if (p.gen[h.index] == 0) p.gen[h.index] = 1;
    for (int i = 0; i < p.n; i++) {
        if (p.dense[i] == (int)h.index) {
            p.dense[i] = p.dense[p.n - 1];
            p.n--;
            break;
        }
    }
    return 1;
}
