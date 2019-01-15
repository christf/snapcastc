#include "syscallwrappers.h"
#include "error.h"

#include <stdio.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <unistd.h>

void obtainsystime(struct timespec *ts) {
	struct timeval currenttime;
	gettimeofday(&currenttime, NULL);
	ts->tv_sec = currenttime.tv_sec;
	ts->tv_nsec = currenttime.tv_usec * 1000;
}

int obtainrandom(void *buf, size_t buflen, unsigned int flags) {
	int rc = 0;
	while (rc != buflen) {
		rc = (int)syscall(SYS_getrandom, buf, buflen, flags);
		if (rc == -1) {
			if (errno != ENOSYS) {
				exit_error("syscall SYS_getrandom.");
			}
			perror("syscall SYS_getrandom failed. retrying");
		}
	}
	return rc;
}
