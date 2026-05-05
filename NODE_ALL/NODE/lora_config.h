// lora_config.h
#ifndef LORA_CONFIG_H
#define LORA_CONFIG_H

#include <Arduino.h>
#include "LoRa_E32.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"

#define LORA_TX_PIN   34
#define LORA_RX_PIN   25
#define LORA_BAUD     9600
#define NODE_ADDH     0x00
#define NODE_ADDL     0x00
#define GW_ADDH       0x00
#define GW_ADDL       0x01
#define LORA_CH       20
#define CHUNK_SIZE    50

typedef struct __attribute__((packed)) {
    uint16_t index;
    uint16_t total;
    uint8_t  len;
    uint8_t  data[CHUNK_SIZE];
} LoraChunk;

typedef struct __attribute__((packed)) {
    uint8_t  status;
    uint16_t index;
} AckPkt;

typedef enum {
    STREAM_TYPE_PART,   // đọc từ flash partition
    STREAM_TYPE_OTA,    // ghi vào OTA
} StreamType;

typedef struct {
    StreamType type;
    // Partition stream
    const esp_partition_t* partition;
    long int               offset;
    size_t                 part_size;
    // OTA stream
    esp_ota_handle_t       ota_handle;
    uint8_t                ota_buf[512];
    size_t                 ota_buf_pos;
    long int               pos;
} UnifiedStream;

#define JANPATCH_STREAM UnifiedStream

#endif