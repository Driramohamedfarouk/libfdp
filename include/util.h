#include <assert.h>
#include <stdint.h>

#define XLOGF(level, fmt, ...) printf("[" level "] " fmt "\n", ##__VA_ARGS__)

void panic(const char *msg) {
	XLOGF("ERR", "%s", msg);
	exit(1);
}


