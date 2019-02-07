#include "timespec.h"
#include <stdlib.h>
#include <time.h>
#include "util.h"

#define BILLION 1000000000UL

struct timespec timeAdd(struct timespec *t1, struct timespec *t2) {
	long sec = t2->tv_sec + t1->tv_sec;
	long nsec = t2->tv_nsec + t1->tv_nsec;
	while (nsec >= BILLION) {
		nsec -= BILLION;
		sec++;
	}
	return (struct timespec){.tv_sec = sec, .tv_nsec = nsec};
}

int timespec_isnear(struct timespec *t1, struct timespec *t2, int chunkms) {
	struct timespec tmp;

	tmp = *t1;
	log_debug("isnear:  t1: %s, tmp: %s\n", print_timespec(t1), print_timespec(&tmp));
	tmp = timeAddMs(t1, chunkms);

	if ((timespec_cmp(*t2, tmp) <= 0) && (timespec_cmp(*t2, *t1) >= 0)) {
		log_debug("isnear: YES+  t1: %s, t2: %s\n", print_timespec(t1), print_timespec(t2));
		return 1;
	}

	if (timespec_cmp(*t2, *t1) <= 0) {
		log_debug("isnear: YES-  t1: %s, t2: %s\n", print_timespec(t1), print_timespec(t2));
		return 1;
	}

	log_debug("isnear: NO  t1: %s, t2: %s\n", print_timespec(t1), print_timespec(t2));
	return 0;
}

timediff timeSub(const struct timespec *t1, const struct timespec *t2) {
	timediff ret;
	struct timespec *it1 = (struct timespec*)t1;
	struct timespec *it2 = (struct timespec*)t2;

	ret.sign = 1;

	if (timespec_cmp(*t1, *t2) < 0) {
		it1 = (struct timespec*)t2;
		it2 = (struct timespec*)t1;
		ret.sign = -1;
	} // now t1 is always greater than t2

	uint64_t tdiff_sec = it1->tv_sec - it2->tv_sec;
	uint64_t tdiff_nsec = 0;
	if (it1->tv_nsec >= it2->tv_nsec) {
		tdiff_nsec = it1->tv_nsec - it2->tv_nsec;
	} else {
		tdiff_sec--;
		tdiff_nsec = 1000000000UL - it2->tv_nsec + it1->tv_nsec;
	}

	ret.time.tv_sec = tdiff_sec;
	ret.time.tv_nsec = tdiff_nsec;

	log_debug("sub:  t1: %s, t2: %s ret: %s sign: %d\n", print_timespec(t1), print_timespec(t2), print_timespec(&ret.time), ret.sign);

	return ret;
}

struct timespec timeAddMs(struct timespec *t1, int ms) {
	struct timespec t2;
	t2.tv_sec = ms / 1000;
	t2.tv_nsec = (ms % 1000) * 1000000l;
	return timeAdd(t1, &t2);
}

int timespec_cmp(struct timespec a, struct timespec b) {
	if (a.tv_sec < b.tv_sec)
		return -1;
	else if (a.tv_sec > b.tv_sec)
		return +1;
	else if (a.tv_nsec < b.tv_nsec)
		return -1;
	else if (a.tv_nsec > b.tv_nsec)
		return +1;

	return 0;
}
