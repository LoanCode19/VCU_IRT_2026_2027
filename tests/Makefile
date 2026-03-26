CC := gcc

TEST_DIR := .
APP_DIR := ..
FREERTOS_DIR := $(abspath $(APP_DIR)/../..)
FREERTOS_PLUS_DIR := $(abspath $(APP_DIR)/../../../FreeRTOS-Plus)

INCLUDES := -I$(APP_DIR)/include
INCLUDES += -I$(FREERTOS_DIR)/Source/include
INCLUDES += -I$(FREERTOS_DIR)/Source/portable/ThirdParty/GCC/Posix
INCLUDES += -I$(FREERTOS_DIR)/Source/portable/ThirdParty/GCC/Posix/utils
INCLUDES += -I$(FREERTOS_DIR)/Demo/Common/include
INCLUDES += -I$(FREERTOS_PLUS_DIR)/Source/FreeRTOS-Plus-Trace/Include

CFLAGS := -std=c11 -O0 -g3 -Wall -Wextra -Wno-unused-parameter
CPPFLAGS := $(INCLUDES) -DprojCOVERAGE_TEST=0

BUILD_DIR := $(TEST_DIR)/build

TEST_UTILS_BIN := $(BUILD_DIR)/test_utils
TEST_THROTTLE_BIN := $(BUILD_DIR)/test_throttle

TEST_COMMON_SRCS := $(TEST_DIR)/test_common.c

TEST_UTILS_SRCS := $(APP_DIR)/src/utils.c $(APP_DIR)/src/throttle.c $(TEST_DIR)/test_utils.c $(TEST_COMMON_SRCS)
TEST_THROTTLE_SRCS := $(APP_DIR)/src/throttle.c $(TEST_DIR)/test_throttle.c $(TEST_COMMON_SRCS)

.PHONY: all clean run-utils run-throttle

all: $(TEST_UTILS_BIN) $(TEST_THROTTLE_BIN)

$(TEST_UTILS_BIN): $(TEST_UTILS_SRCS) $(TEST_DIR)/test_utils.h $(TEST_DIR)/test_common.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_UTILS_SRCS) -o $@

$(TEST_THROTTLE_BIN): $(TEST_THROTTLE_SRCS) $(TEST_DIR)/test_throttle.h $(TEST_DIR)/test_common.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TEST_THROTTLE_SRCS) -o $@

run-utils: $(TEST_UTILS_BIN)
	$(TEST_UTILS_BIN)

run-throttle: $(TEST_THROTTLE_BIN)
	$(TEST_THROTTLE_BIN)

clean:
	rm -rf $(BUILD_DIR)
