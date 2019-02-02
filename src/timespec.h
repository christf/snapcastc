#pragma once
#include <time.h>

typedef struct timediff {
	struct timespec time;
	int sign;
} timediff;

timediff timeSub(const struct timespec *t1, const struct timespec *t2);
struct timespec timeAddMs(struct timespec *t1, int ms);
struct timespec timeAdd(struct timespec *t1, struct timespec *t2);
int timespec_isnear(struct timespec *t1, struct timespec *t2, int chunkms);
int timespec_cmp(struct timespec a, struct timespec b);
