CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -D_POSIX_C_SOURCE=200809L -Iinclude

LDLIBS  = -lpthread

SRC_DIR   = src
BUILD_DIR = build
LOG_DIR   = log
LOG_FILE  = $(LOG_DIR)/terminal.log  $(LOG_DIR)/protocol.log

TARGET  = $(BUILD_DIR)/sender
SOURCES = $(wildcard $(SRC_DIR)/*.c)
OBJECTS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SOURCES))
DEPS    = $(OBJECTS:.o=.d)

all: $(BUILD_DIR) $(LOG_DIR) $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(LOG_DIR):
	mkdir -p $(LOG_DIR)
	touch $(LOG_FILE)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $(OBJECTS) $(LDLIBS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

-include $(DEPS)

clean:
	rm -rf $(BUILD_DIR)
	rm -rf $(LOG_DIR)

rebuild: clean all

help:
	@echo "  RUN           - ./build/sender --port /dev/ttyUSB* --file firm_test/firmware.bin or ./build/sender --port /dev/ttyACM* --file firm_test/firmware.bin or ./build/sender --port /dev/ttyS* --file firm_test/firmware.bin --baud ?"
	@echo "  make          - Build and run the sender"
	@echo "  make clean    - Remove build artifacts"
	@echo "  make rebuild  - Clean and rebuild the sender"
	@echo "  make help     - Show this help message"

.PHONY: run clean rebuild help


