#include "logger.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>

static FILE* g_terminal_log = NULL;
static FILE* g_lora_log     = NULL;

void logger_init(void) {
    g_terminal_log = fopen("log/terminal.log", "w");
    if (!g_terminal_log)
        g_terminal_log = stderr;

    g_lora_log = fopen("log/lora_e32.log", "w");
    if (!g_lora_log)
        g_lora_log = stderr;
}

void log_terminal(const char* fmt, ...) {
    va_list args;

    // In ra stdout
    // va_start(args, fmt);
    // vprintf(fmt, args);
    // va_end(args);

    // Ghi vào terminal.log
    if (g_terminal_log && g_terminal_log != stderr) {
        va_start(args, fmt);
        vfprintf(g_terminal_log, fmt, args);
        va_end(args);
        fflush(g_terminal_log);
    }
}

void log_proto_tx(const char* label, const uint8_t* data, int len, uint16_t index) {
    if (!g_lora_log || g_lora_log == stderr) return;

    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    fprintf(g_lora_log, "\n[%02d:%02d:%02d] [%s] %d bytes | chunk=%u\n",
            t->tm_hour, t->tm_min, t->tm_sec, label, len, index);

    for (int i = 0; i < len; i++) {
        if (i % 16 == 0)
            fprintf(g_lora_log, "  %04X: ", i);
        fprintf(g_lora_log, "%02X ", data[i]);
        if ((i % 16 == 15) || (i == len - 1)) {
            int pad = 15 - (i % 16);
            for (int p = 0; p < pad; p++)
                fprintf(g_lora_log, "   ");
            fprintf(g_lora_log, " | ");
            int row_start = i - (i % 16);
            for (int j = row_start; j <= i; j++) {
                unsigned char c = data[j];
                fprintf(g_lora_log, "%c", (c >= 32 && c < 127) ? c : '.');
            }
            fprintf(g_lora_log, "\n");
        }
    }
    fflush(g_lora_log);
}