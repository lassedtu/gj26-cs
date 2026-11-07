# Build of the car synth library
# Use "make" to build build/libcarsynth.a
# Use "make demo" to build the demo, "make run" to build and start it
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

# Build of the demo. The demo needs raylib.
# Set RAYLIB to the folder that has include/ and lib/ of raylib.
RAYLIB ?= /opt/homebrew
DEMO := $(BUILD_DIR)/car_synth_demo

demo: $(DEMO)

$(DEMO): demo/main.c $(LIBRARY)
	$(CC) $(CPPFLAGS) -I$(RAYLIB)/include $(CFLAGS) demo/main.c -o $@ \
		$(LIBRARY) -L$(RAYLIB)/lib -Wl,-rpath,$(RAYLIB)/lib -lraylib -lm

# The demo loads its sounds from demo/assets, so it must start in the project root
run: $(DEMO)
	./$(DEMO)

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPENDENCIES)

.PHONY: all demo run clean
