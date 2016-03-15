#pragma once

#include "globals.h"

namespace jpegEncoder {
    bool init();
    bool encode(const unsigned char *bufIn, size_t bufLenIn, unsigned char *bufOut, size_t *bufLenOut);
    void close();
}

