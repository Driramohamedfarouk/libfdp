#include <assert.h>
#include <fcntl.h>
#include <fdp.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>


int main(int argc, char *argv[]) {
	int err, fd;
	uint32_t nb_pages = 0;
	uint8_t *buffer;
	void *bp;
	uint32_t nb_gc_events = 0;

	fd = fdp_open("/dev/nvme1n1", O_RDWR);
	assert(fd > 0);
	int rc = posix_memalign(&bp, 16384, 4096);
	if (rc) {
		return -1;
	}
	buffer = (uint8_t *)bp;
	ssize_t ret;
	err = fdp_get_events(fd, buffer, 4096);
	if (err != 0) {
		printf("Failed to get fdp events\n");
	}
	nb_gc_events = *(uint32_t *)buffer;
	printf("Number of FDP Events is %u\n", nb_gc_events);
	for (uint32_t e = 0; e < nb_gc_events; e++) {
		// Events start after the 64-byte log header.
		uint8_t type = *(uint8_t *)(buffer + 64 * (e + 1));
		uint64_t timestamp = *(uint64_t *)(buffer + 64 * (e + 1) + 4);
		time_t rawtime = (time_t)timestamp;
		struct tm *info;

		info = localtime(&rawtime);

		char buffer[80];
		strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S UTC", info);
		printf("Date and Time (Local): %s\n", buffer);
		if (type == 0x80) {
			printf("%lu\n", timestamp);
		}
	}
	free(buffer);
	// open_ru_timer((void *)&dev);
	fdp_close(fd);
	return 0;
}
