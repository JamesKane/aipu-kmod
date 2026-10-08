/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Phase 1 test of /dev/aipu: capabilities, driver version, and a buffer
 * allocated, mapped, written and read back, and freed.
 *
 * Usage: aiputest [bytes [laststep]]: steps 1 QUERY_CAP, 2 version,
 * 3 REQ_BUF, 4 mmap, 5 write/read, 6 FREE_BUF.  Each step is logged to
 * /var/log/aiputest.log, synced, before it runs: a hang leaves the step.
 */
#include <sys/types.h>
#include <sys/mman.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "armchina_aipu.h"

static int logfd = -1;

static void
step(int n, const char *what)
{
	char buf[128];
	int len;

	len = snprintf(buf, sizeof(buf), "step %d: %s\n", n, what);
	(void)write(logfd, buf, len);
	(void)fsync(logfd);
	printf("%s", buf);
}

int
main(int argc, char **argv)
{
	struct aipu_buf_request req;
	struct aipu_cap cap;
	char ver[16];
	size_t bytes, i;
	uint32_t *p;
	int fd, bad = 0, last;

	setvbuf(stdout, NULL, _IONBF, 0);
	bytes = argc > 1 ? strtoul(argv[1], NULL, 0) : 1 << 20;
	last = argc > 2 ? atoi(argv[2]) : 6;
	logfd = open("/var/log/aiputest.log", O_WRONLY | O_CREAT | O_APPEND,
	    0644);
	if (logfd < 0)
		err(1, "log");
	step(0, "open");
	if ((fd = open("/dev/aipu", O_RDWR)) < 0)
		err(1, "open");

	step(1, "QUERY_CAP");
	memset(&cap, 0, sizeof(cap));
	if (ioctl(fd, AIPU_IOCTL_QUERY_CAP, &cap) != 0)
		err(1, "QUERY_CAP");
	printf("partitions %u, ASIDs %u (bases %#jx %#jx %#jx %#jx), "
	    "homogeneous %u, GM %#x/%#x\n", cap.partition_cnt, cap.asid_cnt,
	    (uintmax_t)cap.asid_base[0], (uintmax_t)cap.asid_base[1],
	    (uintmax_t)cap.asid_base[2], (uintmax_t)cap.asid_base[3],
	    cap.is_homogeneous, cap.gm0_size, cap.gm1_size);

	if (last < 2)
		return (0);
	step(2, "GET_DRIVER_VERSION");
	memset(ver, 0, sizeof(ver));
	if (ioctl(fd, AIPU_IOCTL_GET_DRIVER_VERSION, ver) != 0)
		err(1, "GET_DRIVER_VERSION");
	printf("driver %s\n", ver);

	if (last < 3)
		return (0);
	step(3, "REQ_BUF");
	memset(&req, 0, sizeof(req));
	req.bytes = bytes;
	req.align_in_page = 1;
	if (ioctl(fd, AIPU_IOCTL_REQ_BUF, &req) != 0)
		err(1, "REQ_BUF");
	printf("buffer: %ju bytes at NPU address %#jx (offset %#jx, ASID %u)\n",
	    (uintmax_t)req.desc.bytes, (uintmax_t)req.desc.pa,
	    (uintmax_t)req.desc.dev_offset, req.desc.asid);

	if (last < 4)
		goto free;
	step(4, "mmap");
	p = mmap(NULL, req.desc.bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
	    req.desc.pa);
	if (p == MAP_FAILED)
		err(1, "mmap");
	if (last < 5)
		goto unmap;
	step(5, "write/read");
	for (i = 0; i < req.desc.bytes / 4; i++)
		p[i] = (uint32_t)(i * 2654435761u);
	for (bad = 0, i = 0; i < req.desc.bytes / 4; i++)
		if (p[i] != (uint32_t)(i * 2654435761u))
			bad++;
	printf("mapped, wrote and read back %ju words: %d bad\n",
	    (uintmax_t)(req.desc.bytes / 4), bad);
unmap:
	munmap(p, req.desc.bytes);
free:
	step(6, "FREE_BUF");
	if (ioctl(fd, AIPU_IOCTL_FREE_BUF, &req.desc) != 0)
		err(1, "FREE_BUF");
	printf("freed\n");
	close(fd);
	return (bad != 0);
}
