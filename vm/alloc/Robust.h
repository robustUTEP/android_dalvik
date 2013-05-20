// David: added for logging not sure
// if declared somewhere else 
// since code is still written C style
#include <iostream>
#include <fstream>
#include <string>
#include <time.h>
#include <sys/time.h>
#include "sys/stat.h"
#define DEFAULT 0
#define MI 1
#define POLICY DEFAULT
#define MIN_GC_TIME 2000

#define dvmGetThreadCpuTimeMsec() (dvmGetThreadCpuTimeNsec() / 1000000)
using namespace std;

struct timespec start;
int seqNumber;
bool logReady;
//string fileName;
FILE* fileLog;
string name;
unsigned int minTime;
unsigned int intervals;

size_t lastRequestedSize = 0;
//char processName[128];
string processName;
static int numAllocs = 0; // number of allocations
static size_t amountAlloc = 0; // total size allocd
int* freeHistory; // histogram
int threshold = (128 << 10); // default threshold of 128k
u8 lastGCTime = 0; // for scheduling GCs

// GC Policies
struct GcPolSpec {
	/* Name of the policy */
	const char *name;
	/* Minimum GC time in ms*/
	unsigned int minTime;
	/* number of 100ms increments keep size log */
	unsigned int intervals;
};

static const GcPolSpec stock = {
	"default",
	0,
	0
};

static const GcPolSpec GcPolMI2 = {
	"MI2",
	2000,
	0
};

const GcPolSpec *MI2 = &GcPolMI2;

static const GcPolSpec GcPolMI4 = {
	"MI4",
	4000,
	0
};

const GcPolSpec *MI4 = &GcPolMI4;

static const GcPolSpec GcPolMI2A = {
	"MI2A",
	2000,
	5
};

const GcPolSpec *MI2A = &GcPolMI2A;

static const GcPolSpec GcPolMI4A = {
	"MI4A",
	4000,
	5
};

const GcPolSpec *MI4A = &GcPolMI4A;

const GcPolSpec policies[] = {stock, GcPolMI2, GcPolMI4, GcPolMI2A, GcPolMI4A};
GcPolSpec policy;

// get time from RTC
// could shift slightly but shouldn't affect us much
u8 dvmGetRTCTimeNsec(void)
{
#ifdef HAVE_POSIX_CLOCKS
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    return (u8)now.tv_sec*1000000000LL + now.tv_nsec;
#else
    struct timeval now;
    gettimeofday(&now, NULL);
    return (u8)now.tv_sec*1000000000LL + now.tv_usec * 1000LL;
#endif
}

/*
 * Per-thread CPU time, in millis.
 */
INLINE u8 dvmGetRTCTimeMsec(void) {
    return dvmGetRTCTimeNsec() / 1000000;
}
