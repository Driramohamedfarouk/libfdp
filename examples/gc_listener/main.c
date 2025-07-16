#include <assert.h>
#include <fdp.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

void callback(void) { printf("Hello\n"); }

int main(int argc, char *argv[]) {
	fdp_dev_t dev;
	int err;
	uint32_t nb_pages = 0;
	uint8_t *buffer;
	uint32_t nb_gc_events = 0;

	err = fdp_open("/dev/nvme0n1", &dev);
	int rc = posix_memalign(&buffer, 16384, 4096);
	if (rc) {
		return -1;
	}
	ssize_t ret;
	err = fdp_get_events(&dev, buffer, 4096);
	if (err != 0) {
		printf("Failed to get fdp events\n");
	}
	nb_gc_events = *(uint32_t *)buffer;
	printf("Number of FDP Events is %u\n", nb_gc_events);
	for (uint32_t e = 0; e < nb_gc_events; e++) {
		uint8_t type = *(uint8_t *)(buffer + 64 * e);
		uint64_t timestamp = *(uint64_t *)(buffer + 64 * e + 4);
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
	// fdp_register_gc_callback(&dev, callback);
	// while(1);
	open_ru_timer((void *)&dev);
	return 0;
}
