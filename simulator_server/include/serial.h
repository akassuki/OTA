#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>
#include <stddef.h>

int  serial_open(const char* port, int baud);
void serial_close(int fd);
int  serial_write(int fd, const uint8_t* buf, size_t len);
int  serial_write_str(int fd, const char* str);
int  serial_readline(int fd, char* buf, size_t maxlen, int timeout_ms);

#endif














// doc ghi cong com