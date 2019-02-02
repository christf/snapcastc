#pragma once

#include <sys/types.h>

int obtainrandom(void *buf, size_t buflen, unsigned int flags);
void obtainsystime(struct timespec *ts);
