.PHONY: install uninstall clean

PREFIX = ${HOME}
V	  ?= @

install:
	${V}mkdir -p build
	${V}cd build && cmake .. && make
	${V}-build/dlauncher exit
	${V}cp build/dlauncher build/dlauncher.bin ${PREFIX}/bin/

uninstall:
	${V}-dlauncher exit
	${V}rm ${PREFIX}/bin/dlauncher ${PREFIX}/bin/dlauncher.bin

clean:
	${V}-rm -rf build
