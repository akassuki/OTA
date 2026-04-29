#include "file.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8_t* file_read_all(const char* path, size_t* out_size) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "File read error: Cannot open file: %s\n", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    size_t size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t* buf = (uint8_t*)malloc(size);
    if (!buf) {
        fprintf(stderr, "File read error: malloc failed\n");
        fclose(f);
        return NULL;
    }

    size_t rd = fread(buf, 1, size, f);
    fclose(f);

    if (rd != size) {
        fprintf(stderr, "File read incomplete: %zu/%zu\n", rd, size);
        free(buf);
        return NULL;
    }

    *out_size = size;
    return buf;
}

uint32_t crc32_calc(const uint8_t* data, size_t len) {
    uint32_t crc = ~0u;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1));
    }
    return ~crc;
}

void file_free(uint8_t* buf) {
    if (buf) free(buf);
}