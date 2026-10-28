#include "visual.h"
#include <objbase.h>
#include <stdio.h>
#pragma pack(push, 1)
typedef struct ENTRY {
    BYTE w, h, colors, reserved;
    WORD planes, bpp;
    DWORD bytes, offset;
} ENTRY;
#pragma pack(pop)
int wmain(int argc, WCHAR **argv) {
    if (argc != 3)
        return 2;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    NP_IMAGE image = {0};
    if (!np_image_file(argv[1], &image))
        return 1;
    const int sizes[] = {16, 24, 32, 48, 64, 128, 256};
    WORD header[] = {0, 1, 7};
    ENTRY entries[7] = {0};
    DWORD offset = 6 + sizeof(entries);
    BYTE *blocks[7] = {0};
    for (int k = 0; k < 7; ++k) {
        int n = sizes[k];
        DWORD mask = (DWORD)((n + 31) / 32) * 4 * (DWORD)n;
        DWORD bytes = 40 + (DWORD)n * (DWORD)n * 4 + mask;
        BYTE *block = calloc(1, bytes);
        blocks[k] = block;
        BITMAPINFOHEADER *bi = (BITMAPINFOHEADER *)block;
        bi->biSize = 40;
        bi->biWidth = n;
        bi->biHeight = n * 2;
        bi->biPlanes = 1;
        bi->biBitCount = 32;
        BITMAPINFO info = {0};
        info.bmiHeader = *bi;
        info.bmiHeader.biHeight = n;
        void *pixels;
        HBITMAP b = CreateDIBSection(NULL, &info, DIB_RGB_COLORS, &pixels, NULL, 0);
        HDC dc = CreateCompatibleDC(NULL);
        HGDIOBJ old = SelectObject(dc, b);
        ZeroMemory(pixels, n * n * 4);
        if (n <= 24) {
            HBRUSH bg = CreateSolidBrush(RGB(127, 119, 222));
            RECT r = {0, 0, n, n};
            FillRect(dc, &r, bg);
            DeleteObject(bg);
            HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
            RECT port = {n / 4, n / 3, n * 3 / 4, n * 2 / 3};
            FillRect(dc, &port, white);
            port = (RECT){n * 3 / 8, n * 2 / 3, n * 5 / 8, n * 4 / 5};
            FillRect(dc, &port, white);
            DeleteObject(white);
            for (int p = 0; p < n * n; ++p)
                ((BYTE *)pixels)[p * 4 + 3] = 255;
        } else {
            DIBSECTION ds;
            GetObjectW(image.bitmap, sizeof(ds), &ds);
            BYTE *source =
                ds.dsBm.bmBits; /* GDI stretching drops alpha; sample premultiplied WIC pixels explicitly. */
            for (int y = 0; y < n; ++y)
                for (int x = 0; x < n; ++x) {
                    double sx = (x + 0.5) * image.width / n - 0.5, sy = (y + 0.5) * image.height / n - 0.5;
                    int ix = (int)sx, iy = (int)sy;
                    if (ix < 0)
                        ix = 0;
                    if (iy < 0)
                        iy = 0;
                    int jx = ix + 1 < (int)image.width ? ix + 1 : ix,
                        jy = iy + 1 < (int)image.height ? iy + 1 : iy;
                    double fx = sx - ix, fy = sy - iy;
                    BYTE *dest = (BYTE *)pixels + ((n - 1 - y) * n + x) * 4;
                    for (int c = 0; c < 4; ++c) {
                        double v = source[(iy * image.width + ix) * 4 + c] * (1 - fx) * (1 - fy) +
                                   source[(iy * image.width + jx) * 4 + c] * fx * (1 - fy) +
                                   source[(jy * image.width + ix) * 4 + c] * (1 - fx) * fy +
                                   source[(jy * image.width + jx) * 4 + c] * fx * fy;
                        dest[c] = (BYTE)(v + 0.5);
                    }
                    if (dest[3])
                        for (int c = 0; c < 3; ++c) {
                            unsigned v = dest[c] * 255u / dest[3];
                            dest[c] = (BYTE)(v > 255 ? 255 : v);
                        }
                }
        }
        memcpy(block + 40, pixels, n * n * 4);
        BYTE *andmask = block + 40 + n * n * 4;
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
                if (!((BYTE *)pixels)[(y * n + x) * 4 + 3])
                    andmask[y * ((n + 31) / 32) * 4 + x / 8] |= (BYTE)(0x80 >> (x % 8));
        SelectObject(dc, old);
        DeleteObject(b);
        DeleteDC(dc);
        entries[k] = (ENTRY){(BYTE)n, (BYTE)n, 0, 0, 1, 32, bytes, offset};
        offset += bytes;
    }
    FILE *f = NULL;
    _wfopen_s(&f, argv[2], L"wb");
    if (!f)
        return 1;
    fwrite(header, 1, 6, f);
    fwrite(entries, 1, sizeof(entries), f);
    for (int i = 0; i < 7; ++i) {
        fwrite(blocks[i], 1, entries[i].bytes, f);
        free(blocks[i]);
    }
    fclose(f);
    np_image_free(&image);
    CoUninitialize();
    return 0;
}
