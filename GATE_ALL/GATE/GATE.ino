#include <Arduino.h>
#include <LoRa_E32.h>

#define GW_ADDH       0x00
#define GW_ADDL       0x01

#define NODE_ADDH     0x00
#define NODE_ADDL     0x00

#define LORA_CH       20

#define LORA_TX_PIN   34
#define LORA_RX_PIN   25

#define USB_BAUD      115200
#define LORA_BAUD     9600

#define BUF_SIZE      256

HardwareSerial loraSerial(1);

LoRa_E32 e32(
    LORA_TX_PIN,
    LORA_RX_PIN,
    &loraSerial,
    UART_BPS_RATE_9600
);

void setup() {

    Serial.begin(USB_BAUD);

    loraSerial.begin(
        LORA_BAUD,
        SERIAL_8N1,
        LORA_RX_PIN,
        LORA_TX_PIN
    );

    delay(1000);

    e32.begin();

    ResponseStructContainer csc =
        e32.getConfiguration();

    if (csc.status.code == SUCCESS) {

        Configuration config =
            *(Configuration*) csc.data;

        config.ADDH = GW_ADDH;
        config.ADDL = GW_ADDL;
        config.CHAN = LORA_CH;

        config.OPTION.fixedTransmission =
            FT_FIXED_TRANSMISSION;

        e32.setConfiguration(
            config,
            WRITE_CFG_PWR_DWN_SAVE
        );
    }

    csc.close();
}

void loop() {

    /* =========================
       Linux -> Node
    ========================= */

    while (Serial.available()) {

        uint8_t buf[BUF_SIZE];
        size_t len = 0;

        delay(5);

        while (Serial.available() &&
               len < BUF_SIZE) {

            buf[len++] =
                (uint8_t)Serial.read();
        }

        if (len > 0) {

            e32.sendFixedMessage(
                NODE_ADDH,
                NODE_ADDL,
                LORA_CH,
                buf,
                len
            );
        }
    }

    /* =========================
       Node -> Linux
    ========================= */

    if (e32.available() > 0) {

        ResponseContainer rc =
            e32.receiveMessage();

        if (rc.status.code == SUCCESS) {

            String msg = rc.data;

            Serial.write(
                (const uint8_t*)msg.c_str(),
                msg.length()
            );

            Serial.flush();
        }
    }

    delay(1);
}