#include "timespec.h"
#include <stdlib.h>
#include <time.h>
#include "util.h"

#define BILLION 1000000000UL

struct timespec timeAdd(const struct timespec *t1, const struct timespec *t2) {
	long sec = t2->tv_sec + t1->tv_sec;
	long nsec = t2->tv_nsec + t1->tv_nsec;
	while (nsec >= BILLION) {
		nsec -= BILLION;
		sec++;
	}
	return (struct timespec){.tv_sec = sec, .tv_nsec = nsec};
}

int timespec_isnear(const struct timespec *t1, const struct timespec *t2, const int chunkms) {
	struct timespec tmp, tmp1;
	int status = 0;

	tmp = timeSub(t1, t2).time;
	tmp1 = timeAddMs ( &(struct timespec) { .tv_sec = 0, .tv_nsec = 0 }, chunkms);

	if (timespec_cmp( &tmp1, &tmp) >= 0 ) {
		status = 1;
	}

	log_debug("isnear: %s  t1: %s, t2: %s, interval: %d, difference: %s\n", status ? "YES": "NO",  print_timespec(t1), print_timespec(t2), chunkms, print_timespec(&tmp));
	return status;
}

timediff timeSub(const struct timespec *t1, const struct timespec *t2) {
	timediff ret;
	const struct timespec *it1;
	const struct timespec *it2;

	if (timespec_cmp(t1, t2) < 0) {
		it1 = t2;
		it2 = t1;
		ret.sign = -1;
	}
	else {
		it1 = t1;
		it2 = t2;
		ret.sign = 1;
	}

      	// now it1 is always greater than it2

	ret.time.tv_sec = it1->tv_sec - it2->tv_sec;

	if (it1->tv_nsec >= it2->tv_nsec) {
		ret.time.tv_nsec = it1->tv_nsec - it2->tv_nsec;
	} else {
		ret.time.tv_sec--;
		ret.time.tv_nsec = BILLION - it2->tv_nsec + it1->tv_nsec;
	}

	return ret;
}

struct timespec timeSubMs(const struct timespec *t1, const int ms) {
	struct timespec t2;
	t2.tv_sec = ms / 1000;
	t2.tv_nsec = (ms % 1000) * 1000000l;
	return timeSub(t1, &t2).time;
}

struct timespec timeAddMs(const struct timespec *t1, const int ms) {
	struct timespec t2;
	t2.tv_sec = ms / 1000;
	t2.tv_nsec = (ms % 1000) * 1000000l;
	return timeAdd(t1, &t2);
}

int timespec_cmp(const struct timespec *a, const struct timespec *b) {
	if (a->tv_sec < b->tv_sec)
		return -1;
	else if (a->tv_sec > b->tv_sec)
		return +1;
	else if (a->tv_nsec < b->tv_nsec)
		return -1;
	else if (a->tv_nsec > b->tv_nsec)
		return +1;

	return 0;
}

