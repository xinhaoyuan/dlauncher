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
	${V}mkdir -p ${PREFIX}/share/dlauncher
	${V}install -m 0644 external/calc.zsh ${PREFIX}/share/dlauncher/
	${V}install -m 0644 external/zsh_completion/completion-server.zsh ${PREFIX}/share/dlauncher/
	${V}install -m 0644 external/zsh_completion/completion-server-init.zsh ${PREFIX}/share/dlauncher/

uninstall:
	${V}-rm ${PREFIX}/bin/dlauncher ${PREFIX}/bin/dlauncher.bin 
	${V}-rm -rf ${PREFIX}/share/dlauncher/

clean:
	${V}-rm -rf build
