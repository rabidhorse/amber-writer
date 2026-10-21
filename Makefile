# Builds AmberWriter.exe for Windows XP and later with mingw-w64 (32-bit).
#   make            build AmberWriter.exe
#   make test       run the file format and spelling tests under Wine

CC = i686-w64-mingw32-gcc
WINDRES = i686-w64-mingw32-windres
CFLAGS = -Os -std=c99 -Wall -Wextra -Wno-unused-parameter -Wno-cast-function-type -Wno-missing-field-initializers \
         -municode -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0501 -DWINVER=0x0501 -D_WIN32_IE=0x0600 \
         -fno-stack-protector -fno-asynchronous-unwind-tables
LDFLAGS = -municode -mwindows -static-libgcc -s \
          -Wl,--major-subsystem-version,5 -Wl,--minor-subsystem-version,1 \
          -Wl,--major-os-version,5 -Wl,--minor-os-version,1 \
          -Wl,--dynamicbase,--nxcompat
LIBS = -lwinmm -lcomdlg32 -lshell32 -lgdi32 -luser32 -lkernel32 -ladvapi32

SRC = src/amber.c src/doc.c src/spell.c src/beep.c src/dict_data.c
HDR = src/doc.h src/spell.h src/beep.h src/version.h

AmberWriter.exe: $(SRC) $(HDR) build/amber.res.o
	$(CC) $(CFLAGS) $(SRC) build/amber.res.o -o $@ $(LDFLAGS) $(LIBS)

build/amber.res.o: res/amber.rc res/amber.manifest res/amber.ico src/version.h
	mkdir -p build
	$(WINDRES) -I res res/amber.rc -O coff -o $@

build/tests.exe: tests/tests.c src/doc.c src/spell.c src/dict_data.c $(HDR)
	mkdir -p build
	$(CC) $(filter-out -municode,$(CFLAGS)) -mconsole -Isrc tests/tests.c src/doc.c src/spell.c src/dict_data.c -o $@ -static-libgcc

test: build/tests.exe
	wine build/tests.exe

clean:
	rm -rf build AmberWriter.exe

.PHONY: test clean
