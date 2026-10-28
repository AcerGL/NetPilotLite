/* A signed test-only x64 product image that fails BEFORE reporting healthy startup. */
#include "lite.h"
int WINAPI wWinMain(HINSTANCE i, HINSTANCE p, PWSTR c, int s) {
    (void)i;
    (void)p;
    (void)c;
    (void)s;
    return ERROR_APP_INIT_FAILURE;
}
