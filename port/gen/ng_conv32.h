/* NoGravity 64-bit port: convert 32-bit on-disk struct images (see ng_layout32.h). */
#pragma once
#include <string.h>
#include <stdint.h>
#include "ng_layout32.h"

static inline void NG_Convert32(const NG_LAYOUT32 *L, void *dst, const void *src, int n, unsigned limit32)
{
    int i; unsigned k;
    for (i = 0; i < n; i++)
    {
        unsigned char *d = (unsigned char *)dst + (size_t)i * L->size64;
        const unsigned char *s = (const unsigned char *)src + (size_t)i * L->size32;
        for (k = 0; k < L->n; k++)
        {
            const NG_FIELD32 *f = &L->f[k];
            if (f->o32 + f->s32 > limit32) continue;
            if (f->kind == 0) memcpy(d + f->o64, s + f->o32, f->s32);
            else
            {
                uint32_t v; memcpy(&v, s + f->o32, 4);
                if (f->kind == 1) { uint64_t w = v; memcpy(d + f->o64, &w, 8); }
                else { int64_t w = (int32_t)v; memcpy(d + f->o64, &w, 8); }
            }
        }
    }
}
