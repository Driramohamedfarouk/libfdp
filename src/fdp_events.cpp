// Polling/monitoring concerns: reading the FDP events log, watching a
// reclaim unit's remaining write capacity, and (currently disabled)
// background GC-event notification. Distinct from the synchronous write
// path in fdp_io.cpp.
#include "fdp.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "fdp_internal.h"
#include "nvme_types.h"
#include "util.h"

int fdp_get_events(int fd, __u8 *log, __u32 log_size) {
	fdp_dev_t *dev = get_fdp_dev(fd);
	// construct the corresponding arg type
	struct nvme_get_log_args args = {
		// .lpo = offset,
		.lpo = 0, // use 0 as offset for the moment I don't get much the use of
		// nonzero offset
		.result = NULL,
		.log = log,
		.args_size = sizeof(args),
		.fd = dev->g_fd,
		.timeout = NVME_DEFAULT_IOCTL_TIMEOUT,
		.lid = NVME_LOG_LID_FDP_EVENTS,
		.len = log_size,
		.nsid = 0,		 // nsid is not used for this command
		.csi = 0,		 // NVM Command Set Indicator
		.lsi = 1,		 // endurance group id should be 1
		.lsp = (__u8)0U, // controller events by default
		.uuidx = 0U,
	};
	return nvme_get_log(&args);
}

// currently assumes a single RUH, easy to generalize later.
// Temporarly use this an interface for testing purpouses.
int open_ru_timer(void *arg) {
	fdp_dev_t *dev = (fdp_dev_t *)arg;
	__u64 last_seen_ruamw = 0;
	__u32 open_ru = -1; // move this to be inside the dev struct
	while (1) {
		// nanosleep(); // 1 ms
		// Temporary use of api here
		ssize_t ruamw = fdp_get_remaining_bytes_in_ru(dev->bdev_fd, 0);
		if (ruamw < 0) {
			XLOGF("ERR", "fdp_get_remaining_bytes returned error");
			break;
		}
		// XXX(mfd) : This is buggy because the results of remaining media
		// writes returned by the device fluctuate
		if (ruamw > last_seen_ruamw) {
			open_ru++;
			XLOGF("INFO", "RUAMW = %ld", ruamw);
			XLOGF("INFO", "Open the new RU #%u", open_ru);
		}
		last_seen_ruamw = ruamw;
		// sleep for 50ms, an optimal strategy is to sleep depending on how many
		// raumw adaptevly reducing sleep until just busy looping without
		// spinning to obtain very exact results. Calculate error as the
		// difference between the RU size and the ruamw when I discovered the
		// new Recalaim unit. usleep(50 * 1000);
		sleep(1); // 1s, high error rate but less fluctuations.
	}
	return 0;
}

// This is the function that will be run by the deamon
/** TODO(mfd) : Usually GC is not triggered until late, adapt sleeping time
so that we don't have unecessary wakeups. */
static int gc_event_listener(void *arg) {
	int err;
	void *log;
	fdp_dev_t *dev = (fdp_dev_t *)arg;
	printf("This is the thread listener for device %s\n", dev->name);
	// posix memalign the log buffer
	err = posix_memalign(&log, 4096, 4096);
	// last seen GC timestamp
	__u64 last_seen_timestamp = 0;
	while (true) {
		// temporary use of external interface.
		err = fdp_get_events(dev->bdev_fd, (__u8 *)log, 4096);
		// Check for new event
		// parse the log and start from the end looking whether there is a new
		// event
		uint32_t nb_gc_events = *(uint32_t *)log;
		// Assume that at most one new GC event will appear here.
		// TODO(mfd) : Assert this assumption later
		// TODO(mfd) : Define the GC event struct and do appropriate casting
		assert(64 * nb_gc_events < 4096);
		__u8 *last_event_struct = ((__u8 *)log) + 64 * nb_gc_events;
		__u8 type = *last_event_struct;
		__u64 timestamp = *(__u64 *)(last_event_struct + 4);
		if (timestamp != last_seen_timestamp) {
			assert(type == 0x80);
			assert(timestamp > last_seen_timestamp);
			last_seen_timestamp = timestamp;
			dev->gc_callback();
			XLOGF("INFO", "SSD GC is triggered!!!");
		}
		err = sleep(1);
		assert(err == 0);
	}
	return 0;
}

/*
void fdp_register_gc_callback(int fd, void (*gc_callback)(void)) {
	// if this is the first registered callback fire up the deamon thread
	// use a mutex here when making the library MT-Safe.
	int err;
	fdp_dev_t *dev = get_fdp_dev(fd);
	if (dev->gc_callback == NULL) {
		dev->gc_callback = gc_callback;
		// create the event listener thread.
		err = thrd_create(&dev->gc_listener_thread, gc_event_listener,
						  (void *)dev);
		assert(err == thrd_success);
		err = thrd_detach(dev->gc_listener_thread);
		assert(err == thrd_success);
		XLOGF("INFO", "Succesfully registered a GC callback");
	}
	XLOGF("WARNING", "There is already a registered callback, for now we "
					 "support only one callback");
}
*/
