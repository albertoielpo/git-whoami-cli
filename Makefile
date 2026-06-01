# Makefile
# Automatic variables
# ----------------------
# $@	The target name	git-whoami
# $^	All prerequisites, space-separated	git-whoami.c
# $<	The first prerequisite only
# $*	The stem (filename without extension, used in pattern rules)
#

CC      = gcc
CFLAGS  = -Wextra -Wall -Wpedantic -O2 -g -std=c99
# CFLAGS  = -static -Wextra -Wall -Wpedantic -O2 -g -std=c99
TARGET  = git-whoami
SRCS    = git-whoami.c

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(SRCS)
	# clang-format -i $^
	$(CC) $(CFLAGS) -o $@ $^

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) *.o
