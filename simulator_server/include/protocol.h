#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

#define CHUNK_SIZE  52

/* ======= CRC32 ======= */
static inline uint32_t crc32_chunk(const uint8_t* data, size_t len) {
    uint32_t crc = ~0u;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1));
    }
    return ~crc;
}

int proto_send_start(int fd,
                     size_t   file_size,
                     uint16_t total_chunks,
                     uint32_t crc32);

int proto_send_chunk(int fd,
                     uint16_t       index,
                     const uint8_t* data,
                     uint8_t        len);

int proto_send_end(int fd, uint32_t crc32);

int proto_wait_response(int fd,
                        const char* expected,
                        int         timeout_ms);

#endif





//Linux                          Gateway ESP32
//  │                                  │
//   │── "START:281280:5410:4BAA2FE0:CRC32\n"─►│
//   │                                  │ parse file_size
//   │                                  │ parse total_chunks
//   │                                  │ parse crc32
//   │◄─────────────── "READY\n" ───────│
//   │                                  │
//   │── "CHUNK:0:52:CRC_CHUNK0\n" ───────────────►│
//   │── [52 raw bytes] ───────────────►│ đọc đủ 52 bytes
//   │◄─────────────── "OK:0\n" ────────│
//   │                                  │
//   │── "CHUNK:1:52:CRC_CHUNK1\n" ───────────────►│
//   │── [52 raw bytes] ───────────────►│
//   │◄─────────────── "OK:1\n" ────────│
//   │                                  │
//   │       ... 5408 chunks tiếp ...   │
//   │                                  │
//   │── "CHUNK:5409:32:CRC_CHUNK5409\n" ────────────►│ chunk cuối
//   │── [32 raw bytes] ───────────────►│ len < 52
//   │◄─────────────── "OK:5409\n" ─────│
//   │                                  │
//   │── "END:CRC_FILE\n" ─────────────►│ verify CRC32
//   │◄─────────────── "DONE\n" ────────│
