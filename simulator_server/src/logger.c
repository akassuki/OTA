#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h> 
#include <time.h>
#include <sys/stat.h>
#include <stdint.h>

#define LOG_DIR          "log"
#define TERMINAL_LOG     "log/terminal.log"
#define PROTOCOL_LOG     "log/protocol.log"

static FILE* f_terminal = NULL;
static FILE* f_protocol = NULL;

/* ======= TIMESTAMP ======= */
static void get_timestamp(char* buf, size_t len) {
    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    strftime(buf, len, "%Y-%m-%d %H:%M:%S", t);
}

/* ======= INIT ======= */
int logger_init(void) {
    // Tạo thư mục log nếu chưa có
    mkdir(LOG_DIR, 0755);

    f_terminal = fopen(TERMINAL_LOG, "w");
    if (!f_terminal) {
        fprintf(stderr, "Unable to open %s\n", TERMINAL_LOG);
        return -1;
    }

    f_protocol = fopen(PROTOCOL_LOG, "w");
    if (!f_protocol) {
        fprintf(stderr, "Unable to open %s\n", PROTOCOL_LOG);
        fclose(f_terminal);
        return -1;
    }

    // Ghi header phiên mới
    char ts[32];
    get_timestamp(ts, sizeof(ts));

    fprintf(f_terminal, "\n========================================\n");
    fprintf(f_terminal, " SESSION : %s\n", ts);
    fprintf(f_terminal, "========================================\n");

    fprintf(f_protocol, "\n========================================\n");
    fprintf(f_protocol, " SESSION : %s\n", ts);
    fprintf(f_protocol, "========================================\n");

    fflush(f_terminal);
    fflush(f_protocol);
    return 0;
}

/* ======= CLOSE ======= */
void logger_close(void) {
    if (f_terminal) { fclose(f_terminal); f_terminal = NULL; }
    if (f_protocol) { fclose(f_protocol); f_protocol = NULL; }
}

/* ======= LOG TERMINAL ======= */
void log_terminal(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    // Ghi vào file
    if (f_terminal) {
        fprintf(f_terminal, "%s", buf);
        fflush(f_terminal);
    }
}

/* ======= LOG PROTOCOL ======= */
void log_protocol(const char* direction, const char* msg) {
    if (!f_protocol) return;

    char ts[32];
    get_timestamp(ts, sizeof(ts));

    fprintf(f_protocol, "[%s] %s %s\n", ts, direction, msg);
    fflush(f_protocol);
}

/* ======= LOG PROTOCOL TX ======= */
void log_proto_tx(const char* label,const uint8_t* data, int data_len)
{
    if (!f_protocol) return;

    char ts[16];
    get_timestamp(ts, sizeof(ts));

    // In label: TX (START), TX (CHUNK:0), TX (END)
    fprintf(f_protocol, "[%s] TX %-12s", ts, label);

    // In hex bytes nếu có data
    if (data != NULL && data_len > 0) {
        for (int i = 0; i < data_len; i++) {
            fprintf(f_protocol, " %02X", data[i]);
            // Xuống dòng mỗi 16 bytes
            if ((i + 1) % 16 == 0 && i + 1 < data_len)
                fprintf(f_protocol, "\n%*s", 28, "");
        }
    }

    fprintf(f_protocol, "\n");
    fflush(f_protocol);
}

/* ======= LOG PROTOCOL RX ======= */
void log_proto_rx(const char* msg) {
    if (!f_protocol) return;

    char ts[16];
    get_timestamp(ts, sizeof(ts));

    fprintf(f_protocol, "[%s] RX  %s\n", ts, msg);
    fflush(f_protocol);
}