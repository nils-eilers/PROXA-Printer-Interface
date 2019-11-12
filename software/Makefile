EXE = eprom
SRC = eprom.c
PREFIX ?= /usr/local

ifeq ($(WIN),y)
# build 32 bit executable
    CC = i686-w64-mingw32-gcc
# build 64 bit executable
#   CC = x86_64-w64-mingw32-gcc
   EXE := $(EXE).exe
endif


$(EXE):	$(SRC)
	$(CC) -Wall -std=c99 $< -o $@

install:
	install $(EXE) $(PREFIX)/bin
clean:
	rm -f $(EXE)

.PHONY: install clean
