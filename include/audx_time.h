#ifndef AUDX_TIME_H
#define AUDX_TIME_H

#include <stdint.h>

uint64_t audx_now_ns(void);

#ifdef AUDX_TIME_IMPL
#include <time.h>

uint64_t audx_now_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}
#endif // AUDX_TIME_IMPL

#endif // AUDX_TIME_H
