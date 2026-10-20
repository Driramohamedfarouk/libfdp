#pragma once
// This is the public interface of libfdp. NVMe/libnvme-derived protocol
// types used internally (nvme_passthru_cmd, the FDP RUH status structs,
// etc.) are not part of this interface and live in src/nvme_types.h instead.
#include <unistd.h>
#include <cstdint>

// Opaque handle: the definition of fdp_dev is private to the implementation
// (src/nvme_types.h). Callers only ever interact with a device through the
// fd returned by fdp_open().
typedef struct fdp_dev fdp_dev_t;

typedef uint16_t plid_t;

struct io_uring_sqe;  // Forward declaration

// This is the interface of the library

/*********************************************************************************
All interfaces to the library that take the returned file descriptor by
fdp_open() have the following contract on return value. On succes, 0 is
returned. On failure, -1 is returned and errno is set. The following errno are
common to all API method. EBADF  Bad file descriptor All functions that take a
placement ID may return EINVAL if the device is not set up with this placement
ID. All libfdp API try to mirror as much as possible the contract of their
conterparts from standard libraries so as to be a drop in replacement.
*********************************************************************************/

/////////////////////////////////////////////////////////////////////////////////
// Device APIs, implementation lives under src/fdp_device.cpp
/////////////////////////////////////////////////////////////////////////////////

/**
Takes a block device as input will do the same semantic of the open()
syscall and also will open the generic device. On success it will return
the file descriptor of the block device and on failure it WILL returns
the corresponding error code. All interaction with the library will
happen through this handle.
*/
int fdp_open(const char* bdev_name, int flags, ... /* mode_t mode */);

void fdp_close(int fd);

/////////////////////////////////////////////////////////////////////////////////
// IO APIs, implementation lives under src/fdp_io.cpp
/////////////////////////////////////////////////////////////////////////////////

// synchronous pwrite with plid (placement id)
ssize_t fdp_pwrite(int fd, void* buf, size_t count, off_t offset, uint16_t plid);

/**
 Prepares an FDP-directed write into a caller-supplied, caller-owned
 io_uring SQE (fdp_pwrite() uses this internally, against its own ring, for
 the synchronous case). Like the raw liburing io_uring_prep_*() functions,
 this never fails synchronously: if fd is not an open fdp device, the SQE
 is still prepared (targeting fd as given) and submission is left to fail
 normally, reporting the error (-EBADF, or -ENOTTY/-EINVAL if fd is valid
 but not an FDP-capable NVMe char device) through cqe->res at completion,
 exactly like any other bad-fd io_uring request.

 The ring `sqe` comes from MUST have been created with
 IORING_SETUP_SQE128 | IORING_SETUP_CQE32 -- the NVMe passthrough command
 this writes into `sqe->cmd` does not fit in a standard-size SQE, and
 there is no way to detect a mismatched ring from a bare io_uring_sqe*
 here: getting this wrong silently corrupts adjacent ring memory instead
 of failing.
*/
void fdp_io_uring_prep_write(struct io_uring_sqe* sqe, int fd, const void* buf, unsigned count, uint64_t offset, uint16_t plid);

/**
 Mirror of liburing's io_uring_prep_write_fixed(): like
 fdp_io_uring_prep_write(), but [buf, buf + count) must lie inside the
 buffer registered at index buf_index (io_uring_register_buffers()).
 Requires fixed buffers on NVMe passthrough.
*/
void fdp_io_uring_prep_write_fixed(struct io_uring_sqe* sqe, int fd, const void* buf, unsigned count, uint64_t offset, int buf_index, uint16_t plid);

// Sets the placement id of an already filled sqe
void fdp_sqe_set_plid(struct io_uring_sqe* sqe, uint16_t plid);

/**
 Registered files. The SQEs libfdp prepares do not target the block device
 fd returned by fdp_open() but the NVMe generic char device behind it.
 fdp_get_uring_fd() returns that fd: it is what must be passed to
 io_uring_register_files*(). It stays owned by libfdp and is valid until
 fdp_close(fd); unregister it from your rings before calling fdp_close().
 On failure, -1 with errno set (EBADF if fd is not an open fdp device).
*/
int fdp_get_uring_fd(int fd);

/**
 Turns an already prepared sqe into a fixed-file request on the file
 registered at file_index (sets sqe->fd and IOSQE_FIXED_FILE). Like
 fdp_sqe_set_plid(), call it AFTER fdp_io_uring_prep_write*(), which
 resets the whole sqe.
*/
void fdp_sqe_set_fixed_file(struct io_uring_sqe* sqe, unsigned file_index);

/**
 Makes an already prepared sqe use the registered buffer at buf_index.
 fdp_io_uring_prep_write_fixed() is prep + this; same AFTER-prep rule as
 fdp_sqe_set_fixed_file().
*/
void fdp_sqe_set_fixed_buf(struct io_uring_sqe* sqe, unsigned buf_index);

/////////////////////////////////////////////////////////////////////////////////
// Event management APIs, implementation lives under src/fdp_events.cpp
//
// EXPERIMENTAL: this interface is not tested and may change or be removed
// without notice. Do not rely on it.
// Both interface methods here are Ad-hoc and unreliable.
/////////////////////////////////////////////////////////////////////////////////

// temporarly expose this as an interface for debugging.
int fdp_get_events(int fd, uint8_t* log, uint32_t log_size);

// This is part of the interface, the callback now does not take any parameter
// mainly because no driver has any meaningful results for the media relocation
// event. We may change this later.
// void fdp_register_gc_callback(int fd, void (*gc_callback)(void));
int open_ru_timer(void* arg);

/////////////////////////////////////////////////////////////////////////////////
// RUH management APIs, implementation lives under src/fdp_ruh.cpp
/////////////////////////////////////////////////////////////////////////////////

/**
 Returns the nominal size, of a Reclaim Unit on this device
 -- what fdp_get_remaining_bytes_in_ru() reports once a Reclaim Unit is fully fresh.
 On failure, -1 with errno set (EBADF if fd is not an open fdp device).
*/
ssize_t fdp_get_ru_size(int fd);

/**
 Returns the number of ruh available to write to in an opened FDP device on success.
 On failure, -1 with errno set (EBADF if fd is not an open fdp device).
*/
int fdp_get_nruh(int fd);

/**
 Given a placement id, will reset the write pointer of the corresponding RUH
 on a new free Reclaim Unit.
*/
int fdp_reset_free_ru(int fd, plid_t plid);

/**
 On success it return the number of remaining media writes in the open
 Reclaim Unit of the Reclaim Unit Handle corresponding to the plid provided.
*/
ssize_t fdp_get_remaining_bytes_in_ru(int fd, plid_t plid);
