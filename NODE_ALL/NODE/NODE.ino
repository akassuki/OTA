#include <Arduino.h>
#include "LoRa_E32.h"

// ── Config ───────────────────────────────────────────────────
#define LORA_TX_PIN   34
#define LORA_RX_PIN   25
#define LORA_BAUD     9600

#define NODE_ADDH     0x00
#define NODE_ADDL     0x00
#define GW_ADDH       0x00
#define GW_ADDL       0x01
#define LORA_CH       20

#define CHUNK_SIZE    52   // KHÔNG đổi — khớp với Gateway và Linux

HardwareSerial loraSerial(1);
LoRa_E32 e32(LORA_TX_PIN, LORA_RX_PIN, &loraSerial, UART_BPS_RATE_9600);

// ── Struct khớp 100% với Gateway ─────────────────────────────
typedef struct __attribute__((packed)) {
    uint16_t index;
    uint8_t  len;
    uint8_t  data[CHUNK_SIZE];
} Chunk;   // 55 bytes — an toàn với E32 (giới hạn 58 bytes payload)

// ── ACK gửi ngược về Gateway ─────────────────────────────────
typedef struct __attribute__((packed)) {
    uint8_t  status;   // 0xAA = OK, 0xFF = NACK
    uint16_t index;
} AckPkt;  // 3 bytes

void sendAck(uint8_t status, uint16_t index) {
    // Chờ Gateway chuyển sang chế độ RX sau khi TX xong
    vTaskDelay(pdMS_TO_TICKS(300));
    AckPkt ack = { status, index };
    e32.sendFixedMessage(GW_ADDH, GW_ADDL, LORA_CH, &ack, sizeof(ack));
}

// ── Ring buffer ───────────────────────────────────────────────
#define RB_SIZE 16

Chunk ring[RB_SIZE];
volatile int rbHead = 0, rbTail = 0;
SemaphoreHandle_t rbMutex;
SemaphoreHandle_t dataReady;

bool rbFull()  { return ((rbHead + 1) % RB_SIZE) == rbTail; }
bool rbEmpty() { return rbHead == rbTail; }

bool pushChunk(const Chunk* c) {
    if (xSemaphoreTake(rbMutex, pdMS_TO_TICKS(50)) != pdTRUE) return false;
    if (rbFull()) { xSemaphoreGive(rbMutex); return false; }
    memcpy(&ring[rbHead], c, sizeof(Chunk));
    rbHead = (rbHead + 1) % RB_SIZE;
    xSemaphoreGive(rbMutex);
    xSemaphoreGive(dataReady);
    return true;
}

bool popChunk(Chunk* out) {
    if (xSemaphoreTake(rbMutex, pdMS_TO_TICKS(50)) != pdTRUE) return false;
    if (rbEmpty()) { xSemaphoreGive(rbMutex); return false; }
    memcpy(out, &ring[rbTail], sizeof(Chunk));
    rbTail = (rbTail + 1) % RB_SIZE;
    xSemaphoreGive(rbMutex);
    return true;
}

// ── LoRaRxTask ────────────────────────────────────────────────
// Priority cao — không được bị block bởi bất cứ thứ gì
void LoRaRxTask(void* pv) {
    uint16_t lastReceivedIdx = 0xFFFF; 
    while (true) {
        //uint16_t lastReceivedIdx = 0xFFFF; 
        // Chờ đủ bytes — tránh đọc giữa chừng khi packet chưa vào hết buffer UART
        if (e32.available() < (int)sizeof(Chunk)) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        ResponseStructContainer rsc = e32.receiveMessage(sizeof(Chunk));

        // E32 RF error — packet bị nhiễu, CRC tầng RF fail
        if (rsc.status.code != SUCCESS) {
            Serial.printf("[RX] RF error code=%d\n", rsc.status.code);
            rsc.close();
            continue;
        }

        Chunk* pkt = (Chunk*) rsc.data;

        // Sanity check len — tránh garbage data
        if (pkt->len == 0 || pkt->len > CHUNK_SIZE) {
            Serial.printf("[RX] NACK idx=%-4u | bad len=%u\n",
                          pkt->index, pkt->len);
            sendAck(0xFF, pkt->index);
            rsc.close();
            continue;
        }
        if (pkt->index == lastReceivedIdx) {
            Serial.printf("[RX] DUP  idx=%-4u | re-ACK\n", pkt->index);
            sendAck(0xAA, pkt->index);   // ACK lại để Gateway biết đã có
            rsc.close();
            continue;                    // KHÔNG push vào buffer lần 2
        }
        // Push vào ring buffer
        if (!pushChunk(pkt)) {
            // Buffer đầy — NACK để Gateway giữ lại chunk này, retry sau
            Serial.printf("[RX] NACK idx=%-4u | buffer full\n", pkt->index);
            sendAck(0xFF, pkt->index);
            rsc.close();
            continue;
        }

        // ACK — Gateway có thể gửi chunk tiếp theo
        lastReceivedIdx = pkt->index; 
        sendAck(0xAA, pkt->index);
        rsc.close();
    }
}

// ── ProcessTask ───────────────────────────────────────────────
// Priority thấp hơn — log có thể chậm, không ảnh hưởng RX
void ProcessTask(void* pv) {
    Chunk c;
    bool     firstChunk  = true;
    uint16_t lastIndex   = 0;
    uint32_t totalRecv   = 0;
    uint32_t totalUnique = 0;
    uint32_t totalDup    = 0;
    uint32_t totalMiss   = 0;

    while (true) {
        xSemaphoreTake(dataReady, portMAX_DELAY);
        if (!popChunk(&c)) continue;

        totalRecv++;

        // Duplicate — gateway retry cùng chunk
        if (!firstChunk && c.index == lastIndex) {
            totalDup++;
            Serial.printf("[DUP]  idx=%-5u | gateway gui lai, lan thu %u\n",
                          c.index, totalDup);
            continue;
        }

        // Out-of-order
        if (!firstChunk && c.index < lastIndex) {
            Serial.printf("[OOO]  idx=%-5u | sai thu tu, mong cho idx=%u\n",
                          c.index, lastIndex + 1);
            continue;
        }

        // Miss — chunk bị mất
        if (!firstChunk && c.index > (uint16_t)(lastIndex + 1)) {
            uint16_t missed = c.index - lastIndex - 1;
            totalMiss += missed;
            Serial.printf("[MISS] Chunk %u..%u bi mat (%u chunk) | tong mat=%u\n",
                          lastIndex + 1, c.index - 1, missed, totalMiss);
        }

        firstChunk = false;
        lastIndex  = c.index;
        totalUnique++;

        // Log rõ ràng
        Serial.println("----------------------------------------");
        Serial.printf("[OK]  Chunk thu   : %u (tong nhan=%u)\n",
                      totalUnique, totalRecv);
        Serial.printf("      So thu tu   : idx=%u\n",   c.index);
        Serial.printf("      Do dai data : %u bytes\n", c.len);
        Serial.printf("      Bi mat      : %u chunk\n", totalMiss);
        Serial.printf("      Trung lap   : %u lan\n",   totalDup);
        Serial.println("      Data (hex):");

        for (uint8_t i = 0; i < c.len; i++) {
            if (i % 16 == 0) Serial.printf("        %02X: ", i);
            Serial.printf("%02X ", c.data[i]);
            if ((i % 16 == 15) || (i == c.len - 1)) Serial.println();
        }
        Serial.flush();
    }
}

// ── Setup ─────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);

    loraSerial.begin(LORA_BAUD, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);
    e32.begin();

    ResponseStructContainer csc = e32.getConfiguration();
    if (csc.status.code == SUCCESS) {
        Configuration cfg = *(Configuration*) csc.data;
        cfg.ADDH = NODE_ADDH;
        cfg.ADDL = NODE_ADDL;
        cfg.CHAN  = LORA_CH;
        cfg.OPTION.fixedTransmission = FT_FIXED_TRANSMISSION;
        e32.setConfiguration(cfg, WRITE_CFG_PWR_DWN_SAVE);
        Serial.println("[Node] LoRa config OK");
    } else {
        Serial.println("[Node] LoRa config FAIL");
    }
    csc.close();

    rbMutex   = xSemaphoreCreateMutex();
    dataReady = xSemaphoreCreateCounting(RB_SIZE, 0);

    // RxTask ở core 0, priority 2 — luôn sẵn sàng nhận
    // ProcessTask ở core 1, priority 1 — log không block RX
    xTaskCreatePinnedToCore(LoRaRxTask,  "RX",  4096, NULL, 2, NULL, 0);
    xTaskCreatePinnedToCore(ProcessTask, "LOG", 4096, NULL, 1, NULL, 1);

    Serial.println("[Node] Ready");
}

void loop() { vTaskDelay(pdMS_TO_TICKS(1000)); }