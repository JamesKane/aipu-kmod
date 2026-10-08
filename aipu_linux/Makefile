# /dev/aipu for Linux programs, under the Linuxulator.

.PATH:	${.CURDIR:H}/freebsd

KMOD=	aipu_linux
SRCS=	aipu_linux.c

CFLAGS+= -I${.CURDIR:H}/freebsd

.include <bsd.kmod.mk>
