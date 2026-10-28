#ifndef NP_VISUAL_H
#define NP_VISUAL_H
#include "settings.h"
typedef struct NP_IMAGE {
    HBITMAP bitmap;
    UINT width, height;
    BOOL opaque;
} NP_IMAGE;
typedef struct NP_SURFACE {
    HDC dc;
    HBITMAP bitmap;
    HGDIOBJ old;
    int width, height;
    void *pixels;
} NP_SURFACE;
BOOL np_surface_size(NP_SURFACE *, int, int);
void np_surface_free(NP_SURFACE *);
BOOL np_image_file(const WCHAR *, NP_IMAGE *);
BOOL np_image_resource(int, NP_IMAGE *);
void np_image_free(NP_IMAGE *);
void np_image_draw(HDC, const RECT *, const NP_IMAGE *, int);
void np_background(HDC, const RECT *, const NP_SETTINGS *, const NP_COLORS *, const NP_IMAGE *);
#endif
