.PHONY: _build install uninstall clean

PREFIX = ${HOME}
V	  ?= @

_build: build
	make -C build

build:
	${V}mkdir -p build
	${V}cd build && cmake .. 

install: _build
	${V}-build/dlauncher exit
	${V}cp build/dlauncher build/dlauncher.bin ${PREFIX}/bin/

uninstall:
	${V}-dlauncher exit
	${V}rm ${PREFIX}/bin/dlauncher ${PREFIX}/bin/dlauncher.bin

clean:
	${V}-rm -rf build
