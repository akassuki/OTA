#ifndef LOGGER_H
#define LOGGER_H

#include <stdarg.h>
#include <stdint.h>

// Khởi tạo logger
void logger_init(void);

// Đóng logger
void logger_close(void);

// Ghi vào terminal.log (thay thế printf)
void log_terminal(const char* fmt, ...);

// Ghi vào protocol.log (bản tin giao tiếp)
void log_protocol(const char* direction, const char* msg);

void log_proto_tx(const char* label,const uint8_t* data, int data_len,uint16_t index);

void log_proto_rx(const char* msg);

#endif