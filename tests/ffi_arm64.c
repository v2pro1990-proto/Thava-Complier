#include <stdint.h>

__declspec(dllexport) double mix(int64_t a, double b, int64_t c, float d) {
    return a + b + c + d;
}

__declspec(dllexport) int64_t ninth(int64_t a, int64_t b, int64_t c, int64_t d,
                                  int64_t e, int64_t f, int64_t g, int64_t h,
                                  int64_t i) {
    return a + b + c + d + e + f + g + h + i;
}

__declspec(dllexport) const char* label(void) {
    return "arm64";
}
