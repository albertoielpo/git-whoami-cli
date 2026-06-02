# Makefile
# Automatic variables
# ----------------------
# $@	The target name	git-whoami
# $^	All prerequisites, space-separated	git-whoami.c
# $<	The first prerequisite only
# $*	The stem (filename without extension, used in pattern rules)
#

CC      = gcc
# CFLAGS  = -Wextra -Wall -Wpedantic -O2 -g -std=c99
CFLAGS  = -static -Wextra -Wall -Wpedantic -O2 -g -std=c99
TARGET  = git-whoami
SRCS    = git-whoami.c

.PHONY: all run clean install

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o bin/$@ $^

run: $(TARGET)
	./bin/$(TARGET)

clean:
	rm -f $(TARGET) *.o

install:
	cp ./bin/$(TARGET) ~/.local/bin/$(TARGET)
