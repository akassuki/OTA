#include <Arduino.h>
#include <LoRa_E32.h>
#include <HardwareSerial.h>

#define LORA_TX_PIN  34
#define LORA_RX_PIN  25
#define LORA_BAUD    9600
#define GW_ADDH      0x00
#define GW_ADDL      0x01
#define LORA_CH      20

HardwareSerial loraSerial(1);
LoRa_E32 e32(LORA_TX_PIN, LORA_RX_PIN, &loraSerial, UART_BPS_RATE_9600);

void setup() {
    Serial.begin(115200);
    loraSerial.begin(LORA_BAUD, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);
    e32.begin();
    delay(1000);
    Serial.println("Ready");
}

void loop() {
    e32.sendFixedMessage(GW_ADDH, GW_ADDL, LORA_CH, "HELLO", 5);
    Serial.println("Sent HELLO");
    delay(2000);
}