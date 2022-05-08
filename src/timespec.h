#pragma once
#include <time.h>

typedef struct timediff {
	struct timespec time;
	int sign;
} timediff;

timediff timeSub(const struct timespec *t1, const struct timespec *t2);
struct timespec timeAddMs(const struct timespec *t1, const int ms);
struct timespec timeAdd(const struct timespec *t1, const struct timespec *t2);
int timespec_isnear(const struct timespec *t1, const struct timespec *t2, const int chunkms);
struct timespec timeSubMs(const struct timespec *t1, const int ms);
int timespec_cmp(const struct timespec *t1, const struct timespec *t2);
