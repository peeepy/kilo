# Compiler
CC = gcc

# Compiler flags
# -Wall: Enable common warnings
# -Wextra: Enable extra warnings
# -pedantic: Issue warnings required by standard C
# -std=c99: Use the C99 standard
# Feature test macros for modern POSIX/GNU features
# -g: Add debug information
CFLAGS = -Wall -Wextra -pedantic -std=c99 -g \
         -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_GNU_SOURCE -I/home/poppy/lua-5.4.7/src

# Linker flags
LDFLAGS = -L/home/poppy/lua-5.4.7/src

LIBS = -llua -lm

# Executable name
TARGET = kilo

# Source directory
SRC_DIR = src

# Object directory (optional, keeps things tidy)
OBJ_DIR = obj

# Find all .c files in the source & lua directory
SOURCES := $(wildcard $(SRC_DIR)/*.c $(SRC_DIR)/lua/*.c)

# Create object file names by replacing src/ with obj/ and .c with .o
OBJECTS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SOURCES))

# Default target: build the executable
all: $(TARGET)

# Rule to link the executable
$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) $(LIBS) -o $(TARGET)

# Rule to compile source files into object files
# $< is the first prerequisite (the .c file)
# $@ is the target name (the .o file)
# -Iinclude tells the compiler to look in the include/ directory for header files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c include/kilo.h | $(OBJ_DIR)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

# Create object directory if it doesn't exist
$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# Target to clean up build files
clean:
	rm -rf $(OBJ_DIR) $(TARGET)

.PHONY: all clean