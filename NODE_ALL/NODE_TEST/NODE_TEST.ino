#include <Arduino.h>
#include <LittleFS.h>
#include <LoRa_E32.h>

/* ======= CONFIG ======= */

#define LORA_RX_PIN   25
#define LORA_TX_PIN   34
#define LORA_CH       20
#define NODE_ADDH     0x00
#define NODE_ADDL     0x00
#define GW_ADDH       0x00
#define GW_ADDL       0x01

#define CHUNK_SIZE    52
#define SAVE_PATH     "/received.bin"

/* ======= LORA ======= */
HardwareSerial loraSerial(1);
LoRa_E32 e32(34, 25, &loraSerial, UART_BPS_RATE_9600);

/* ======= TRẠNG THÁI ======= */
struct RxState {
    bool     active         = false;
    uint16_t expected       = 0;
    uint16_t totalChunks    = 0;
    uint32_t fileSize       = 0;
    uint32_t crc_expected   = 0;
    uint32_t chunksReceived = 0;
    File     file;
};
RxState rx;

/* ======= CRC32 ======= */
uint32_t crc32_calc(const uint8_t* data, size_t len) {
    uint32_t crc = ~0u;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1));
    }
    return ~crc;
}

uint32_t crc32_file(const char* path) {
    File f = LittleFS.open(path, "r");
    if (!f) return 0;
    uint32_t crc = ~0u;
    uint8_t  buf[128];
    while (f.available()) {
        int rd = f.read(buf, sizeof(buf));
        for (int i = 0; i < rd; i++) {
            crc ^= buf[i];
            for (int j = 0; j < 8; j++)
                crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1));
        }
    }
    f.close();
    return ~crc;
}

/* ======= GỬI VỀ LINUX (qua LoRa → Gateway) ======= */
void loraSend(const char* msg) {
    e32.sendFixedMessage(GW_ADDH, GW_ADDL, LORA_CH,
                         msg, strlen(msg) + 1);
    Serial.printf("[TX] %s\n", msg);
}

void loraSendf(const char* fmt, ...) {
    char buf[64];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    e32.sendFixedMessage(GW_ADDH, GW_ADDL, LORA_CH,
                         buf, strlen(buf) + 1);
    Serial.printf("[TX] %s\n", buf);
}

/* ======= LOG HEX ======= */
void log_hex(const uint8_t* data, uint8_t len) {
    for (uint8_t i = 0; i < len; i++) {
        if (data[i] < 0x10) Serial.print("0");
        Serial.print(data[i], HEX);
        Serial.print(" ");
        if ((i + 1) % 16 == 0) Serial.println();
    }
    Serial.println();
}

/* ======= HANDLE START ======= */
void handleStart(String& line) {
    int p1 = line.indexOf(':');
    int p2 = line.indexOf(':', p1 + 1);
    int p3 = line.indexOf(':', p2 + 1);

    if (p1 < 0 || p2 < 0 || p3 < 0) {
        Serial.println("[ERROR] START parse fail");
        loraSend("ERROR:START_PARSE_FAIL");
        return;
    }

    uint32_t fileSize    = (uint32_t)line.substring(p1 + 1, p2).toInt();
    uint16_t totalChunks = (uint16_t)line.substring(p2 + 1, p3).toInt();
    uint32_t crc32       = (uint32_t)strtoul(
                               line.substring(p3 + 1).c_str(), nullptr, 16);

    Serial.println("\n==========================================");
    Serial.println("[RX] START");
    Serial.println("==========================================");
    Serial.printf("   file_size    : %lu bytes\n", fileSize);
    Serial.printf("   total_chunks : %u\n",         totalChunks);
    Serial.printf("   crc32        : 0x%08X\n",     crc32);
    Serial.println("==========================================");

    if (LittleFS.exists(SAVE_PATH)) {
        LittleFS.remove(SAVE_PATH);
        Serial.println("[FS] Xóa file cũ");
    }

    rx.file = LittleFS.open(SAVE_PATH, "w");
    if (!rx.file) {
        Serial.println("[ERROR] Không mở được file");
        loraSend("ERROR:FS_OPEN_FAIL");
        return;
    }

    rx.active         = true;
    rx.expected       = 0;
    rx.totalChunks    = totalChunks;
    rx.fileSize       = fileSize;
    rx.crc_expected   = crc32;
    rx.chunksReceived = 0;

    Serial.println("[OK] Sẵn sàng nhận");
    loraSend("READY");
}

/* ======= HANDLE CHUNK ======= */
void handleChunk(String& header) {
    if (!rx.active) {
        loraSend("ERROR:NO_START");
        return;
    }

    int p1 = header.indexOf(':');
    int p2 = header.indexOf(':', p1 + 1);
    int p3 = header.indexOf(':', p2 + 1);

    if (p1 < 0 || p2 < 0 || p3 < 0) {
        loraSend("ERROR:CHUNK_PARSE_FAIL");
        return;
    }

    uint16_t idx        = (uint16_t)header.substring(p1 + 1, p2).toInt();
    uint8_t  len        = (uint8_t)header.substring(p2 + 1, p3).toInt();
    uint32_t crc_expect = (uint32_t)strtoul(
                              header.substring(p3 + 1).c_str(), nullptr, 16);

    Serial.printf("[RX] CHUNK index=%u len=%u crc=0x%08X\n",
                  idx, len, crc_expect);

    // Kiểm tra thứ tự
    if (idx != rx.expected) {
        Serial.printf("[ERROR] Sai thứ tự: nhận %u chờ %u\n",
                      idx, rx.expected);
        loraSendf("ERROR:CHUNK_%u_ORDER", idx);
        return;
    }

    // Kiểm tra len
    if (len == 0 || len > CHUNK_SIZE) {
        loraSendf("ERROR:CHUNK_%u_LEN", idx);
        return;
    }

    // Đọc raw bytes từ LoRa
    uint8_t buf[CHUNK_SIZE];
    memset(buf, 0, sizeof(buf));
    size_t   received = 0;
    unsigned long t   = millis();

    while (received < len && millis() - t < 5000) {
        if (e32.available() > 0) {
            ResponseStructContainer rsc = e32.receiveMessage(1);
            if (rsc.status.code == SUCCESS)
                buf[received++] = *(uint8_t*)rsc.data;
            rsc.close();
        }
    }

    // Kiểm tra đủ bytes
    if (received != len) {
        Serial.printf("[ERROR] Thiếu bytes: %u/%u\n", received, len);
        loraSendf("ERROR:CHUNK_%u_INCOMPLETE", idx);
        return;
    }

    // Kiểm tra CRC32
    uint32_t crc_actual = crc32_calc(buf, len);
    if (crc_actual != crc_expect) {
        Serial.printf("[ERROR] CRC32 sai: 0x%08X != 0x%08X\n",
                      crc_actual, crc_expect);
        loraSendf("ERROR:CHUNK_%u_CRC_FAIL", idx);
        return;
    }

    // Ghi file
    if (rx.file.write(buf, len) != len) {
        loraSendf("ERROR:CHUNK_%u_WRITE_FAIL", idx);
        return;
    }

    Serial.printf("   HEX: ");
    log_hex(buf, len);

    rx.expected++;
    rx.chunksReceived++;

    Serial.printf("[OK] Chunk %u/%u\n", rx.chunksReceived, rx.totalChunks);
    loraSendf("OK:%u:%08X", idx, crc_actual);
}

/* ======= HANDLE END ======= */
void handleEnd(String& line) {
    if (!rx.active) {
        loraSend("ERROR:NO_START");
        return;
    }

    rx.file.close();
    rx.active = false;

    uint32_t crc_linux = (uint32_t)strtoul(
        line.substring(line.indexOf(':') + 1).c_str(), nullptr, 16);

    Serial.println("\n==========================================");
    Serial.println("[RX] END");
    Serial.println("==========================================");
    Serial.printf("   chunks : %u / %u\n",
                  rx.chunksReceived, rx.totalChunks);
    Serial.printf("   crc32  : 0x%08X\n", crc_linux);

    // Kiểm tra đủ chunks
    if (rx.chunksReceived != rx.totalChunks) {
        Serial.printf("[ERROR] Thiếu chunks: %u/%u\n",
                      rx.chunksReceived, rx.totalChunks);
        loraSend("ERROR:CHUNKS_MISSING");
        return;
    }

    // Tính CRC32 toàn file
    uint32_t crc_file = crc32_file(SAVE_PATH);
    Serial.printf("   crc32 file : 0x%08X\n", crc_file);

    if (crc_file != crc_linux) {
        Serial.println("[ERROR] CRC32 KHÔNG KHỚP");
        LittleFS.remove(SAVE_PATH);
        loraSend("ERROR:CRC_FILE_FAIL");
        return;
    }

    File check = LittleFS.open(SAVE_PATH, "r");
    uint32_t actualSize = check.size();
    check.close();

    Serial.println("[OK] CRC32 KHỚP");
    Serial.printf("[OK] File: %lu bytes\n", actualSize);
    Serial.println("==========================================\n");

    loraSendf("DONE:%08X", crc_file);
}

/* ======= SETUP ======= */
void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("==========================================");
    Serial.println("           NODE ESP32                    ");
    Serial.println("==========================================");
    Serial.printf("ADDR     : 0x%02X%02X\n", NODE_ADDH, NODE_ADDL);
    Serial.printf("GW ADDR  : 0x%02X%02X\n", GW_ADDH,   GW_ADDL);
    Serial.printf("Channel  : %d\n",          LORA_CH);
    Serial.println("==========================================");

    if (!LittleFS.begin(true)) {
        Serial.println("[ERROR] LittleFS thất bại");
        return;
    }
    Serial.println("[OK] LittleFS mounted");

    loraSerial.begin(9600, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);
    e32.begin();

    // Cấu hình Fixed Mode
    ResponseStructContainer csc = e32.getConfiguration();
    if (csc.status.code == SUCCESS) {
        Configuration cfg = *(Configuration*) csc.data;
        cfg.ADDH = NODE_ADDH;
        cfg.ADDL = NODE_ADDL;
        cfg.CHAN  = LORA_CH;
        cfg.OPTION.fixedTransmission = FT_FIXED_TRANSMISSION;
        e32.setConfiguration(cfg, WRITE_CFG_PWR_DWN_SAVE);
        Serial.println("[OK] LoRa configured");
    }
    csc.close();

    Serial.println("[OK] NODE SẴN SÀNG");
    Serial.println("==========================================\n");
}

/* ======= LOOP ======= */
String lineBuf = "";

void loop() {
    if (e32.available() > 0) {
        ResponseStructContainer rsc = e32.receiveMessage(64);
        if (rsc.status.code == SUCCESS) {
            char* raw = (char*)rsc.data;
            lineBuf += String(raw);

            // Xử lý từng dòng
            int idx;
            while ((idx = lineBuf.indexOf('\n')) >= 0) {
                String line = lineBuf.substring(0, idx);
                line.trim();
                lineBuf = lineBuf.substring(idx + 1);

                if (line.length() == 0) continue;

                Serial.printf("[RX RAW] %s\n", line.c_str());

                if      (line.startsWith("START:")) handleStart(line);
                else if (line.startsWith("CHUNK:")) handleChunk(line);
                else if (line.startsWith("END:"))   handleEnd(line);
                else Serial.printf("[WARN] Unknown: %s\n", line.c_str());
            }
        }
        rsc.close();
    }
    delay(1);
}