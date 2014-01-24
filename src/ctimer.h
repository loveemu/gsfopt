
#ifndef CTIMER_H_INCLUDED
#define CTIMER_H_INCLUDED

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#ifdef WIN32
#pragma comment(lib, "winmm.lib")
#include <windows.h>
#else
#include <unistd.h>
#include <sys/time.h>
#endif

static void timer_init(void)
{
#ifdef WIN32
	timeBeginPeriod(1);
#endif
}

static void timer_uninit(void)
{
#ifdef WIN32
	timeEndPeriod(1);
#endif
}

static double timer_get(void)
{
#ifdef WIN32
	return (double)(timeGetTime()) / 1000;
#else
	struct timeval t;
	gettimeofday(&t, NULL);
	return (double)t.tv_sec + (double)t.tv_usec * 1e-6;
#endif
}

static void timer_sleep(double tsleep)
{
#ifdef WIN32
	Sleep((DWORD)(tsleep * 1000));
#else
	struct timespec treq, trem;
	treq.tv_sec = (time_t)tsleep;
	treq.tv_nsec = (long)((tsleep - treq.tv_sec) * 1000000000.0);
	memset(&trem, 0, sizeof(trem));
	nanosleep(&treq, &trem);
#endif
}

#endif
