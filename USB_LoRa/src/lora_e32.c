#include "lora_e32.h"
#include "serial.h"
#include "logger.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/time.h>

int lora_open(const char* port, int baud) {
    int fd = serial_open(port, baud);
    if (fd < 0) return -1;
    log_terminal("[LORA] Opened %s @ %d baud\n", port, baud);
    sleep(1);
    serial_flush_input(fd);
    return fd;
}

void lora_close(int fd) {
    serial_close(fd);
}

void lora_flush(int fd) {
    serial_flush_input(fd);
}

int lora_write_bytes(int fd, const uint8_t* buf, size_t len) {
    return serial_write(fd, buf, len);
}

// ── Đọc đúng `len` bytes với timeout ─────────────────────────
int lora_read_bytes(int fd, uint8_t* buf, size_t len, int timeout_ms) {
    size_t received = 0;
    struct timeval tv_start, tv_now;
    gettimeofday(&tv_start, NULL);

    while (received < len) {
        gettimeofday(&tv_now, NULL);
        int elapsed = (int)((tv_now.tv_sec  - tv_start.tv_sec)  * 1000 +
                            (tv_now.tv_usec - tv_start.tv_usec) / 1000);
        int remaining = timeout_ms - elapsed;
        if (remaining <= 0) return -2;  // timeout

        fd_set fds;
        struct timeval tv;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        tv.tv_sec  = remaining / 1000;
        tv.tv_usec = (remaining % 1000) * 1000;

        int ret = select(fd + 1, &fds, NULL, NULL, &tv);
        if (ret < 0)  return -1;  // error
        if (ret == 0) return -2;  // timeout

        ssize_t rd = read(fd, buf + received, len - received);
        if (rd < 0) return -1;
        received += (size_t)rd;
    }
    return (int)received;
}

// ── Gửi LoraChunk đến Node qua fixed transmission ─────────────
// Format: [NODE_ADDH][NODE_ADDL][LORA_CH][LoraChunk 55 bytes]
int lora_send_chunk(int fd, uint16_t index, uint16_t total,
                    const uint8_t* data, uint8_t len)
{
    uint8_t buf[3 + sizeof(LoraChunk)];

    // Fixed transmission header
    buf[0] = NODE_ADDH;
    buf[1] = NODE_ADDL;
    buf[2] = LORA_CH;

    // LoraChunk payload
    LoraChunk* pkt = (LoraChunk*)(buf + 3);
    pkt->index = index;
    pkt->total = total;
    pkt->len   = len;
    memset(pkt->data, 0, CHUNK_DATA_SIZE);
    memcpy(pkt->data, data, len);

    log_terminal("[LORA] TX chunk idx=%u/%u len=%u\n",
                 index, total - 1, len);
    log_proto_tx("CHUNK_TX", buf, sizeof(buf));

    if (lora_write_bytes(fd, buf, sizeof(buf)) < 0) {
        log_terminal("[LORA] Write error\n");
        return -1;
    }
    return 0;
}

// ── Chờ ACK từ Node ───────────────────────────────────────────
// Node gửi về AckPkt (3 bytes): [status][index_lo][index_hi]
int lora_wait_ack(int fd, uint16_t expected_index, int timeout_ms) {
    AckPkt ack;
    int ret = lora_read_bytes(fd, (uint8_t*)&ack, sizeof(AckPkt), timeout_ms);

    if (ret == -2) {
        log_terminal("[LORA] ACK timeout (expected idx=%u)\n", expected_index);
        return -1;
    }
    if (ret < 0) {
        log_terminal("[LORA] ACK read error\n");
        return -1;
    }

    log_terminal("[LORA] RX ACK status=0x%02X idx=%u (expected=%u)\n",
                 ack.status, ack.index, expected_index);
    log_proto_tx("ACK_RX", (uint8_t*)&ack, sizeof(AckPkt));

    if (ack.index != expected_index) {
        log_terminal("[LORA] ACK index mismatch: got %u expected %u\n",
                     ack.index, expected_index);
        return -1;
    }

    return ack.status;
}