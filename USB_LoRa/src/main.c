#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "logger.h"
#include "file.h"
#include "lora_e32.h"

static void print_usage(const char* prog) {
    fprintf(stderr,
        "Usage: %s --port <port> --file <patch.bin> [--baud <baud>]\n"
        "  --port : serial port cua E32, e.g. /dev/ttyUSB0\n"
        "  --file : duong dan toi file patch\n"
        "  --baud : baud rate (mac dinh 9600)\n",
        prog);
}

static void print_progress(uint16_t done, uint16_t total,
                            size_t bytes_sent, double elapsed)
{
    float  pct    = (float)done / total * 100.0f;
    int    filled = (int)(pct / 2.0f);
    double speed  = elapsed > 0 ? bytes_sent / elapsed : 0;

    printf("\r  [");
    for (int i = 0; i < 50; i++) {
        if      (i < filled - 1) printf("=");
        else if (i == filled - 1) printf(">");
        else                      printf(".");
    }
    printf("] %5.1f%% | %u/%u chunks | %.0f B/s   ",
           pct, done, total, speed);
    fflush(stdout);
}

int main(int argc, char* argv[]) {
    logger_init();

    const char* port     = PORT_DEFAULT;
    const char* filepath = FILE_DEFAULT;
    int         baud     = E32_BAUD;

    // ── Parse arguments ──────────────────────────────────────
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc)
            port = argv[++i];
        else if (strcmp(argv[i], "--file") == 0 && i + 1 < argc)
            filepath = argv[++i];
        else if (strcmp(argv[i], "--baud") == 0 && i + 1 < argc)
            baud = atoi(argv[++i]);
        else {
            fprintf(stderr, "Invalid argument: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    if (!port || !filepath) {
        print_usage(argv[0]);
        return 1;
    }

    // ── Đọc file patch ────────────────────────────────────────
    size_t   file_size = 0;
    uint8_t* file_data = file_read_all(filepath, &file_size);
    if (!file_data) return 1;

    uint16_t total_chunks = (uint16_t)
        ((file_size + CHUNK_DATA_SIZE - 1) / CHUNK_DATA_SIZE);

    log_terminal("=============================================\n");
    log_terminal("  File    : %s\n",            filepath);
    log_terminal("  Size    : %zu bytes\n",     file_size);
    log_terminal("  Chunks  : %u (%d bytes/chunk)\n",
                 total_chunks, CHUNK_DATA_SIZE);
    log_terminal("  Port    : %s @ %d baud\n",  port, baud);
    log_terminal("  Node    : %02X:%02X CH=%d\n",
                 NODE_ADDH, NODE_ADDL, LORA_CH);
    log_terminal("=============================================\n\n");

    // ── Mở E32 ───────────────────────────────────────────────
    int fd = lora_open(port, baud);
    if (fd < 0) { file_free(file_data); return 1; }

    // ── Gửi từng chunk ───────────────────────────────────────
    log_terminal("[LORA] Bat dau gui %u chunks...\n\n", total_chunks);

    struct timespec ts_start, ts_now;
    clock_gettime(CLOCK_MONOTONIC, &ts_start);

    for (uint16_t i = 0; i < total_chunks; i++) {
        size_t  offset = (size_t)i * CHUNK_DATA_SIZE;
        uint8_t len    = (uint8_t)((file_size - offset) > CHUNK_DATA_SIZE
                                    ? CHUNK_DATA_SIZE
                                    : (file_size - offset));
        int success = 0;

        for (int r = 0; r < MAX_RETRY; r++) {
            // Gửi chunk
            if (lora_send_chunk(fd, i, total_chunks,
                                file_data + offset, len) != 0) {
                log_terminal("[ERROR] Khong gui duoc chunk %u\n", i);
                break;
            }

            // Chờ E32 chuyển sang RX + Node xử lý + gửi ACK
            usleep(800000);  // 300ms — khớp với Node ESP32

            int ack = lora_wait_ack(fd, i, ACK_TIMEOUT_MS);

            if (ack == 0xAA) {
                success = 1;
                break;
            } else if (ack == 0xFF) {
                log_terminal("[LORA] NACK chunk %u, retry %d/%d\n",
                             i, r + 1, MAX_RETRY);
            } else {
                log_terminal("[LORA] No ACK chunk %u, retry %d/%d\n",
                             i, r + 1, MAX_RETRY);
            }

            usleep(500000);  // 500ms trước khi retry
        }

        if (!success) {
            log_terminal("\n[FAIL] Chunk %u failed after %d retries. Stopping.\n",
                         i, MAX_RETRY);
            lora_close(fd);
            file_free(file_data);
            return 1;
        }

        clock_gettime(CLOCK_MONOTONIC, &ts_now);
        double elapsed = (ts_now.tv_sec  - ts_start.tv_sec) +
                         (ts_now.tv_nsec - ts_start.tv_nsec) / 1e9;
        print_progress(i + 1, total_chunks, offset + len, elapsed);
    }

    printf("\n");

    // ── Thống kê ─────────────────────────────────────────────
    clock_gettime(CLOCK_MONOTONIC, &ts_now);
    double total_time = (ts_now.tv_sec  - ts_start.tv_sec) +
                        (ts_now.tv_nsec - ts_start.tv_nsec) / 1e9;

    // log_terminal("\n=============================================\n");
    // log_terminal("  HOAN THANH!\n");
    // log_terminal("  Thoi gian : %.1f giay\n",             total_time);
    // log_terminal("  Toc do    : %.0f bytes/s\n",          file_size / total_time);
    // log_terminal("  Da gui    : %zu bytes / %u chunks\n", file_size, total_chunks);
    // log_terminal("=============================================\n");

    log_terminal("\n=============================================\n");
    log_terminal("  Completed!\n");
    log_terminal("  Time : %.1f seconds\n",        total_time);
    log_terminal("  Speed   : %.0f bytes/s\n",       file_size / total_time);
    log_terminal("  Sent   : %zu bytes / %u chunks\n", file_size, total_chunks);
    log_terminal("=============================================\n");
    printf("\n=============================================\n");
    printf("  Completed!\n");
    printf("  Time : %.1f seconds\n",        total_time);
    printf("  Speed   : %.0f bytes/s\n",       file_size / total_time);
    printf("  Sent   : %zu bytes / %u chunks\n", file_size, total_chunks);
    printf("=============================================\n");
    lora_close(fd);
    file_free(file_data);
    return 0;
}