#include "protocol.h"
#include "serial.h"
#include "logger.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define MAX_RETRY    5
#define RESP_BUFLEN  64

static int g_paused = 0; 



// static void wait_line_silence(int fd, int silence_ms) {
//     char buf[RESP_BUFLEN];
//     // Đọc và bỏ qua tất cả data đến khi không còn gì trong silence_ms
//     while (1) {
//         int ret = serial_readline(fd, buf, sizeof(buf), silence_ms);
//         if (ret < 0) break;  // Timeout = im lặng = sẵn sàng
//         log_terminal("   [DRAIN] %s\n", buf);
//     }
// }


static int read_response(int fd, char* buf, int buflen, int timeout_ms) {
    while (1) {
        int ret = serial_readline(fd, buf, buflen, timeout_ms);
        if (ret < 0) return ret;  // timeout hoặc lỗi

        log_proto_rx(buf);
        log_terminal("   ◄ RX: %s\n", buf);

        if (strcmp(buf, "WAIT") == 0) {
            log_terminal("   [FLOW] ESP32 buffer full → pausing\n");
            g_paused = 1;
            continue;  // Đọc tiếp, chờ response thực sự
        }

        if (strcmp(buf, "RESUME") == 0) {
            log_terminal("   [FLOW] ESP32 buffer ready → resuming\n");
            g_paused = 0;
            continue;  // Đọc tiếp
        }

        return ret;  // Response thực sự (OK, READY, ERROR,...)
    }
}

static void wait_if_paused(int fd) {
    if (!g_paused) return;

    log_terminal("   [FLOW] Waiting for RESUME from ESP32...\n");

    char buf[RESP_BUFLEN];
    while (g_paused) {
        int ret = serial_readline(fd, buf, sizeof(buf), 30000);
        if (ret < 0) {
            log_terminal("   [WARN] Timeout waiting for RESUME, retrying...\n");
            continue;
        }

        log_proto_rx(buf);
        log_terminal("   ◄ RX: %s\n", buf);

        if (strcmp(buf, "RESUME") == 0) {
            log_terminal("   [FLOW] Got RESUME → continuing\n");
            g_paused = 0;
        }
        // WAIT lặp lại → tiếp tục chờ
    }
}

/* ======= CHỜ VÀ KIỂM TRA RESPONSE ======= */
int proto_wait_response(int fd, const char* expected, int timeout_ms) {
    char buf[RESP_BUFLEN];
    int  ret = read_response(fd, buf, sizeof(buf), timeout_ms);

    if (ret == -2) {
        log_terminal("   [TIMEOUT] Waiting for '%s'\n", expected);
        log_proto_rx("TIMEOUT");
        return -1;
    }
    if (ret < 0) {
        log_terminal("   [ERROR] Failed to read response\n");
        log_proto_rx("ERROR:READ");
        return -1;
    }

    // Log RX từ ESP32
    // log_proto_rx(buf);
    // log_terminal("   ◄ RX: %s\n", buf);

    // Kiểm tra response đúng không
    if (strncmp(buf, expected, strlen(expected)) == 0)
        return 0;

    // ESP32 báo lỗi
    if (strncmp(buf, "ERROR", 5) == 0) {
        log_terminal("   [ERROR] ESP32: %s\n", buf);
        return -1;
    }

    log_terminal("   [ERROR] Unexpected response: got '%s', expected '%s'\n",
                 buf, expected);
    return -1;
}

/* ======= SEND START ======= */
int proto_send_start(int fd,
                     size_t   file_size,
                     uint16_t total_chunks,
                     uint32_t crc32)
{
    char cmd[64];
    int  cmd_len = snprintf(cmd, sizeof(cmd),
                            "START:%zu:%u:%08X\n",
                            file_size, total_chunks, crc32);

    for (int r = 0; r < MAX_RETRY; r++) {
        log_terminal("=> Sending START (attempt %d): %s", r + 1, cmd);
        log_proto_tx("START", (const uint8_t*)cmd, cmd_len);

        serial_flush_input(fd); 
        serial_write_str(fd, cmd);

        if (proto_wait_response(fd, "READY", 5000) == 0) {
            log_terminal("   [OK] ESP32 ready\n");
            g_paused = 0;
            return 0;
        }

        log_terminal("   Retrying...\n");
    }

    log_terminal("[FAIL] START failed after %d attempts\n", MAX_RETRY);
    return -1;
}

/* ======= SEND CHUNK ======= */
int proto_send_chunk(int fd,
                     uint16_t       index,
                     const uint8_t* data,
                     uint8_t        len)
{
    uint32_t chunk_crc = crc32_chunk(data, len);

    char expected_ok[32];
    snprintf(expected_ok, sizeof(expected_ok), "OK:%u:%08X", index, chunk_crc);

    char header[48];
    snprintf(header, sizeof(header),
             "CHUNK:%u:%u:%08X\n", index, len, chunk_crc);

    char label[32];
    snprintf(label, sizeof(label), "CHUNK:%u", index);

    for (int r = 0; r < MAX_RETRY; r++) {
        wait_if_paused(fd);
        serial_flush_input(fd);

        log_proto_tx(label, data, len);
        log_terminal("   ► TX: %s", header);

        serial_write_str(fd, header);
        serial_write(fd, data, len);

        // Verify: ESP32 phải reply đúng "OK:index:CRC32"
        if (proto_wait_response(fd, expected_ok, 10000) == 0)
            return 0;

        log_terminal("   [WARN] Chunk %u retry %d/%d\n",
                     index, r + 1, MAX_RETRY);
    }

    log_terminal("[FAIL] Chunk %u failed after %d attempts\n",
                 index, MAX_RETRY);
    return -1;
}

int proto_send_end(int fd, uint32_t crc32) {
    char cmd[32];
    int  cmd_len = snprintf(cmd, sizeof(cmd), "END:%08X\n", crc32);

    if (cmd_len < 0 || cmd_len >= (int)sizeof(cmd)) {
        log_terminal("[ERROR] snprintf failed\n");
        return -1;
    }

    // Expect: "DONE:CRC32"
    char expected_done[32];
    snprintf(expected_done, sizeof(expected_done), "DONE:%08X", crc32);

    for (int r = 0; r < MAX_RETRY; r++) {
        log_terminal("=> Sending END (attempt %d): %s", r + 1, cmd);
        log_proto_tx("END", (const uint8_t*)cmd, cmd_len);

        serial_flush_input(fd);
        serial_write_str(fd, cmd);

        if (proto_wait_response(fd, expected_done, 30000) == 0) {
            log_terminal("   [OK] Transfer complete\n");
            return 0;
        }

        log_terminal("   Retrying...\n");
    }

    log_terminal("[FAIL] END failed after %d attempts\n", MAX_RETRY);
    return -1;
}