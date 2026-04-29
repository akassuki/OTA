#include <Arduino.h>
#include <LoRa_E32.h>

/* ========= CONFIG ========= */

#define NODE_ADDH     0x00
#define NODE_ADDL     0x00

#define GW_ADDH       0x00
#define GW_ADDL       0x01

#define LORA_CH       20

#define LORA_TX_PIN   34
#define LORA_RX_PIN   25

#define LORA_BAUD     9600

/* ========= LORA ========= */

HardwareSerial loraSerial(1);
LoRa_E32 e32(LORA_TX_PIN, LORA_RX_PIN,
             &loraSerial,
             UART_BPS_RATE_9600);

String lineBuf = "";

void sendToGateway(const char* msg) {

    char out[128];

    snprintf(
        out,
        sizeof(out),
        "%s\n",
        msg
    );

    Serial.print("[TX] ");
    Serial.print(out);

    ResponseStatus rs =
        e32.sendFixedMessage(
            GW_ADDH,
            GW_ADDL,
            LORA_CH,
            out,
            strlen(out)
        );

    if (rs.code != SUCCESS) {

        Serial.println("[ERROR] SEND FAIL");
    }
}

void setup() {

    Serial.begin(115200);

    loraSerial.begin(
        LORA_BAUD,
        SERIAL_8N1,
        LORA_RX_PIN,
        LORA_TX_PIN
    );

    delay(500);

    e32.begin();

    /* ===== Fixed Mode ===== */

    ResponseStructContainer csc =
        e32.getConfiguration();

    if (csc.status.code == SUCCESS) {

        Configuration config =
            *(Configuration*) csc.data;

        config.ADDH = NODE_ADDH;
        config.ADDL = NODE_ADDL;
        config.CHAN = LORA_CH;

        config.OPTION.fixedTransmission =
            FT_FIXED_TRANSMISSION;

        e32.setConfiguration(
            config,
            WRITE_CFG_PWR_DWN_SAVE
        );
    }

    csc.close();

    Serial.println("NODE READY");
}

void loop() {

    if (e32.available() > 1) {

        ResponseContainer rc =
            e32.receiveMessage();

        if (rc.status.code == SUCCESS) {

            String msg = String(rc.data);

            Serial.print("[RX] ");
            Serial.println(msg);

            /* ==========================
               TEST PROTOCOL
               ========================== */

            if (msg.startsWith("START:")) {

                sendToGateway("READY");
            }

            else if (msg.startsWith("CHUNK:")) {

                static int idx = 0;

                char reply[32];

                sprintf(reply, "OK:%d", idx++);

                sendToGateway(reply);
            }

            else if (msg.startsWith("END:")) {

                sendToGateway("DONE");
            }
        }
    }

    delay(1);
}