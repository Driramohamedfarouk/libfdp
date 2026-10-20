// Demonstrates checking every documented fdp_open() failure and reporting
// a specific message for each. fdp_open() also passes through errno from
// the plain open(2) calls it wraps (e.g. ENOENT, ENODEV, EBUSY if the
// device node itself can't be opened) and from io_uring_queue_init(), so
// this falls back to strerror() for anything not explicitly handled.
//
// Pass a device path to try; defaults to /dev/nvme1n1. Try a bogus path,
// a non-NVMe block device, or running without root to see the different
// error messages.
#include <errno.h>
#include <fcntl.h>
#include <fdp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
	const char *bdev_name = argc > 1 ? argv[1] : "/dev/nvme1n1";

	int fd = fdp_open(bdev_name, O_RDWR);
	if (fd < 0) {
		switch (errno) {
		case EINVAL:
			fprintf(stderr, "%s: not a valid NVMe block device name\n",
					bdev_name);
			break;
		case EACCES:
		case EPERM:
			fprintf(stderr,
					"%s: insufficient privileges to issue NVMe passthrough "
					"commands (try running as root)\n",
					bdev_name);
			break;
		case ENAMETOOLONG:
			fprintf(stderr, "%s: device name is too long\n", bdev_name);
			break;
		case ENOMEM:
			fprintf(stderr, "%s: allocation failed opening the device\n",
					bdev_name);
			break;
		case EIO:
			fprintf(stderr, "%s: failed to read NVMe device attributes\n",
					bdev_name);
			break;
		case ENOTSUP:
			fprintf(stderr, "%s: device/namespace does not support FDP\n",
					bdev_name);
			break;
		default:
			fprintf(stderr, "%s: fdp_open failed: %s\n", bdev_name,
					strerror(errno));
			break;
		}
		exit(EXIT_FAILURE);
	}

	printf("%s: opened successfully (fd=%d)\n", bdev_name, fd);
	fdp_close(fd);
	return 0;
}
