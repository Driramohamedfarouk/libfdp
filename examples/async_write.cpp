// Demonstrates the raw io_uring interface (fdp_io_uring_prep_write), as
// opposed to the synchronous fdp_pwrite() used by the other examples.
//
// The caller owns the io_uring instance here: it can batch several
// FDP-directed writes, targeting different placement ids, into one
// io_uring_submit() call and then drain the completions, instead of
// paying a syscall round trip per write like fdp_pwrite() does.
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

#define WRITE_SIZE 16384
#define NUM_WRITES 4

static const uint16_t PLIDS[NUM_WRITES] = {0, 1, 0, 1};

int main(void) {
	int fd, rc;
	struct io_uring ring;
	void *bufs[NUM_WRITES];

	fd = fdp_open("/dev/nvme1n1", O_RDWR);
	assert(fd > 0);

	// NVMe uring-cmd passthrough needs the 128B SQE / 32B CQE ring layout.
	rc = io_uring_queue_init(NUM_WRITES, &ring,
							  IORING_SETUP_SQE128 | IORING_SETUP_CQE32);
	assert(rc == 0);

	for (int i = 0; i < NUM_WRITES; i++) {
		rc = posix_memalign(&bufs[i], WRITE_SIZE, WRITE_SIZE);
		assert(rc == 0);
		memset(bufs[i], 'A' + i, WRITE_SIZE);

		struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
		assert(sqe != NULL);
		fdp_io_uring_prep_write(sqe, fd, bufs[i], WRITE_SIZE,
								 (__u64)i * WRITE_SIZE, PLIDS[i]);
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

	for (int i = 0; i < NUM_WRITES; i++)
		free(bufs[i]);
	io_uring_queue_exit(&ring);
	fdp_close(fd);
	return 0;
}
