#ifndef FILE_H
#define FILE_H

#include <stdint.h>
#include <stddef.h>

uint8_t* file_read_all(const char* path, size_t* out_size);
uint32_t crc32_calc(const uint8_t* data, size_t len);
void     file_free(uint8_t* buf);

#endif






// xu ly file.bin