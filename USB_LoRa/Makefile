CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -D_POSIX_C_SOURCE=200809L -Iinclude -Iconfig

LDLIBS  = -lpthread

SRC_DIR   = src
BUILD_DIR = build
LOG_DIR   = log
LOG_FILE  = $(LOG_DIR)/terminal.log  $(LOG_DIR)/lora_e32.log

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
	@echo "  RUN           - ./build/sender --port /dev/? --file firm_test/? --baud ?"
	@echo "  RUN_DEFAULT   - ./build/sender (uses default port and file)"
	@echo "  make          - Build and run the sender"
	@echo "  make clean    - Remove build artifacts"
	@echo "  make rebuild  - Clean and rebuild the sender"
	@echo "  make help     - Show this help message"

.PHONY: run clean rebuild help


