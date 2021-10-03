EXEC_PROG := part-y
BUILD_DIR := ./build
SRCS      := backup.c bcd.c disk.c file.c partition.c part-y.c sha3.c tools.c win_mbr2gpt.c
OBJS      := $(SRCS:%=$(BUILD_DIR)/%.o)
INC_DIRS  := ./inc
INC_FLAGS := $(addprefix -I,$(INC_DIRS))
CFLAGS    := $(INC_FLAGS) -D__USE_GNU -DNDEBUG -D_LINUX -I./inc -O3 -fomit-frame-pointer \
             -pedantic -Wall -Wextra -Werror -c -fmessage-length=0 -Wno-format-truncation \
             -Wno-stringop-truncation -fPIC -fstack-protector-all -Wformat=2 -Wformat-security \
             -Wstrict-overflow -pthread
LDFLAGS   := -fPIC -lpthread -ldl
CC        := gcc
LINK      := gcc

$(BUILD_DIR)/$(EXEC_PROG): $(OBJS)
	$(LINK) $(OBJS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.c.o: ./src/%.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)
