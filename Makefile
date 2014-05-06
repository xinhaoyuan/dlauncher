# dlauncher - dynamic launcher based on dmenu
# See LICENSE file for copyright and license details.

include config.mk

SRC = dlauncher.c draw.c dummy_plugin.c
OBJ = ${SRC:.c=.o}

all: options dlauncher.bin

options:
	@echo dlauncher build options:
	@echo "CFLAGS   = ${CFLAGS}"
	@echo "LDFLAGS  = ${LDFLAGS}"
	@echo "CC       = ${CC}"

.c.o:
	@echo CC -c $<
	@${CC} -c $< ${CFLAGS}

${OBJ}: config.mk draw.h

dlauncher.bin: dlauncher.o draw.o dummy_plugin.o
	@echo CC -o $@
	@${CC} -o $@ dlauncher.o draw.o dummy_plugin.o ${LDFLAGS}


clean:
	@echo cleaning
	@rm -f dlauncher.bin ${OBJ} dlauncher-${VERSION}.tar.gz

dist: clean
	@echo creating dist tarball
	@mkdir -p dlauncher-${VERSION}
	@cp LICENSE Makefile README config.mk dlauncher.1 draw.h ${SRC} dlauncher dlauncher-${VERSION}
	@tar -cf dlauncher-${VERSION}.tar dlauncher-${VERSION}
	@gzip dlauncher-${VERSION}.tar
	@rm -rf dlauncher-${VERSION}

install: all
	@echo installing executables to ${DESTDIR}${PREFIX}/bin
	@mkdir -p ${DESTDIR}${PREFIX}/bin
	@cp -f dlauncher dlauncher.bin ${DESTDIR}${PREFIX}/bin
	@chmod 755 ${DESTDIR}${PREFIX}/bin/dlauncher ${DESTDIR}${PREFIX}/bin/dlauncher.bin
	@echo installing manual pages to ${DESTDIR}${MANPREFIX}/man1
	@mkdir -p ${DESTDIR}${MANPREFIX}/man1
	@sed "s/VERSION/${VERSION}/g" < dlauncher.1 > ${DESTDIR}${MANPREFIX}/man1/dlauncher.1
	@chmod 644 ${DESTDIR}${MANPREFIX}/man1/dlauncher.1

uninstall:
	@echo removing executables from ${DESTDIR}${PREFIX}/bin
	@rm -f ${DESTDIR}${PREFIX}/bin/dlauncher ${DESTDIR}${PREFIX}/bin/dlauncher.bin
	@echo removing manual page from ${DESTDIR}${MANPREFIX}/man1
	@rm -f ${DESTDIR}${MANPREFIX}/man1/dlauncher.1

.PHONY: all options clean dist install uninstall
