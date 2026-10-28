#ifndef NP_VISUAL_H
#define NP_VISUAL_H
#include "settings.h"
typedef struct NP_IMAGE {
    HBITMAP bitmap;
    UINT width, height;
} NP_IMAGE;
BOOL np_image_file(const WCHAR *, NP_IMAGE *);
BOOL np_image_resource(int, NP_IMAGE *);
void np_image_free(NP_IMAGE *);
void np_image_draw(HDC, const RECT *, const NP_IMAGE *, int);
void np_background(HDC, const RECT *, const NP_SETTINGS *, const NP_COLORS *, const NP_IMAGE *);
#endif
