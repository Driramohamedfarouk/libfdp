// Demonstrates fdp_sqe_set_plid(): retargeting the placement id of an
// io_uring SQE that has already been prepared, without rebuilding the
// whole command.
//
// This matters when the placement decision is made after the write is
// staged (e.g. a caching layer that decides hot/cold just before
// submission): fdp_io_uring_prep_write() below stages the SQE against
// plid 0, then fdp_sqe_set_plid() overrides that choice to plid 1 right
// before it is submitted.
#include <assert.h>
#include <fcntl.h>
#include <fdp.h>
#include <liburing.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WRITE_SIZE 16384

int main(void) {
	int fd, rc;
	struct io_uring ring;
	void *buf;

	fd = fdp_open("/dev/nvme1n1", O_RDWR);
	assert(fd > 0);

	rc = io_uring_queue_init(1, &ring,
							  IORING_SETUP_SQE128 | IORING_SETUP_CQE32);
	assert(rc == 0);

	rc = posix_memalign(&buf, WRITE_SIZE, WRITE_SIZE);
	assert(rc == 0);
	memset(buf, 'B', WRITE_SIZE);

	struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
	assert(sqe != NULL);

	// Stage the write for placement id 0...
	fdp_io_uring_prep_write(sqe, fd, buf, WRITE_SIZE, 0, 0);
	// ...then change our mind and redirect it to placement id 1.
	fdp_sqe_set_plid(sqe, 1);

	rc = io_uring_submit(&ring);
	assert(rc == 1);

	struct io_uring_cqe *cqe;
	rc = io_uring_wait_cqe(&ring, &cqe);
	assert(rc == 0);
	printf("write completed with res=%d (targeted plid 1)\n", cqe->res);
	io_uring_cqe_seen(&ring, cqe);

	free(buf);
	io_uring_queue_exit(&ring);
	fdp_close(fd);
	return 0;
}
