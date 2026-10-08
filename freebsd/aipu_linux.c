/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 James Kane
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * /dev/aipu for Linux programs (the Linuxulator): CIX's libnoe and Arm
 * China's libaipudrv built for Linux.  Their ioctls ('A', Linux's encoding)
 * re-encoded as FreeBSD's and run as a FreeBSD program's; mmap and poll need
 * nothing.  Other drivers' 'A' ioctls (ALSA's) are left alone.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/ioccom.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/sysproto.h>

#include <machine/../linux/linux.h>
#include <machine/../linux/linux_proto.h>
#include <compat/linux/linux_ioctl.h>

#include "aipu_freebsd.h"

#define	LINUX_IOC_SIZE(cmd)	(((cmd) >> 16) & 0x3fff)
#define	LINUX_IOC_DIR(cmd)	((cmd) >> 30)
#define	LINUX_IOC_WRITE		1	/* to the kernel: IOC_IN */
#define	LINUX_IOC_READ		2	/* from it: IOC_OUT */

#define	AIPU_LINUX_MIN		0x4100	/* AIPU_IOCTL_MAGIC 'A' */
#define	AIPU_LINUX_MAX		0x41ff

static linux_ioctl_function_t aipu_linux_ioctl;
static struct linux_ioctl_handler aipu_linux_handler = {
	aipu_linux_ioctl, AIPU_LINUX_MIN, AIPU_LINUX_MAX
};

static int
aipu_linux_ioctl(struct thread *td, struct linux_ioctl_args *args)
{
	struct ioctl_args ia;
	u_long cmd;
	u_int dir, size;

	if (!aipu_fbsd_is_aipu_fd(args->fd))
		return (ENOIOCTL);
	dir = LINUX_IOC_DIR(args->cmd);
	size = LINUX_IOC_SIZE(args->cmd);
	if (size > IOCPARM_MAX)
		return (EINVAL);
	cmd = args->cmd & 0xffff;
	if (dir == 0)
		cmd |= IOC_VOID;
	else {
		cmd |= (u_long)size << 16;
		if (dir & LINUX_IOC_WRITE)
			cmd |= IOC_IN;
		if (dir & LINUX_IOC_READ)
			cmd |= IOC_OUT;
	}
	ia.fd = args->fd;
	ia.com = cmd;
	ia.data = (caddr_t)(uintptr_t)args->arg;
	return (sys_ioctl(td, &ia));
}

static int
aipu_linux_modevent(module_t mod __unused, int cmd, void *arg __unused)
{
	switch (cmd) {
	case MOD_LOAD:
		return (linux_ioctl_register_handler(&aipu_linux_handler));
	case MOD_UNLOAD:
		return (linux_ioctl_unregister_handler(&aipu_linux_handler));
	default:
		return (0);
	}
}

DEV_MODULE(aipu_linux, aipu_linux_modevent, NULL);
MODULE_DEPEND(aipu_linux, linux64elf, 1, 1, 1);
MODULE_DEPEND(aipu_linux, aipu, 1, 1, 1);
MODULE_VERSION(aipu_linux, 1);
