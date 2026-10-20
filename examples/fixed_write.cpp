// Demonstrates io_uring registered files and registered buffers with
// libfdp, on top of the raw interface shown in async_write.cpp.
//
// Two things differ from plain liburing usage:
//  - the file to register is fdp_get_uring_fd(fd) (the NVMe char device
//    libfdp submits to), not the block device fd returned by fdp_open();
//  - fdp_sqe_set_fixed_file() must be called after the prep call, since
//    the prep resets the whole SQE.
//
// Assumes /dev/nvme1n1 has at least two reclaim unit handles (plid 0 and
// plid 1); if your device only has one, change PLIDS below to {0, 0}.
#include <assert.h>
#include <fcntl.h>
#include <fdp.h>
#include <liburing.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>

#define WRITE_SIZE 16384
#define NUM_WRITES 4

static const uint16_t PLIDS[NUM_WRITES] = {0, 1, 0, 1};

int main(void) {
	int fd, ufd, rc;
	struct io_uring ring;
	struct iovec iovs[NUM_WRITES];

	fd = fdp_open("/dev/nvme1n1", O_RDWR);
	assert(fd > 0);

	// NVMe uring-cmd passthrough needs the 128B SQE / 32B CQE ring layout.
	rc = io_uring_queue_init(NUM_WRITES, &ring,
							  IORING_SETUP_SQE128 | IORING_SETUP_CQE32);
	assert(rc == 0);

	// Register the char device libfdp submits to, at file index 0.
	ufd = fdp_get_uring_fd(fd);
	assert(ufd >= 0);
	rc = io_uring_register_files(&ring, &ufd, 1);
	assert(rc == 0);

	for (int i = 0; i < NUM_WRITES; i++) {
		rc = posix_memalign(&iovs[i].iov_base, WRITE_SIZE, WRITE_SIZE);
		assert(rc == 0);
		iovs[i].iov_len = WRITE_SIZE;
		memset(iovs[i].iov_base, 'A' + i, WRITE_SIZE);
	}
	rc = io_uring_register_buffers(&ring, iovs, NUM_WRITES);
	assert(rc == 0);

	for (int i = 0; i < NUM_WRITES; i++) {
		struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
		assert(sqe != NULL);
		// Buffer i is registered at index i.
		fdp_io_uring_prep_write_fixed(sqe, fd, iovs[i].iov_base, WRITE_SIZE,
									   (__u64)i * WRITE_SIZE, i, PLIDS[i]);
		fdp_sqe_set_fixed_file(sqe, 0);
	}

	rc = io_uring_submit(&ring);
	assert(rc == NUM_WRITES);

	for (int i = 0; i < NUM_WRITES; i++) {
		struct io_uring_cqe *cqe;
		rc = io_uring_wait_cqe(&ring, &cqe);
		assert(rc == 0);
		printf("write %d completed with res=%d\n", i, cqe->res);
		io_uring_cqe_seen(&ring, cqe);
	}

	// Unregister before fdp_close(), which closes the registered fd.
	io_uring_unregister_buffers(&ring);
	io_uring_unregister_files(&ring);
	io_uring_queue_exit(&ring);
	for (int i = 0; i < NUM_WRITES; i++)
		free(iovs[i].iov_base);
	fdp_close(fd);
	return 0;
}
