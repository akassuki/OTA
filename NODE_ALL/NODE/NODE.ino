    #include "lora_config.h"
    #include "janpatch.h"

    // ── fread/fwrite/fseek/ftell ──────────────────────────────────

    // ── Global ───────────────────────────────────────────────────
    HardwareSerial loraSerial(1);
    LoRa_E32 e32(LORA_TX_PIN, LORA_RX_PIN, &loraSerial, UART_BPS_RATE_9600);

    #define RB_SIZE 16
    LoraChunk ring[RB_SIZE];
    volatile int rbHead = 0, rbTail = 0;
    SemaphoreHandle_t rbMutex;
    SemaphoreHandle_t dataReady;

    bool rbFull()  { return ((rbHead + 1) % RB_SIZE) == rbTail; }
    bool rbEmpty() { return rbHead == rbTail; }

    bool pushChunk(const LoraChunk* c) {
        if (xSemaphoreTake(rbMutex, pdMS_TO_TICKS(50)) != pdTRUE) return false;
        if (rbFull()) { xSemaphoreGive(rbMutex); return false; }
        memcpy(&ring[rbHead], c, sizeof(LoraChunk));
        rbHead = (rbHead + 1) % RB_SIZE;
        xSemaphoreGive(rbMutex);
        xSemaphoreGive(dataReady);
        return true;
    }

    bool popChunk(LoraChunk* out) {
        if (xSemaphoreTake(rbMutex, pdMS_TO_TICKS(50)) != pdTRUE) return false;
        if (rbEmpty()) { xSemaphoreGive(rbMutex); return false; }
        memcpy(out, &ring[rbTail], sizeof(LoraChunk));
        rbTail = (rbTail + 1) % RB_SIZE;
        xSemaphoreGive(rbMutex);
        return true;
    }

    void sendAck(uint8_t status, uint16_t index) {
        vTaskDelay(pdMS_TO_TICKS(300));
        AckPkt ack = { status, index };
        e32.sendFixedMessage(GW_ADDH, GW_ADDL, LORA_CH, &ack, sizeof(ack));
    }

    // ── applyDeltaOTA ────────────────────────────────────────────

    size_t my_fread(void* buf, size_t size, size_t count, UnifiedStream* s) {
        size_t bytes = size * count;
        if (s->type == STREAM_TYPE_PART) {
            if (esp_partition_read(s->partition, (size_t)s->offset, buf, bytes) != ESP_OK)
                return 0;
            s->offset += bytes;
            s->pos    += bytes;
            return count;
        }
        return 0; // OTA không đọc
    }

size_t my_fwrite(const void* buf, size_t size, size_t count, UnifiedStream* s) {
    size_t bytes = size * count;
    if (s->type == STREAM_TYPE_OTA) {
        const uint8_t* src = (const uint8_t*)buf;
        size_t written = 0;
        while (written < bytes) {
            size_t space = 512 - s->ota_buf_pos;
            size_t copy  = (space < bytes - written) ? space : bytes - written;
            memcpy(s->ota_buf + s->ota_buf_pos, src + written, copy);
            s->ota_buf_pos += copy;
            written        += copy;
            if (s->ota_buf_pos == 512) {
                esp_err_t e = esp_ota_write(s->ota_handle, s->ota_buf, 512);
                if (e != ESP_OK) {
                    Serial.printf("[FWRITE] ota_write 512 FAIL: %s\n", esp_err_to_name(e));
                    return 0;
                }
                s->ota_buf_pos = 0;
            }
        }
        s->pos += bytes;
        Serial.printf("[FWRITE] ghi %u bytes, tong=%ld\n", bytes, s->pos); // TẠM THỜI
        return count;
    }
    if (s->type == STREAM_TYPE_PART) {
        // Partition read-only — không ghi
        s->pos += bytes;
        return count;
    }
    return 0;
}

int my_fseek(UnifiedStream* s, long int offset, int whence) {
    if (whence == SEEK_SET) {
        s->offset = offset;
        s->pos    = offset;
    } else if (whence == SEEK_CUR) {
        s->offset += offset;
        s->pos    += offset;
    }
    return 0;
}

long my_ftell(UnifiedStream* s) {
    return s->pos;
}

    bool ota_flush(UnifiedStream* s) {
        if (s->ota_buf_pos > 0) {
            if (esp_ota_write(s->ota_handle, s->ota_buf, s->ota_buf_pos) != ESP_OK)
                return false;
            s->ota_buf_pos = 0;
        }
        return true;
    }

    // ── applyDeltaOTA ────────────────────────────────────────────
bool applyDeltaOTA(size_t patchSize) {
    Serial.println("[DELTA] Bat dau apply patch...");

    const esp_partition_t* oldPart   = esp_ota_get_running_partition();
    const esp_partition_t* patchPart = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "patch");
    const esp_partition_t* newPart   = esp_ota_get_next_update_partition(NULL);

    if (!oldPart || !patchPart || !newPart) {
        Serial.println("[DELTA] ERROR: Khong tim thay partition");
        return false;
    }

    esp_ota_handle_t otaHandle = 0;
    esp_err_t err = esp_ota_begin(newPart, OTA_WITH_SEQUENTIAL_WRITES, &otaHandle);
    if (err != ESP_OK) {
        Serial.printf("[DELTA] ERROR: ota_begin: %s\n", esp_err_to_name(err));
        return false;
    }

    // Source stream
    UnifiedStream src;
    memset(&src, 0, sizeof(src));
    src.type      = STREAM_TYPE_PART;
    src.partition = oldPart;
    src.part_size = oldPart->size;

    // Patch stream
    UnifiedStream pat;
    memset(&pat, 0, sizeof(pat));
    pat.type      = STREAM_TYPE_PART;
    pat.partition = patchPart;
    pat.part_size = patchPart->size;

    // Target stream (OTA)
    UnifiedStream tgt;
    memset(&tgt, 0, sizeof(tgt));
    tgt.type       = STREAM_TYPE_OTA;
    tgt.ota_handle = otaHandle;

    uint8_t buf_src[512], buf_pat[512], buf_tgt[512];

    janpatch_ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.fread  = my_fread;
    ctx.fwrite = my_fwrite;
    ctx.fseek  = my_fseek;
    ctx.ftell  = my_ftell;

    ctx.source_buffer.buffer = buf_src;
    ctx.source_buffer.size   = sizeof(buf_src);
    ctx.source_buffer.stream = &src;

    ctx.patch_buffer.buffer  = buf_pat;
    ctx.patch_buffer.size    = sizeof(buf_pat);
    ctx.patch_buffer.stream  = &pat;

    ctx.target_buffer.buffer = buf_tgt;
    ctx.target_buffer.size   = sizeof(buf_tgt);
    ctx.target_buffer.stream = &tgt;

    ctx.max_file_size = (long)(oldPart->size + patchSize);

    Serial.println("[DELTA] Dang apply janpatch...");
    int ret = janpatch(ctx, &src, &pat, &tgt);
    Serial.printf("[DELTA] janpatch ret=%d | output=%ld bytes\n", ret, tgt.pos);

    if (ret != 0) {
        Serial.printf("[DELTA] ERROR: janpatch failed: %d\n", ret);
        esp_ota_abort(otaHandle);
        return false;
    }

    if (!ota_flush(&tgt)) {
        Serial.println("[DELTA] ERROR: flush fail");
        esp_ota_abort(otaHandle);
        return false;
    }

    err = esp_ota_end(otaHandle);
    Serial.printf("[DELTA] ota_end: %s\n", esp_err_to_name(err));
    if (err != ESP_OK) {
        esp_ota_abort(otaHandle);
        return false;
    }

    err = esp_ota_set_boot_partition(newPart);
    if (err != ESP_OK) {
        Serial.printf("[DELTA] ERROR: set_boot: %s\n", esp_err_to_name(err));
        return false;
    }

    Serial.println("[DELTA] THANH CONG!");
    return true;
}

    // ── LoRaRxTask ────────────────────────────────────────────────
    void LoRaRxTask(void* pv) {
        uint16_t lastReceivedIdx = 0xFFFF;
        while (true) {
            if (e32.available() < (int)sizeof(LoraChunk)) {
                vTaskDelay(pdMS_TO_TICKS(5));
                continue;
            }
            ResponseStructContainer rsc = e32.receiveMessage(sizeof(LoraChunk));
            if (rsc.status.code != SUCCESS) {
                rsc.close(); continue;
            }
            LoraChunk* pkt = (LoraChunk*) rsc.data;
            if (pkt->len == 0 || pkt->len > CHUNK_SIZE) {
                sendAck(0xFF, pkt->index); rsc.close(); continue;
            }
            if (pkt->index == lastReceivedIdx) {
                sendAck(0xAA, pkt->index); rsc.close(); continue;
            }
            if (!pushChunk(pkt)) {
                sendAck(0xFF, pkt->index); rsc.close(); continue;
            }
            lastReceivedIdx = pkt->index;
            sendAck(0xAA, pkt->index);
            rsc.close();
        }
    }

    // ── ProcessTask ───────────────────────────────────────────────
    void ProcessTask(void* pv) {
        Serial.println("fasfasdfads");
        LoraChunk c;
        bool     firstChunk  = true;
        uint16_t lastIndex   = 0;
        size_t   patchSize   = 0;
        // ── THÊM các biến đếm ────────────────────────────────────
        uint32_t totalRecv   = 0;
        uint32_t totalUnique = 0;
        uint32_t totalDup    = 0;
        uint32_t totalMiss   = 0;

        const esp_partition_t* patchPart = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "patch");

        if (!patchPart) {
            Serial.println("[PROCESS] ERROR: Khong tim thay patch partition");
            vTaskDelete(NULL);
            return;
        }

        Serial.println("[PROCESS] Xoa patch partition...");
        if (c.index == 0 && firstChunk) {
            Serial.println("[PROCESS] Xoa patch partition...");
            esp_partition_erase_range(patchPart, 0, patchPart->size);
        }

        while (true) {
            xSemaphoreTake(dataReady, portMAX_DELAY);
            if (!popChunk(&c)) continue;

            totalRecv++;

            // ── Kiểm tra duplicate ───────────────────────────────
            if (!firstChunk && c.index == lastIndex) {
                totalDup++;
                Serial.printf("[DUP]  idx=%-5u\n", c.index);
                continue;
            }

            // ── Kiểm tra out-of-order ────────────────────────────
            if (!firstChunk && c.index < lastIndex) {
                Serial.printf("[OOO]  idx=%-5u\n", c.index);
                continue;
            }

            // ── Kiểm tra miss ────────────────────────────────────
            if (!firstChunk && c.index > (uint16_t)(lastIndex + 1)) {
                uint16_t missed = c.index - lastIndex - 1;
                totalMiss += missed;
                Serial.printf("[MISS] Chunk %u..%u bi mat\n",
                            lastIndex + 1, c.index - 1);
            }

            firstChunk = false;
            lastIndex  = c.index;
            totalUnique++;

            // ── Ghi vào patch partition ──────────────────────────
            size_t offset = (size_t)c.index * CHUNK_SIZE;
            esp_err_t err = esp_partition_write(patchPart, offset, c.data, c.len);
            if (err != ESP_OK) {
                Serial.printf("[PROCESS] ERROR: write patch idx=%u: %s\n",
                            c.index, esp_err_to_name(err));
                continue;
            }

            patchSize += c.len;

            // ── LOG CHI TIẾT ─────────────────────────────────────
            Serial.println("----------------------------------------");
            Serial.printf("[OK]  Chunk thu   : %u (tong nhan=%u)\n",
                        totalUnique, totalRecv);
            Serial.printf("      So thu tu   : idx=%u / total=%u\n",
                        c.index, c.total);
            Serial.printf("      Do dai data : %u bytes\n", c.len);
            Serial.printf("      Patch size  : %u bytes\n", patchSize);
            Serial.printf("      Bi mat      : %u chunk\n", totalMiss);
            Serial.printf("      Trung lap   : %u lan\n",   totalDup);
            Serial.println("      Data (hex):");
            for (uint8_t i = 0; i < c.len; i++) {
                if (i % 16 == 0) Serial.printf("        %02X: ", i);
                Serial.printf("%02X ", c.data[i]);
                if ((i % 16 == 15) || (i == c.len - 1)) Serial.println();
            }
            Serial.flush();

            // ── Chunk cuối → apply Delta OTA ─────────────────────
            if (c.index == c.total - 1) {
                Serial.printf("[PATCH] Nhan du %u chunks | size=%u bytes\n",
                            c.total, patchSize);
                if (applyDeltaOTA(patchSize)) {
                    vTaskDelay(pdMS_TO_TICKS(3000));
                    esp_restart();
                } else {
                    Serial.println("[DELTA] FAIL — giu nguyen firmware cu");
                    firstChunk  = true;
                    lastIndex   = 0;
                    patchSize   = 0;
                    totalUnique = 0;
                    totalRecv   = 0;
                    totalDup    = 0;
                    totalMiss   = 0;
                    esp_partition_erase_range(patchPart, 0, patchPart->size);
                }
            }
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

        xTaskCreatePinnedToCore(LoRaRxTask,  "RX",   4096, NULL, 2, NULL, 0);
        xTaskCreatePinnedToCore(ProcessTask, "PROC", 8192, NULL, 1, NULL, 1);

        Serial.println("[Node] Ready — cho nhan patch...");
    }

    void loop() { vTaskDelay(pdMS_TO_TICKS(1000)); }