# libfdp

A small Linux library for using **NVMe Flexible Data Placement (FDP)**: open an
FDP-capable NVMe namespace, write data to a chosen placement identifier, and
inspect reclaim unit state and media-relocation events.

## Requirements

- Linux with `io_uring` and NVMe passthrough support
- [`liburing`](https://github.com/axboe/liburing) (`liburing-dev` on Debian/Ubuntu)
- A C++11 compiler
- CMake 3.16 or newer
- An NVMe device with FDP enabled, and permission to open it (usually root)
## Build and install

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build
```

To install somewhere else, pass a prefix:

```sh
cmake --install build --prefix ~/.local
```

## Use it in your CMake project

If libfdp is installed:

```cmake
find_package(fdp 0.1 REQUIRED)
target_link_libraries(myapp PRIVATE fdp::fdp)
```

If you installed to a non-system prefix, point CMake at it with
`-DCMAKE_PREFIX_PATH=$HOME/.local`.

If you would rather not install anything, fetch it at configure time:

```cmake
include(FetchContent)
FetchContent_Declare(fdp
    GIT_REPOSITORY https://github.com/Driramohamedfarouk/libfdp.git
    GIT_TAG        v0.1.0)
FetchContent_MakeAvailable(fdp)

target_link_libraries(myapp PRIVATE fdp::fdp)
```

Either way you get the same target, and it brings its own include directory,
`liburing` and pthreads with it. You do not need to add `-luring` yourself.

## Minimal example

`main.cpp` — write 16 KiB to placement identifier 1 and report how much room is
left in that reclaim unit:

```cpp
#include <fdp.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>

int main() {
    int fd = fdp_open("/dev/nvme0n1", O_RDWR);
    if (fd < 0) {
        perror("fdp_open");
        return 1;
    }

    void *buf = nullptr;
    if (posix_memalign(&buf, 4096, 16384) != 0) {
        fdp_close(fd);
        return 1;
    }

    const plid_t plid = 1;
    ssize_t written = fdp_pwrite(fd, buf, 16384, 0, plid);
    printf("wrote %zd bytes to placement id %u\n", written, plid);

    ssize_t left = fdp_get_remaining_bytes_in_ru(fd, plid);
    printf("%zd bytes left in the open reclaim unit\n", left);

    free(buf);
    fdp_close(fd);
    return 0;
}
```

`CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.16)
project(myapp LANGUAGES CXX)

find_package(fdp 0.1 REQUIRED)

add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE fdp::fdp)
```

Build and run:

```sh
cmake -B build
cmake --build build
sudo ./build/myapp
```

Change `/dev/nvme0n1` to your own namespace. `fdp_pwrite` takes the same
arguments as `pwrite`, plus the placement identifier that tells the drive which
reclaim unit the data belongs to.
