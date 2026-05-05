#ifndef LORA_E32_H
#define LORA_E32_H

#include <stdint.h>
#include <stddef.h>
#include "config.h"


// ── Packet structures khớp 100% với Node ESP32 ───────────────
typedef struct __attribute__((packed)) {
    uint16_t index;
    uint16_t total;
    uint8_t  len;
    uint8_t  data[CHUNK_DATA_SIZE];
} LoraChunk;   // 55 bytes

typedef struct __attribute__((packed)) {
    uint8_t  status;   // 0xAA = OK, 0xFF = NACK
    uint16_t index;
} AckPkt;      // 3 bytes

// ── API ───────────────────────────────────────────────────────
int  lora_open(const char* port, int baud);
void lora_close(int fd);
void lora_flush(int fd);

int  lora_send_chunk(int fd, uint16_t index, uint16_t total,
                     const uint8_t* data, uint8_t len);

// Returns: 0xAA=OK, 0xFF=NACK, -1=timeout/error
int  lora_wait_ack(int fd, uint16_t expected_index, int timeout_ms);

int  lora_read_bytes(int fd, uint8_t* buf, size_t len, int timeout_ms);
int  lora_write_bytes(int fd, const uint8_t* buf, size_t len);

#endif