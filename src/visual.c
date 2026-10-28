#define COBJMACROS
#include "visual.h"
#include <wincodec.h>
#include <objbase.h>
void np_surface_free(NP_SURFACE *s) {
    if (s->dc) {
        SelectObject(s->dc, s->old);
        DeleteObject(s->bitmap);
        DeleteDC(s->dc);
    }
    ZeroMemory(s, sizeof(*s));
}
BOOL np_surface_size(NP_SURFACE *s, int width, int height) {
    if (width <= 0 || height <= 0 || (ULONGLONG)width * height > 32000000)
        return FALSE;
    if (s->dc && s->width == width && s->height == height)
        return TRUE;
    NP_SURFACE n = {0};
    BITMAPINFO b = {0};
    b.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    b.bmiHeader.biWidth = width;
    b.bmiHeader.biHeight = -height;
    b.bmiHeader.biPlanes = 1;
    b.bmiHeader.biBitCount = 32;
    b.bmiHeader.biCompression = BI_RGB;
    n.dc = CreateCompatibleDC(NULL);
    n.bitmap = CreateDIBSection(NULL, &b, DIB_RGB_COLORS, &n.pixels, NULL, 0);
    if (!n.dc || !n.bitmap) {
        if (n.dc)
            DeleteDC(n.dc);
        if (n.bitmap)
            DeleteObject(n.bitmap);
        return FALSE;
    }
    n.old = SelectObject(n.dc, n.bitmap);
    n.width = width;
    n.height = height;
    np_surface_free(s);
    *s = n;
    return TRUE;
}
void np_image_free(NP_IMAGE *i) {
    if (i->bitmap)
        DeleteObject(i->bitmap);
    ZeroMemory(i, sizeof(*i));
}
static BOOL decode(IWICBitmapDecoder *d, NP_IMAGE *out) {
    IWICBitmapFrameDecode *frame = NULL;
    IWICFormatConverter *convert = NULL;
    IWICImagingFactory *factory = NULL;
    UINT w = 0, h = 0;
    BOOL ok = FALSE;
    if (SUCCEEDED(IWICBitmapDecoder_GetFrame(d, 0, &frame)) &&
        SUCCEEDED(IWICBitmapFrameDecode_GetSize(frame, &w, &h)) && w && h && (ULONGLONG)w * h <= 16000000 &&
        SUCCEEDED(CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                                   &IID_IWICImagingFactory, (void **)&factory)) &&
        SUCCEEDED(IWICImagingFactory_CreateFormatConverter(factory, &convert)) &&
        SUCCEEDED(IWICFormatConverter_Initialize(convert, (IWICBitmapSource *)frame,
                                                 &GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
                                                 NULL, 0, WICBitmapPaletteTypeCustom))) {
        BITMAPINFO bi = {0};
        void *pixels;
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = (LONG)w;
        bi.bmiHeader.biHeight = -(LONG)h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        HBITMAP b = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &pixels, NULL, 0);
        if (b && SUCCEEDED(IWICFormatConverter_CopyPixels(convert, NULL, w * 4, w * h * 4, pixels))) {
            np_image_free(out);
            out->bitmap = b;
            out->width = w;
            out->height = h;
            out->opaque = TRUE;
            for (size_t i = 0; i < (size_t)w * h; ++i)
                if (((DWORD *)pixels)[i] >> 24 != 255) {
                    out->opaque = FALSE;
                    break;
                }
            ok = TRUE;
        } else if (b)
            DeleteObject(b);
    }
    if (convert)
        IWICFormatConverter_Release(convert);
    if (frame)
        IWICBitmapFrameDecode_Release(frame);
    if (factory)
        IWICImagingFactory_Release(factory);
    return ok;
}
BOOL np_image_file(const WCHAR *file, NP_IMAGE *out) {
    IWICImagingFactory *f = NULL;
    IWICBitmapDecoder *d = NULL;
    BOOL ok = FALSE;
    if (SUCCEEDED(CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                                   &IID_IWICImagingFactory, (void **)&f)) &&
        SUCCEEDED(IWICImagingFactory_CreateDecoderFromFilename(f, file, NULL, GENERIC_READ,
                                                               WICDecodeMetadataCacheOnLoad, &d)))
        ok = decode(d, out);
    if (d)
        IWICBitmapDecoder_Release(d);
    if (f)
        IWICImagingFactory_Release(f);
    return ok;
}
BOOL np_image_resource(int id, NP_IMAGE *out) {
    HRSRC r = FindResourceW(NULL, MAKEINTRESOURCEW(id), RT_RCDATA);
    HGLOBAL m = r ? LoadResource(NULL, r) : NULL;
    BYTE *data = m ? LockResource(m) : NULL;
    IWICImagingFactory *f = NULL;
    IWICStream *s = NULL;
    IWICBitmapDecoder *d = NULL;
    BOOL ok = FALSE;
    if (data &&
        SUCCEEDED(CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                                   &IID_IWICImagingFactory, (void **)&f)) &&
        SUCCEEDED(IWICImagingFactory_CreateStream(f, &s)) &&
        SUCCEEDED(IWICStream_InitializeFromMemory(s, data, SizeofResource(NULL, r))) &&
        SUCCEEDED(IWICImagingFactory_CreateDecoderFromStream(f, (IStream *)s, NULL,
                                                             WICDecodeMetadataCacheOnLoad, &d)))
        ok = decode(d, out);
    if (d)
        IWICBitmapDecoder_Release(d);
    if (s)
        IWICStream_Release(s);
    if (f)
        IWICImagingFactory_Release(f);
    return ok;
}
void np_image_draw(HDC dc, const RECT *r, const NP_IMAGE *i, int mode) {
    if (!i->bitmap)
        return;
    int rw = r->right - r->left, rh = r->bottom - r->top;
    if (rw <= 0 || rh <= 0)
        return;
    int dw = rw, dh = MulDiv(rw, (int)i->height, (int)i->width);
    if ((mode == 0 && dh > rh) || (mode == 1 && dh < rh)) {
        dh = rh;
        dw = MulDiv(rh, (int)i->width, (int)i->height);
    }
    if (mode == 2) {
        dw = (int)i->width;
        dh = (int)i->height;
    }
    int saved = SaveDC(dc);
    IntersectClipRect(dc, r->left, r->top, r->right, r->bottom);
    HDC mem = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(mem, i->bitmap);
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    if (i->opaque) {
        SetStretchBltMode(dc, HALFTONE);
        SetBrushOrgEx(dc, 0, 0, NULL);
        StretchBlt(dc, r->left + (rw - dw) / 2, r->top + (rh - dh) / 2, dw, dh, mem, 0, 0, (int)i->width,
                   (int)i->height, SRCCOPY);
    } else
        AlphaBlend(dc, r->left + (rw - dw) / 2, r->top + (rh - dh) / 2, dw, dh, mem, 0, 0, (int)i->width,
                   (int)i->height, blend);
    SelectObject(mem, old);
    DeleteDC(mem);
    RestoreDC(dc, saved);
}
void np_background(HDC dc, const RECT *r, const NP_SETTINGS *s, const NP_COLORS *c, const NP_IMAGE *image) {
    HBRUSH b = CreateSolidBrush(c->background);
    FillRect(dc, r, b);
    DeleteObject(b);
    if (s->background_mode == 1) {
        TRIVERTEX v[2] = {{r->left, r->top, (COLOR16)(GetRValue(c->background) << 8),
                           (COLOR16)(GetGValue(c->background) << 8), (COLOR16)(GetBValue(c->background) << 8),
                           0},
                          {r->right, r->bottom, (COLOR16)(GetRValue(c->accent) << 8),
                           (COLOR16)(GetGValue(c->accent) << 8), (COLOR16)(GetBValue(c->accent) << 8), 0}};
        GRADIENT_RECT g = {0, 1};
        GradientFill(dc, v, 2, &g, 1, GRADIENT_FILL_RECT_V);
    }
    if (s->background_mode == 2 && image->bitmap) {
        np_image_draw(dc, r, image, s->image_mode);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP one = CreateCompatibleBitmap(dc, 1, 1);
        HGDIOBJ old = SelectObject(mem, one);
        SetPixel(mem, 0, 0, c->background);
        BLENDFUNCTION blend = {AC_SRC_OVER, 0, (BYTE)(s->mask * 255 / 100), 0};
        AlphaBlend(dc, r->left, r->top, r->right - r->left, r->bottom - r->top, mem, 0, 0, 1, 1, blend);
        SelectObject(mem, old);
        DeleteObject(one);
        DeleteDC(mem);
    }
}
