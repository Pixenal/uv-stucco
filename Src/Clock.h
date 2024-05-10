#pragma once

#ifdef PLATFORM_LINUX
	#include <sys/time.h>
#endif
#ifdef PLATFORM_WINDOWS
	#include <time.h>
#endif
#ifdef PLATFORM_LINUX
	#define CLOCK_INIT struct timeval start, stop;
	#define CLOCK_TIME_DIFF(start, stop) (stop.tv_sec - start.tv_sec) * 1000000 + (stop.tv_usec - start.tv_usec)
	#define CLOCK_START gettimeofday(&start, NULL)
	#define CLOCK_STOP(a) gettimeofday(&stop, NULL); printf("%s - %s: %lu\n", __func__, (a), CLOCK_TIME_DIFF(start, stop))
	#define CLOCK_STOP_NO_PRINT gettimeofday(&stop, NULL)
#endif
#ifdef PLATFORM_WINDOWS
	#define CLOCK_INIT struct timespec start = {0}; struct timespec stop = {0};
	#define CLOCK_TIME_DIFF(start, stop) (stop.tv_sec - start.tv_sec) * 1000000 + (stop.tv_nsec - start.tv_nsec)
	#define CLOCK_TIME_GET(a) if(timespec_get(&a, TIME_UTC) != TIME_UTC) printf("CLOCK_START failed\n")
	#define CLOCK_START CLOCK_TIME_GET(start)
	#define CLOCK_STOP(a) CLOCK_TIME_GET(stop); printf("%s - %s: %llu\n", __func__, (a), CLOCK_TIME_DIFF(start, stop))
	#define CLOCK_STOP_NO_PRINT CLOCK_TIME_GET(stop)
#endif
