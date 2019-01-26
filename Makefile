#
# Makefile for the PlainFS filesystem
#
SRC = /usr/src/linux
obj-m += plainfs.o

mod:
	make -C $(SRC) SUBDIRS=$(PWD) V=1 modules

clean:
	make -C $(SRC) SUBDIRS=$(PWD) V=1 clean
	rm -f mkfs

mkfs: mkfs.c
	gcc -o mkfs mkfs.c
