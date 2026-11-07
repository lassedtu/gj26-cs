# Build of the car synth library
# Use "make" to build build/libcarsynth.a
# Use "make clean" to remove the build files

BUILD_DIR := build
OBJ_DIR := $(BUILD_DIR)/obj
LIBRARY := $(BUILD_DIR)/libcarsynth.a

SOURCES := $(sort $(wildcard src/*.c))
OBJECTS := $(SOURCES:%.c=$(OBJ_DIR)/%.o)
DEPENDENCIES := $(OBJECTS:.o=.d)

CFLAGS ?= -O2 -Wall -Wextra -std=c17
CPPFLAGS += -Iinclude -MMD -MP

all: $(LIBRARY)

$(LIBRARY): $(OBJECTS)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $(OBJECTS)

$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPENDENCIES)

.PHONY: all clean
