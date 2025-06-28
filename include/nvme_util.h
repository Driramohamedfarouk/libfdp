
/** This is from the Cachelib source code 
https://github.com/facebook/CacheLib */

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
  // max_hw_sectors_kb : This is the maximum number of kilobytes supported in a
  // single data transfer.
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

