#pragma once
#include <math.h>
#include <algorithm>
#include <stdio.h>

//#define max(a,b)            (((a) > (b)) ? (a) : (b))
//#define min(a,b)            (((a) < (b)) ? (a) : (b))
#define frame_max 4096
#define Pi 3.1415926535

#define section_max 10000

inline FILE* omlsa_fopen(const char* filename, const char* mode) {
#ifdef _MSC_VER
    FILE* file = nullptr;
    if (fopen_s(&file, filename, mode) != 0) { return nullptr; }
    return file;
#else
    return fopen(filename, mode);
#endif
}
