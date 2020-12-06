#define _GNU_SOURCE 1

#include "syscallwrappers.h"
#include "error.h"

#include <stdio.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <unistd.h>
#include <linux/random.h>
#include <fcntl.h>

void obtainsystime(struct timespec *ts) {
	struct timeval currenttime;
	gettimeofday(&currenttime, NULL);
	ts->tv_sec = currenttime.tv_sec;
	ts->tv_nsec = currenttime.tv_usec * 1000;
}

int obtainrandom(void *buf, size_t buflen, unsigned int flags) {
  int rc = 0;
#if !defined(SYS_getrandom)
  int urandom = open("/dev/urandom", O_RDONLY);
  if (urandom < 0) {
    exit_error("Failed to open /dev/urandom.");
  }
#endif
  while (rc != buflen) {
#if defined(SYS_getrandom)
    rc = (int)syscall(SYS_getrandom, buf, buflen, flags);
    if (rc == -1) {
      if (errno != ENOSYS) {
	exit_error("syscall SYS_getrandom.");
      }
      perror("syscall SYS_getrandom failed. retrying");
#else
    rc = read(urandom, buf, buflen);
    if (rc < 0) {
      perror("reading /dev/urandom failed. retrying");
#endif
    }
  }
  return rc;
}
