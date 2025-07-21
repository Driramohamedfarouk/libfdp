#include <assert.h>
#include <fdp.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
	// given a freshly initialized device and a page size
	// prints the number of pages in the RU
	// This assumes a freshly initialized fdp device
	ssize_t rumr, prev_rumr;
	int err, fd;
	uint32_t nb_pages = 0;
	void *buffer;

	prev_rumr = 0;
	fd = fdp_open("/dev/nvme1n1", O_RDWR);
	rumr = fdp_get_remaining_bytes_in_ru(fd, 0);
	int rc = posix_memalign(&buffer, 16384, 4096);
	if (rc) {
		return -1;
	}
	ssize_t ret;
	while (rumr >= prev_rumr) {
		prev_rumr = rumr;
		ret = fdp_pwrite(fd, buffer, 16384, 0, 0);
		assert(ret == 16384);
		rumr = fdp_get_remaining_bytes_in_ru(fd, 0);
		++nb_pages;
	}
	free(buffer);
	printf("nb_pages = %u, ruamr = %ld\n", nb_pages, rumr);
	// while()
	fdp_close(fd);
	return 0;
}
