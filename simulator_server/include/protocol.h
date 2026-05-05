#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include "config.h"


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





// LINUX                    GATEWAY (ESP32)                    NODE (ESP32)
//   │                           │                                  │
//   │──── START:size:chunks ───►│                                  │
//   │                           │ parse, reply READY               │
//   │◄─────── READY ────────────│                                  │
//   │                           │                                  │
//   │──── CHUNK:0:52:CRC ──────►│                                  │
//   │──── [52 bytes raw] ───────►│                                  │
//   │                           │ UartTask nhận, check CRC         │
//   │                           │ đưa vào pendingChunk             │
//   │                           │ give(chunkReady)                 │
//   │                           │ block tại take(chunkDone)        │
//   │                           │         │                        │
//   │                           │   LoRaTask take(chunkReady)      │
//   │                           │   sendFixedMessage ─────────────►│
//   │                           │                                  │ nhận chunk
//   │                           │                                  │ xử lý
//   │                           │◄──────────── ACK:OK:0 ───────────│
//   │                           │   acked = true                   │
//   │                           │   chunkResult = true             │
//   │                           │   give(chunkDone)                │
//   │                           │         │                        │
//   │                           │ UartTask unblock                 │
//   │◄─────── OK:0:CRC ─────────│ reply OK về Linux                │
//   │                           │                                  │
//   │──── CHUNK:1:52:CRC ──────►│  (lặp lại cho chunk tiếp)       │
//   │         ...               │                                  │
//   │                           │                                  │
//   │──── END:CRC_FILE ────────►│                                  │
//   │◄─────── DONE:CRC ─────────│                                  │



// LINUX                    GATEWAY                            NODE
//   │                           │                                  │
//   │──── CHUNK:5:52:CRC ──────►│                                  │
//   │                           │ give(chunkReady)                 │
//   │                           │ block(chunkDone)                 │
//   │                           │                                  │
//   │                           │ sendFixedMessage ───────────────►│ (mất gói)
//   │                           │ chờ ACK... timeout 3s            │
//   │                           │ retry lần 2 ────────────────────►│ (mất gói)
//   │                           │ retry lần 3 ────────────────────►│ (mất gói)
//   │                           │ acked = false                    │
//   │                           │ chunkResult = false              │
//   │                           │ give(chunkDone)                  │
//   │                           │                                  │
//   │◄── ERROR:CHUNK_5_LORA_FAIL│                                  │
//   │                           │                                  │
//   │ retry chunk 5             │                                  │
//   │──── CHUNK:5:52:CRC ──────►│ (bắt đầu lại từ đầu)            │