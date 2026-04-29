#include <Arduino.h>
#include <LoRa_E32.h>

/* ================= CONFIG ================= */

#define LORA_RX_PIN   25
#define LORA_TX_PIN   34

#define LORA_BAUD     9600
#define LINUX_BAUD    115200

#define LORA_CH       20

#define GATE_ADDH     0x00
#define GATE_ADDL     0x01

#define NODE_ADDH     0x00
#define NODE_ADDL     0x00

#define BUF_SIZE      256

/* ================= LORA ================= */

HardwareSerial loraSerial(1);

LoRa_E32 e32(
    LORA_TX_PIN,
    LORA_RX_PIN,
    &loraSerial,
    UART_BPS_RATE_9600
);

/* ================= SETUP ================= */

void setup()
{
    Serial.begin(LINUX_BAUD);

    loraSerial.begin(
        LORA_BAUD,
        SERIAL_8N1,
        LORA_RX_PIN,
        LORA_TX_PIN
    );

    delay(500);

    e32.begin();

    /* ===== CONFIG FIXED MODE ===== */

    ResponseStructContainer csc =
        e32.getConfiguration();

    if (csc.status.code == SUCCESS)
    {
        Configuration cfg =
            *(Configuration*)csc.data;

        cfg.ADDH = GATE_ADDH;
        cfg.ADDL = GATE_ADDL;
        cfg.CHAN = LORA_CH;

        cfg.OPTION.fixedTransmission =
            FT_FIXED_TRANSMISSION;

        e32.setConfiguration(
            cfg,
            WRITE_CFG_PWR_DWN_SAVE
        );
    }

    csc.close();

    delay(500);
}

/* ================= LOOP ================= */

void loop()
{
    /* =====================================
     * UART -> LORA
     * ===================================== */

    if (Serial.available())
    {
        uint8_t buf[BUF_SIZE];

        size_t len = 0;

        delay(2);

        while (
            Serial.available() &&
            len < BUF_SIZE
        )
        {
            buf[len++] =
                (uint8_t)Serial.read();
        }

        if (len > 0)
        {
            e32.sendFixedMessage(
                NODE_ADDH,
                NODE_ADDL,
                LORA_CH,
                buf,
                len
            );
        }
    }

    /* =====================================
     * LORA -> UART
     * ===================================== */

    if (e32.available() > 1)
    {
        ResponseContainer rc =
            e32.receiveMessage();

        if (rc.status.code == SUCCESS)
        {
            Serial.write(
                (uint8_t*)rc.data.c_str(),
                rc.data.length()
            );

            Serial.flush();
        }
    }

    delay(1);
}