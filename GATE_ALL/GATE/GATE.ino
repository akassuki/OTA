#include <Arduino.h>
#include <LoRa_E32.h>

#define CHUNK_SIZE 52
#define RB_SIZE 100

#define NODE_ADDH     0x00
#define NODE_ADDL     0x00

#define GW_ADDH       0x00
#define GW_ADDL       0x01

#define LORA_CH       20

#define LORA_TX_PIN   34
#define LORA_RX_PIN   25

#define LORA_BAUD     9600

#define RB_HIGH_WATERMARK  80   
#define RB_LOW_WATERMARK   20   

HardwareSerial loraSerial(1);
LoRa_E32 e32(LORA_TX_PIN, LORA_RX_PIN, &loraSerial, UART_BPS_RATE_9600);

/* ================== PACKETS ================== */
typedef struct __attribute__((packed)) {
    uint8_t  type;
    uint32_t file_size;
    uint16_t total_chunks;
    uint32_t crc32;
} PktStart;

typedef struct {
    uint16_t index;
    uint8_t  len;
    uint8_t  data[CHUNK_SIZE];
} Chunk;

/* ================== RING BUFFER ================== */
Chunk ring[RB_SIZE];

volatile int head = 0;
volatile int tail = 0;
SemaphoreHandle_t rbMutex;
SemaphoreHandle_t dataReady;

int rbCount() {
    return (head - tail + RB_SIZE) % RB_SIZE;
}


bool isFull() {
    return ((head + 1) % RB_SIZE) == tail;
}

bool isEmpty() {
    return head == tail;
}

bool pushChunk(uint16_t idx, uint8_t len, uint8_t* data) {
    if (xSemaphoreTake(rbMutex, portMAX_DELAY) != pdTRUE) return false;
    
    if (isFull()) {
        xSemaphoreGive(rbMutex);
        return false;
    }

    ring[head].index = idx;
    ring[head].len   = len;
    memcpy(ring[head].data, data, len);
    head = (head + 1) % RB_SIZE;
    int count = rbCount(); 

    xSemaphoreGive(rbMutex);
    xSemaphoreGive(dataReady);
    if (count >= RB_HIGH_WATERMARK) {
        linuxSend("WAIT");
    }
    return true;
}

bool popChunk(Chunk &out) {
    if (xSemaphoreTake(rbMutex, portMAX_DELAY) != pdTRUE) return false;
    
    if (isEmpty()) {
        xSemaphoreGive(rbMutex);
        return false;
    }

    out = ring[tail];
    memset(&ring[tail], 0, sizeof(Chunk));
    tail = (tail + 1) % RB_SIZE;

    xSemaphoreGive(rbMutex);
    return true;
}

/* ================== UART RESPONSE ================== */
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

/* ================== CRC32 ================== */
uint32_t crc32_calc(const uint8_t* data, size_t len) {
    uint32_t crc = ~0u;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1));
    }
    return ~crc;
}

/* ================== HANDLE START ================== */
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

    linuxSend("READY");
}

/* ================== HANDLE CHUNK (PUSH RING BUFFER) ================== */
void handleChunk(String& header) {

    int p1 = header.indexOf(':');
    int p2 = header.indexOf(':', p1 + 1);
    int p3 = header.indexOf(':', p2 + 1);

    uint16_t idx = (uint16_t)header.substring(p1 + 1, p2).toInt();
    uint8_t len  = (uint8_t)header.substring(p2 + 1, p3).toInt();

    uint32_t crc_expect = (uint32_t)strtoul(
        header.substring(p3 + 1).c_str(), nullptr, 16);

    uint8_t buf[CHUNK_SIZE];
    memset(buf, 0, sizeof(buf));

    size_t received = 0;
    unsigned long t = millis();

    while (received < len && millis() - t < 5000) {
        if (Serial.available()) {
            buf[received++] = (uint8_t)Serial.read();
        }
        else {
            vTaskDelay(1 / portTICK_PERIOD_MS);
        }
    }

    if (received != len) {
        linuxSendf("ERROR:CHUNK_%u_INCOMPLETE", idx);
        return;
    }

    uint32_t crc_actual = crc32_calc(buf, len);

    if (crc_actual != crc_expect) {
        linuxSendf("ERROR:CHUNK_%u_CRC_FAIL", idx);
        return;
    }

    if (pushChunk(idx, len, buf)) {
        // ✔ FIX Ở ĐÂY
        linuxSendf("OK:%u:%08X", idx, crc_actual);
    } else {
        linuxSendf("ERROR:%u:BUFFER_FULL", idx);
    }
}

/* ================== HANDLE END ================== */
void handleEnd(String& line) {
    uint32_t crc = (uint32_t)strtoul(
        line.substring(line.indexOf(':') + 1).c_str(), nullptr, 16);

    linuxSendf("DONE:%08X", crc);
}

/* ================== UART TASK ================== */
void UartTask(void *pvParameters) {

    String lineBuf = "";

    while (1) {

        while (Serial.available()) {
            char c = (char)Serial.read();

            if (c == '\n') {
                lineBuf.trim();

                if (lineBuf.length() > 0) {

                    if (lineBuf.startsWith("START:")) {
                        handleStart(lineBuf);
                    }
                    else if (lineBuf.startsWith("CHUNK:")) {
                        handleChunk(lineBuf);
                    }
                    else if (lineBuf.startsWith("END:")) {
                        handleEnd(lineBuf);
                    }
                }

                lineBuf = "";
            }
            else if (c != '\r') {
                lineBuf += c;
            }
        }

        vTaskDelay(1 / portTICK_PERIOD_MS);
    }
}

/* ================== LORA TASK ================== */
void LoRaTask(void *pvParameters) {

    Chunk c;

    while (1) {
        xSemaphoreTake(dataReady, portMAX_DELAY); 

        if (popChunk(c)) {

            // =========================
            // TẠO PACKET GỬI LORA
            // =========================

            // fixed packet gửi qua E32
            struct {
                uint16_t index;
                uint8_t len;
                uint8_t data[CHUNK_SIZE];
            } payload;

            payload.index = c.index;
            payload.len   = c.len;
            memcpy(payload.data, c.data, c.len);

            // =========================
            // GỬI QUA E32 LORA
            // =========================
            ResponseStatus rs = e32.sendFixedMessage(
                GW_ADDH,
                GW_ADDL,
                LORA_CH,
                &payload,
                sizeof(payload)
            );

            if (rbCount() <= RB_LOW_WATERMARK) {
                linuxSend("RESUME");
            }

            // =========================
            // DEBUG (KHÔNG DÙNG SERIAL CHO PROTOCOL)
            // =========================
            // if (rs.code == SUCCESS) {
            //     Serial.print("[LoRa] OK idx=");
            //     Serial.println(c.index);
            // } else {
            //     Serial.print("[LoRa] FAIL idx=");
            //     Serial.println(c.index);
            // }
        }

        //vTaskDelay(5 / portTICK_PERIOD_MS);
        vTaskDelay(10 / portTICK_PERIOD_MS); 
    }
}

/* ================== SETUP ================== */
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.flush();
    linuxSend("BOOT_OK"); 
    rbMutex = xSemaphoreCreateMutex();
    dataReady = xSemaphoreCreateCounting(RB_SIZE, 0);
    loraSerial.begin(LORA_BAUD,SERIAL_8N1,LORA_RX_PIN,LORA_TX_PIN);
    delay(500);
    e32.begin();
    ResponseStructContainer csc = e32.getConfiguration();
    if (csc.status.code == SUCCESS) {

        Configuration config = *(Configuration*) csc.data;

        config.ADDH = NODE_ADDH;
        config.ADDL = NODE_ADDL;
        config.CHAN = LORA_CH;

        config.OPTION.fixedTransmission = FT_FIXED_TRANSMISSION;

        e32.setConfiguration(config,WRITE_CFG_PWR_DWN_SAVE);
    }

    csc.close();


    xTaskCreate(UartTask, "UART Task", 4096, NULL, 1, NULL);
    xTaskCreate(LoRaTask, "LoRa Task", 4096, NULL, 1, NULL);
}

/* ================== LOOP (RỖNG) ================== */
void loop() {
    // không dùng trong FreeRTOS architecture
}