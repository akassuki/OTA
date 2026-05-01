#include <Arduino.h>

#define CHUNK_SIZE 52

/* ======= PACKETS ======= */
typedef struct __attribute__((packed)) {
    uint8_t  type;
    uint32_t file_size;
    uint16_t total_chunks;
    uint32_t crc32;
} PktStart;

typedef struct __attribute__((packed)) {
    uint8_t  type;
    uint16_t index;
    uint8_t  len;
    uint8_t  data[CHUNK_SIZE];
} PktChunk;

typedef struct __attribute__((packed)) {
    uint8_t  type;
    uint32_t crc32;
} PktEnd;

/* ======= GỬI RESPONSE ======= */
void linuxSend(const char* msg) {
    Serial.println(msg);
    Serial.flush();
}

void linuxSendf(const char* fmt, ...) {
    char buf[64];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Serial.println(buf);
    Serial.flush();
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


/* ======= HANDLE START ======= */
void handleStart(String& line) {
    int p1 = line.indexOf(':');
    int p2 = line.indexOf(':', p1 + 1);
    int p3 = line.indexOf(':', p2 + 1);

    uint32_t fileSize    = (uint32_t)line.substring(p1 + 1, p2).toInt();
    uint16_t totalChunks = (uint16_t)line.substring(p2 + 1, p3).toInt();
    uint32_t crc32       = (uint32_t)strtoul(
                               line.substring(p3 + 1).c_str(), nullptr, 16);

    (void)fileSize;
    (void)totalChunks;
    (void)crc32;

    // Không cần LoRa, reply READY ngay
    linuxSend("READY");
}

/* ======= HANDLE CHUNK ======= */
void handleChunk(String& header) {
    // Parse: CHUNK:index:len:CRC32
    int p1 = header.indexOf(':');
    int p2 = header.indexOf(':', p1 + 1);
    int p3 = header.indexOf(':', p2 + 1);

    uint16_t idx        = (uint16_t)header.substring(p1 + 1, p2).toInt();
    uint8_t  len        = (uint8_t)header.substring(p2 + 1, p3).toInt();
    uint32_t crc_expect = (uint32_t)strtoul(
                              header.substring(p3 + 1).c_str(), nullptr, 16);

    // Đọc raw bytes
    uint8_t buf[CHUNK_SIZE];
    memset(buf, 0, sizeof(buf));
    size_t received = 0;
    unsigned long t = millis();

    while (received < len && millis() - t < 5000) {
        if (Serial.available()) {
            buf[received++] = (uint8_t)Serial.read();
        }
    }

    // Kiểm tra đủ bytes
    if (received != len) {
        linuxSendf("ERROR:CHUNK_%u_INCOMPLETE", idx);
        return;
    }

    // Kiểm tra CRC32
    uint32_t crc_actual = crc32_calc(buf, len);
    if (crc_actual != crc_expect) {
        linuxSendf("ERROR:CHUNK_%u_CRC_FAIL", idx);
        return;
    }

    linuxSendf("OK:%u:%08X", idx, crc_actual);
}

/* ======= HANDLE END ======= */
void handleEnd(String& line) {
    uint32_t crc = (uint32_t)strtoul(
        line.substring(line.indexOf(':') + 1).c_str(), nullptr, 16);

    // Reply kèm CRC32 file để Linux verify
    linuxSendf("DONE:%08X", crc);
}

/* ======= SETUP ======= */
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.flush();
    linuxSend("BOOT_OK"); 
}

/* ======= LOOP ======= */
String lineBuf = "";

void loop() {
    while (Serial.available()) {
        char c = (char)Serial.read();

        if (c == '\n') {
            lineBuf.trim();

            if (lineBuf.length() > 0) {
                if (lineBuf.startsWith("START:")) {
                    handleStart(lineBuf);
                } else if (lineBuf.startsWith("CHUNK:")) {
                    handleChunk(lineBuf);
                } else if (lineBuf.startsWith("END:")) {
                    handleEnd(lineBuf);
                }
            }

            lineBuf = "";

        } else if (c != '\r') {
            lineBuf += c;
        }
    }
    delay(1);
}