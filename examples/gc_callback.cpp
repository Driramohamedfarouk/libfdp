// Counts device GC events (FDP Media Reallocated events) with a capturing
// lambda registered through fdp_register_gc_callback(), then unregisters it
// with fdp_unregister_gc_callback().
//
// usage: gc_callback <bdev> [seconds]
#include <errno.h>
#include <fcntl.h>
#include <fdp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <atomic>

int main(int argc, char* argv[])
{
    const char* bdev = argc > 1 ? argv[1] : "/dev/nvme1n1";
    int seconds = argc > 2 ? atoi(argv[2]) : 60;

    int fd = fdp_open(bdev, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "fdp_open(%s): %s\n", bdev, strerror(errno));
        return 1;
    }

    // The callback runs on libfdp's listener thread, hence the atomic.
    std::atomic<uint64_t> nb_gc_events(0);
    if (fdp_register_gc_callback(fd, [&nb_gc_events] { nb_gc_events++; }) != 0) {
        fprintf(stderr, "fdp_register_gc_callback: %s\n", strerror(errno));
        fdp_close(fd);
        return 1;
    }

    for (int s = 1; s <= seconds; s++) {
        sleep(1);
        printf("[%3ds] GC events: %lu\n", s, (unsigned long)nb_gc_events.load());
    }

    // Once this returns the lambda will not run again; fdp_close() would
    // also do it.
    if (fdp_unregister_gc_callback(fd) != 0)
        fprintf(stderr, "fdp_unregister_gc_callback: %s\n", strerror(errno));
    printf("Total GC events: %lu\n", (unsigned long)nb_gc_events.load());

    fdp_close(fd);
    return 0;
}
