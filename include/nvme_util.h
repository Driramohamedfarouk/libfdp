/** This is adapted from the Cachelib source code
https://github.com/facebook/CacheLib */

#include "util.h"

const char nvme_dev_prefix[10] = "/dev/nvme";
const char nvme_generic_dev_prefix[8] = "/dev/ng";

bool is_valid_nvme_device(const char *bdev_name) {
	const char *pattern = "^/dev/nvme[0-9]+n[0-9]+(p[0-9]+)?$";
	regex_t regex;
	int ret;

	ret = regcomp(&regex, pattern, REG_EXTENDED);
	if (ret) {
		fprintf(stderr, "Failed to compile regex\n");
		return false;
	}

	ret = regexec(&regex, bdev_name, 0, NULL, 0);
	regfree(&regex);

	return ret == 0; // 0 means match
}

int get_nvme_char_device(const char *bdev_name, char *out_buf,
						 size_t buf_size) {
	fprintf(stderr, "get_nvme_char_dev(%s, %lu)\n", bdev_name, buf_size);
	size_t len = strlen(bdev_name);

	if (len < 11 || len > 15)
		return -1;

	if (strncmp(bdev_name, nvme_dev_prefix, sizeof(nvme_dev_prefix) - 1) != 0)
		return -1;

	size_t pos = sizeof(nvme_dev_prefix) - 1;

	if (!isdigit(bdev_name[pos++]))
		return -1;

	if (bdev_name[pos] != 'n')
		return -1;

	strcpy(out_buf, "/dev/ng");
	strncpy(out_buf + 7, bdev_name + 9, len - pos + 1);
	// The generic device name is always 2 chars shorter
	out_buf[len - 2] = '\0';

	fprintf(stderr, "%s\n", out_buf);

	return 0;
}

// responsability of caller to provide big enough buffer
static uint64_t read_numeric_from_file(const char *filename) {
	FILE *fp;
	char out_buf[1024];
	char *str;
	uint64_t res;

	fp = fopen(filename, "r");
	assert(fp != NULL);

	str = fgets(out_buf, 1024, fp);
	assert(str != NULL);

	res = atoll(out_buf);
	return res;
}

uint64_t read_dev_attr(const char *bdev_name, const char *attr) {
	char path[512];
	// + 5 is to skip /dev/ in the bdev_name
	sprintf(path, "/sys/block/%s/%s", bdev_name + 5, attr);
	XLOGF("INFO", "Reading attribute from %s", path);
	return read_numeric_from_file(path);
}

// Get the Namespace ID of an NVMe block device
int get_nvme_ns_id(const char *ns_name) {
	return read_dev_attr(ns_name, "nsid");
}

// Get the Max Transfer size in bytes for an NVMe block device
uint32_t get_max_transfer_size(const char *ns_name) {
	// max_hw_sectors_kb : This is the maximum number of kilobytes supported in
	// a single data transfer.
	// (https://www.kernel.org/doc/Documentation/block/queue-sysfs.txt)
	return (1024u * /* multiply by kb */ read_dev_attr(
						ns_name, "queue/max_hw_sectors_kb"));
}

// Get LBA shift of an NVMe block device
uint32_t get_lba_size(const char *ns_name) {
	return read_dev_attr(ns_name, "queue/logical_block_size");
}

#if 0 
// Get the partition start in bytes, cuurently this is unused
uint64_t get_part_start(const char *ns_name, const char *part_name) {
  // return (512u * /* sysfs size is in terms of linux sector size */
  //         read_dev_attr(ns_name + "/" + partName, "start"));
  // return (512u * /* sysfs size is in terms of linux sector size */
  return 512u;
}
#endif
