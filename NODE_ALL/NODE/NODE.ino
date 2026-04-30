#include <Arduino.h>
#include "LoRa_E32.h"

/* ================== LORA CONFIG ================== */
#define LORA_TX_PIN   34
#define LORA_RX_PIN   25
#define LORA_BAUD     9600

#define NODE_ADDH     0x00
#define NODE_ADDL     0x01   // node nhận
#define GW_ADDH       0x00
#define GW_ADDL       0x00

#define LORA_CH       20

HardwareSerial loraSerial(1);
LoRa_E32 e32(LORA_TX_PIN, LORA_RX_PIN, &loraSerial, UART_BPS_RATE_9600);

/* ================== PAYLOAD STRUCT ================== */
#define CHUNK_SIZE 52

typedef struct __attribute__((packed)) {
    uint16_t index;
    uint8_t len;
    uint8_t data[CHUNK_SIZE];
} Chunk;

/* ================== SETUP ================== */
void setup() {
    Serial.begin(115200);
    delay(500);

    loraSerial.begin(LORA_BAUD, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);

    e32.begin();

    ResponseStructContainer csc = e32.getConfiguration();
    if (csc.status.code == SUCCESS) {

        Configuration config = *(Configuration*) csc.data;

        config.ADDH = NODE_ADDH;
        config.ADDL = NODE_ADDL;
        config.CHAN  = LORA_CH;

        config.OPTION.fixedTransmission = FT_FIXED_TRANSMISSION;

        e32.setConfiguration(config, WRITE_CFG_PWR_DWN_SAVE);
        Serial.println("[LoRa] Config OK");
    }

    csc.close();

    Serial.println("[LoRa RX Node] Ready...");
}

/* ================== LOOP ================== */
void loop() {

    if (e32.available() > 1) {

        ResponseStructContainer rsc = e32.receiveMessage(sizeof(Chunk));

        if (rsc.status.code == SUCCESS) {

            Chunk pkt = *(Chunk*) rsc.data;

            Serial.println("================================");

            Serial.print("[RX] Index : ");
            Serial.println(pkt.index);

            Serial.print("[RX] Len   : ");
            Serial.println(pkt.len);

            Serial.print("[RX] Data  : ");

            for (int i = 0; i < pkt.len; i++) {
                if (pkt.data[i] < 16) Serial.print("0");
                Serial.print(pkt.data[i], HEX);
                Serial.print(" ");
            }

            Serial.println();

            Serial.println("================================");
        }
        else {
            Serial.println("[LoRa] Receive FAIL");
        }

        rsc.close();
    }

    delay(10);
}