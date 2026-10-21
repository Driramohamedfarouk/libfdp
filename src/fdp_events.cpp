// Polling/monitoring concerns: reading the FDP events log, watching a
// reclaim unit's remaining write capacity, and background GC-event
// notification. Distinct from the synchronous write
// path in fdp_io.cpp.
//
// EXPERIMENTAL: none of this is tested; the interface may change or be
// removed without notice.
// Both interface methods here are Ad-hoc and unreliable.
#include "fdp.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <new>
#include <system_error>
#include <thread>

#include "fdp_internal.h"
#include "nvme_types.h"
#include "util.h"

int fdp_get_events(int fd, uint8_t* log, uint32_t log_size)
{
    FDP_GET_DEV_OR_RETURN(dev, fd);
    // construct the corresponding arg type
    struct nvme_get_log_args args = {
        // .lpo = offset,
        .lpo = 0,  // use 0 as offset for the moment I don't get much the use of
        // nonzero offset
        .result = NULL,
        .log = log,
        .args_size = sizeof(args),
        .fd = dev->g_fd,
        .timeout = NVME_DEFAULT_IOCTL_TIMEOUT,
        .lid = NVME_LOG_LID_FDP_EVENTS,
        .len = log_size,
        .nsid = 0,        // nsid is not used for this command
        .csi = 0,         // NVM Command Set Indicator
        .lsi = 1,         // endurance group id should be 1
        .lsp = (__u8)0U,  // controller events by default
        .uuidx = 0U,
    };
    return nvme_get_log(&args);
}

// currently assumes a single RUH, easy to generalize later.
// Temporarly use this an interface for testing purpouses.
int open_ru_timer(void* arg)
{
    fdp_dev_t* dev = (fdp_dev_t*)arg;
    __u64 last_seen_ruamw = 0;
    __u32 open_ru = -1;  // move this to be inside the dev struct
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
        if ((__u64)ruamw > last_seen_ruamw) {
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
        sleep(1);  // 1s, high error rate but less fluctuations.
    }
    return 0;
}

// FDP Events log page layout (NVMe 2.0, Get Log Page LID 0x23): a 64-byte
// header whose first dword is the number of events, followed by up to 63
// 64-byte event entries.
#define FDP_EVENTS_LOG_SIZE 4096
#define FDP_EVENT_SIZE 64
#define FDP_MAX_EVENTS (FDP_EVENTS_LOG_SIZE / FDP_EVENT_SIZE - 1)
#define FDP_EVENT_MEDIA_REALLOCATED 0x80
#define FDP_GC_POLL_INTERVAL std::chrono::seconds(1)

struct fdp_gc_listener {
    std::function<void()> callback;
    std::thread thread;
    std::mutex lock;
    std::condition_variable cv;
    bool stop = false;
    // Timestamp of the newest Media Reallocated event already reported.
    uint64_t last_seen_timestamp = 0;
    uint8_t* log = NULL;  // FDP_EVENTS_LOG_SIZE bytes, page aligned

    ~fdp_gc_listener() { free(log); }
};

// Serializes registration so two threads cannot both start a listener.
static std::mutex gc_register_lock;

// Reads the controller FDP events log and returns the number of Media
// Reallocated (i.e. device GC) events newer than l->last_seen_timestamp,
// advancing it. Returns -1 if the log could not be read.
static int poll_gc_events(fdp_dev_t* dev, fdp_gc_listener* l)
{
    if (fdp_get_events(dev->bdev_fd, l->log, FDP_EVENTS_LOG_SIZE) != 0)
        return -1;
    uint32_t nb_events = *(uint32_t*)l->log;
    if (nb_events > FDP_MAX_EVENTS)
        nb_events = FDP_MAX_EVENTS;
    int nb_new = 0;
    uint64_t newest = l->last_seen_timestamp;
    for (uint32_t e = 0; e < nb_events; e++) {
        const uint8_t* event = l->log + FDP_EVENT_SIZE * (e + 1);
        // The timestamp is in milliseconds, in the low 48 bits.
        uint64_t timestamp;
        memcpy(&timestamp, event + 4, sizeof(timestamp));
        timestamp &= ((1ULL << 48) - 1);
        if (event[0] != FDP_EVENT_MEDIA_REALLOCATED || timestamp <= l->last_seen_timestamp)
            continue;
        nb_new++;
        if (timestamp > newest)
            newest = timestamp;
    }
    l->last_seen_timestamp = newest;
    return nb_new;
}

/** TODO(mfd) : Usually GC is not triggered until late, adapt sleeping time
so that we don't have unecessary wakeups. */
static void gc_event_listener(fdp_dev_t* dev, fdp_gc_listener* l)
{
    std::unique_lock<std::mutex> guard(l->lock);
    while (!l->cv.wait_for(guard, FDP_GC_POLL_INTERVAL, [l] { return l->stop; })) {
        int nb_new = poll_gc_events(dev, l);
        if (nb_new < 0) {
            XLOGF("ERR", "%s: failed to read FDP events log", dev->name);
            continue;
        }
        // Do not hold the lock while running user code.
        guard.unlock();
        for (int i = 0; i < nb_new; i++)
            l->callback();
        guard.lock();
    }
}

int fdp_register_gc_callback(int fd, std::function<void()> callback)
{
    FDP_GET_DEV_OR_RETURN(dev, fd);
    if (!callback) {
        errno = EINVAL;
        return -1;
    }
    std::lock_guard<std::mutex> guard(gc_register_lock);
    if (dev->gc_listener != NULL) {
        errno = EBUSY;
        return -1;
    }
    fdp_gc_listener* l = new (std::nothrow) fdp_gc_listener();
    if (l == NULL || posix_memalign((void**)&l->log, 4096, FDP_EVENTS_LOG_SIZE) != 0) {
        delete l;
        errno = ENOMEM;
        return -1;
    }
    // Only report GC events that happen from now on, and fail early if the
    // device does not expose the FDP events log.
    if (poll_gc_events(dev, l) < 0) {
        delete l;
        errno = EIO;
        return -1;
    }
    l->callback = std::move(callback);
    dev->gc_listener = l;
    try {
        l->thread = std::thread(gc_event_listener, dev, l);
    } catch (const std::system_error& e) {
        dev->gc_listener = NULL;
        delete l;
        errno = e.code().value();
        return -1;
    }
    return 0;
}

// Detaches dev's listener under gc_register_lock, then stops and joins it
// outside the lock, so a callback that is still running may itself call
// fdp_register_gc_callback() without deadlocking. Returns -1 with errno set
// if there is no listener (ENOENT) or if called from the listener thread,
// i.e. from inside the callback, which would join itself (EDEADLK).
static int stop_gc_listener(fdp_dev_t* dev)
{
    fdp_gc_listener* l;
    {
        std::lock_guard<std::mutex> guard(gc_register_lock);
        l = dev->gc_listener;
        if (l == NULL) {
            errno = ENOENT;
            return -1;
        }
        if (l->thread.get_id() == std::this_thread::get_id()) {
            errno = EDEADLK;
            return -1;
        }
        dev->gc_listener = NULL;
    }
    {
        std::lock_guard<std::mutex> guard(l->lock);
        l->stop = true;
    }
    l->cv.notify_one();
    l->thread.join();
    delete l;
    return 0;
}

int fdp_unregister_gc_callback(int fd)
{
    FDP_GET_DEV_OR_RETURN(dev, fd);
    return stop_gc_listener(dev);
}

void fdp_stop_gc_listener(fdp_dev_t* dev)
{
    int saved_errno = errno;
    stop_gc_listener(dev);
    errno = saved_errno;
}
