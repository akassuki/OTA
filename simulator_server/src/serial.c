#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "serial.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <sys/select.h>
#include <logger.h>

static speed_t baud_to_speed(int baud) {
    switch (baud) {
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        default:
            fprintf(stderr, "Baud not supported: %d, using 115200\n", baud);
            return B115200;
    }
}

int serial_open(const char* port, int baud) {
    int fd = open(port, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) {
        fprintf(stderr, "serial_open error: %s\n", strerror(errno));
        return -1;
    }

    struct termios tty;
    memset(&tty, 0, sizeof(tty));

    if (tcgetattr(fd, &tty) != 0) {
        fprintf(stderr, " tcgetattr failed: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    speed_t spd = baud_to_speed(baud);
    cfsetispeed(&tty, spd);
    cfsetospeed(&tty, spd);

    // 8N1
    tty.c_cflag &= ~PARENB;   // không parity
    tty.c_cflag &= ~CSTOPB;   // 1 stop bit
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |=  CS8;      // 8 bit data

    // Tắt flow control
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cflag |=  CREAD | CLOCAL;

    // Raw mode
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
    tty.c_oflag &= ~OPOST;

    // Blocking read
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 1;  // 100ms timeout

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        fprintf(stderr, "tcsetattr failed: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    tcflush(fd, TCIOFLUSH);
    return fd;
}

void serial_close(int fd) {
    if (fd >= 0) close(fd);
}

int serial_write(int fd, const uint8_t* buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t wr = write(fd, buf + total, len - total);
        if (wr < 0) {
            fprintf(stderr, "serial_write error: %s\n", strerror(errno));
            return -1;
        }
        total += (size_t)wr;
    }
    return (int)total;
}

int serial_write_str(int fd, const char* str) {
    return serial_write(fd, (const uint8_t*)str, strlen(str));
}

int serial_readline(int fd, char* buf, size_t maxlen, int timeout_ms) {
    size_t pos = 0;
    struct timeval tv;
    fd_set fds;

    while (pos < maxlen - 1) {
        tv.tv_sec  = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;

        FD_ZERO(&fds);
        FD_SET(fd, &fds);

        int ret = select(fd + 1, &fds, NULL, NULL, &tv);
        if (ret < 0) {
            fprintf(stderr, "select error: %s\n", strerror(errno));
            return -1;
        }
        if (ret == 0) {
            // Timeout
            buf[pos] = '\0';
            return -2;
        }

        char c;
        ssize_t rd = read(fd, &c, 1);
        if (rd <= 0) continue;

        if (c == '\n') {
            buf[pos] = '\0';
            // Trim '\r' nếu có
            if (pos > 0 && buf[pos - 1] == '\r')
                buf[--pos] = '\0';
            return (int)pos;
        }

        buf[pos++] = c;
    }

    buf[pos] = '\0';
    return (int)pos;
}