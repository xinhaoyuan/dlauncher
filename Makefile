.PHONY: _build install uninstall clean

PREFIX ?= /usr/local
V	   ?= @

_build: build
	make -C build

build:
	${V}mkdir -p build
	${V}cd build && cmake .. 

install: _build
	${V}install -m 0755 build/dlauncher ${PREFIX}/bin/
	${V}install -m 0755 build/dlauncher.bin ${PREFIX}/bin/

uninstall:
	${V}rm ${PREFIX}/bin/dlauncher ${PREFIX}/bin/dlauncher.bin

clean:
	${V}-rm -rf build
