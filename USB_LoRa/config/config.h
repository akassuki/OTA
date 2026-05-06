#ifndef CONFIG_H
#define CONFIG_H

#define GW_ADDH          0x00
#define GW_ADDL          0x01
#define NODE_ADDH        0x00
#define NODE_ADDL        0x00
#define LORA_CH          20

#define CHUNK_DATA_SIZE  50  // CHUNK_SIZE 
#define MAX_RETRY        5
#define PORT_DEFAULT     "/dev/ttyUSB0"
#define FILE_DEFAULT     "firm_test/firmware.bin"
#define E32_BAUD         9600    // baud



#define ACK_TIMEOUT_MS   3000    // 
#define ACK_OK           0xAA   // 
#define ACK_NACK         0xFF   // 


#endif // CONFIG_H