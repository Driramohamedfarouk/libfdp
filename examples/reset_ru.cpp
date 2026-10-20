// Demonstrates fdp_reset_free_ru(): forcing a reclaim unit handle onto a
// fresh reclaim unit instead of waiting for it to fill up naturally.
//
// This writes a few chunks to placement id 0 (consuming some of its
// currently open reclaim unit), reports how much room that left, then
// resets the handle and shows that it is back on a full, fresh reclaim
// unit.
#include <assert.h>
#include <fcntl.h>
#include <fdp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WRITE_SIZE 16384
#define NUM_WRITES 4
#define PLID 0

int main(void) {
	int fd, rc;
	void *buf;
	ssize_t before, after, reset;

	fd = fdp_open("/dev/nvme1n1", O_RDWR);
	assert(fd > 0);

	rc = posix_memalign(&buf, WRITE_SIZE, WRITE_SIZE);
	assert(rc == 0);
	memset(buf, 'C', WRITE_SIZE);

	before = fdp_get_remaining_bytes_in_ru(fd, PLID);
	printf("remaining bytes in RU before writing: %zd\n", before);

	for (int i = 0; i < NUM_WRITES; i++) {
		ssize_t written =
			fdp_pwrite(fd, buf, WRITE_SIZE, (off_t)i * WRITE_SIZE, PLID);
		assert(written == WRITE_SIZE);
	}

	after = fdp_get_remaining_bytes_in_ru(fd, PLID);
	printf("remaining bytes in RU after writing %d x %d bytes: %zd\n",
		   NUM_WRITES, WRITE_SIZE, after);

	fdp_reset_free_ru(fd, PLID);

	reset = fdp_get_remaining_bytes_in_ru(fd, PLID);
	printf("remaining bytes in RU after fdp_reset_free_ru: %zd\n", reset);

	free(buf);
	fdp_close(fd);
	return 0;
}
