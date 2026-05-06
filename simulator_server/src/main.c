#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <logger.h>
#include "protocol.h"
#include "file.h"
#include "serial.h"


static void print_usage(const char* prog) {
    fprintf(stderr,
        "Usage: %s --port <port> --file <file.bin> [--baud <baud>]\n"
        "  --port  : serial port, e.g.: /dev/ttyUSB0\n"
        "  --file  : Path to .bin file\n"
        "  --baud  : baud rate (Default 115200)\n",
        prog);
}

// static void print_progress(uint16_t done, uint16_t total,
//                            size_t bytes_sent, double elapsed)
// {
//     float pct   = (float)done / total * 100.0f;
//     int   filled = (int)(pct / 2.0f);
//     double speed = elapsed > 0 ? bytes_sent / elapsed : 0;

//     printf("\r  [");
//     for (int i = 0; i < 50; i++)
//         printf(i < filled ? "=>" : ".");
//     printf("] %5.1f%% | %u/%u chunks | %.0f B/s   ",
//            pct, done, total, speed);
//     fflush(stdout);
// }

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

    log_terminal("\r  [");
    for (int i = 0; i < 50; i++) {
        if (i < filled - 1)
            log_terminal("=");
        else if (i == filled - 1)
            log_terminal(">");
        else
            log_terminal(".");
    }
    log_terminal("] %5.1f%% | %u/%u chunks | %.0f B/s   ",
                   pct, done, total, speed);
    fflush(stdout);
}

int main(int argc, char* argv[]) {
    logger_init();
    const char* port     = PORT_DEFAULT;
    const char* filepath = FILE_DEFAULT;
    int         baud     = DEFAULT_BAUD;

    // ── Parse arguments ──
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

    // ── Đọc file ──
    size_t   file_size = 0;
    uint8_t* file_data = file_read_all(filepath, &file_size);
    if (!file_data) return 1;

    uint32_t crc         = crc32_calc(file_data, file_size);
    uint16_t total_chunks = (uint16_t)((file_size + CHUNK_SIZE - 1) / CHUNK_SIZE);

    log_terminal("=============================================\n");
    log_terminal("  File       : %s\n",       filepath);
    log_terminal("  Size       : %zu bytes\n", file_size);
    log_terminal("  Chunks     : %u (max %d bytes/chunk)\n", total_chunks, CHUNK_SIZE);
    log_terminal("  CRC32      : 0x%08X\n",   crc);
    log_terminal("  Port       : %s @ %d baud\n", port, baud);
    log_terminal("=============================================\n\n");

    // ── Mở serial ──
    int fd = serial_open(port, baud);
    if (fd < 0) { file_free(file_data); return 1; }

    serial_reset_esp32(fd);  
    // Chờ ESP32 boot xong
    log_terminal("waiting for ESP32...\n");
    // sleep(2);

    char boot_buf[64];
    memset(boot_buf, 0, sizeof(boot_buf));
    int  boot_pos = 0;
    int  found    = 0;

    struct timespec t_start, t_now;
    clock_gettime(CLOCK_MONOTONIC, &t_start);

    while (!found) {
        clock_gettime(CLOCK_MONOTONIC, &t_now);
        double elapsed = (t_now.tv_sec - t_start.tv_sec) +
                        (t_now.tv_nsec - t_start.tv_nsec) / 1e9;
        if (elapsed > 10.0) {
            log_terminal("[ERROR] ESP32 not responding\n");
            serial_close(fd);
            file_free(file_data);
            return 1;
        }

        char c;
        if (read(fd, &c, 1) <= 0) continue;

        if (c == '\n' || c == '\r') {
            boot_buf[boot_pos] = '\0';

            // Trim ký tự rác ở đầu — tìm "BOOT_OK" trong chuỗi
            if (strstr(boot_buf, "BOOT_OK")) {
                log_terminal("   [BOOT] ESP32 ready\n");
                found = 1;
            }

            boot_pos = 0;
            memset(boot_buf, 0, sizeof(boot_buf));
        } else {
            if (boot_pos < (int)sizeof(boot_buf) - 1)
                boot_buf[boot_pos++] = c;
        }
    }

    serial_flush_input(fd);

    struct timespec ts_start, ts_now;
    clock_gettime(CLOCK_MONOTONIC, &ts_start);

    // ── BƯỚC 1: START ──
    if (proto_send_start(fd, file_size, total_chunks, crc) != 0) {
        fprintf(stderr, "Unable to start transmission\n");
        serial_close(fd);
        file_free(file_data);
        return 1;
    }

    // ── BƯỚC 2: Gửi từng chunk ──
    log_terminal("\nStarting to send %u chunks...\n\n", total_chunks);

    for (uint16_t i = 0; i < total_chunks; i++) {
        size_t  offset   = (size_t)i * CHUNK_SIZE;
        uint8_t len      = (uint8_t)((file_size - offset) > CHUNK_SIZE
                                     ? CHUNK_SIZE
                                     : (file_size - offset));

        if (proto_send_chunk(fd, i, file_data + offset, len) != 0) {
            fprintf(stderr, "\n Unable to send chunk %u – Stopping\n", i);
            log_terminal("\n Unable to send chunk %u – Stopping\n", i);
            serial_close(fd);
            file_free(file_data);
            return 1;
        }

        clock_gettime(CLOCK_MONOTONIC, &ts_now);
        double elapsed = (ts_now.tv_sec  - ts_start.tv_sec) +
                         (ts_now.tv_nsec - ts_start.tv_nsec) / 1e9;

        print_progress(i + 1, total_chunks, offset + len, elapsed);
    }

    printf("\n");

    log_terminal("\n");  // newline sau progress bar

    // ── BƯỚC 3: END ──
    if (proto_send_end(fd, crc) != 0) {
        fprintf(stderr, "Unable to send END\n");
        log_terminal("Unable to send END\n");
        serial_close(fd);
        file_free(file_data);
        return 1;
    }

    // ── Thống kê ──
    clock_gettime(CLOCK_MONOTONIC, &ts_now);
    double total_time = (ts_now.tv_sec  - ts_start.tv_sec) +
                        (ts_now.tv_nsec - ts_start.tv_nsec) / 1e9;

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

    serial_close(fd);
    file_free(file_data);
    return 0;
}