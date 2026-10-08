# The Arm China Zhouyi NPU driver (CIX Sky1: Zhouyi v3), through LinuxKPI,
# built against drm-kmod (for dma-buf).

DRMKMOD?=	${.CURDIR:H:H}/drm-kmod
.include "${DRMKMOD}/kconfig.mk"

NPUDIR=	${.CURDIR:H}/driver/armchina-npu

.PATH:	${NPUDIR} ${NPUDIR}/zhouyi ${NPUDIR}/sky1 ${.CURDIR:H}/freebsd

KMOD=	aipu

SRCS=	aipu.c \
	aipu_common.c \
	aipu_dma_buf.c \
	aipu_io.c \
	aipu_irq.c \
	aipu_job_manager.c \
	aipu_mm.c \
	aipu_priv.c \
	aipu_tcb.c \
	zhouyi.c \
	v3.c \
	v3_priv.c

# In place of CIX's sky1/sky1.c.
SRCS+=	aipu_freebsd.c \
	aipu_freebsd_bus.c

SRCS+=	opt_acpi.h \
	opt_platform.h \
	acpi_if.h \
	bus_if.h \
	device_if.h \
	pci_if.h \
	vnode_if.h

.include <bsd.kmod.mk>

.include "${DRMKMOD}/linuxkpi_version.mk"
.include "${DRMKMOD}/compiler_flags.mk"

CFLAGS+= -I${.CURDIR:H}/freebsd/include
CFLAGS+= -I${NPUDIR} -I${NPUDIR}/include -I${NPUDIR}/zhouyi
CFLAGS+= -I${.CURDIR:H}/freebsd
CFLAGS+= -I${DRMKMOD}/linuxkpi/gplv2/include
CFLAGS+= -I${DRMKMOD}/linuxkpi/bsd/include
CFLAGS+= -I${SYSDIR}/compat/linuxkpi/common/include
CFLAGS+= -I${SYSDIR}/compat/linuxkpi/dummy/include
CFLAGS+= -I${DRMKMOD}/include
CFLAGS+= -I${DRMKMOD}/include/uapi

CFLAGS+= '-DKBUILD_MODNAME="${KMOD}"'
CFLAGS+= '-DLINUXKPI_PARAM_PREFIX=aipu_'
CFLAGS+= ${KCONFIG:C/(.*)/-DCONFIG_\1/}
CFLAGS+= -include ${.CURDIR:H}/freebsd/include/aipu_freebsd_compat.h
# As CIX's DKMS build for the Sky1 (driver/Makefile): Zhouyi v3, no devfreq
# yet (a fixed clock).
CFLAGS+= '-DKMD_VERSION="6.2.0"' -DBUILD_ZHOUYI_V3 \
	-DCONFIG_ARMCHINA_NPU_ARCH_V3 -DCONFIG_SKY1 -DCONFIG_SYSFS

CFLAGS.aipu_freebsd_bus.c+= -DAIPU_FBSD_BUS
CWARNFLAGS+=	-Wno-format -Wno-gnu-pointer-arith -Wno-cast-qual \
		-Wno-unused-but-set-variable
