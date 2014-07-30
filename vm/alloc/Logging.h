// David: added for logging not sure
// if declared somewhere else
// since code is still written C style

#ifndef ROBUST_LOG_H_
#define ROBUST_LOG_H_

#include <iostream>
#include <fstream>
#include <string>
#include <time.h>
#include <sys/time.h>
// experimental
#include "alloc/PerfCounts.h"


#define NUM_POLICIES 17
#define LOG_CUSTOM 0
#define LOG_TRY_MALLOC 1
#define LOG_GC 2
#define LOG_WAIT_CONC_GC 3
#define LOG_GC_SCHED 4
#define LOG_IGNORE_EXPLICIT 5
#define MAX_STRING_LENGTH 768

#define DID_NOTHING 0
#define SCHED_GC 1
#define PRED_LONG 2
#define TIMEOUT 3
#define SUCCESS 4
#define RELEASE_SPLEEN 5
#define GROW_AND_GO 6

#define dvmGetThreadCpuTimeMsec() (dvmGetThreadCpuTimeNsec() / 1000000)
using namespace std;

extern int seqNumber;
extern int lastGCSeqNumber;
extern bool logReady;
extern FILE* fileLog;
extern string policyName;
extern int policyNumber;
extern unsigned int minGCTime;
extern unsigned int intervals;
extern unsigned int adaptive;
extern unsigned int resizeThreshold;
extern unsigned short timeToWait;
extern unsigned short numIterations;
extern unsigned short currIterations;
extern float partialAlpha;
extern float fullAlpha;
extern float beta;
extern float avgPercFreedPartial;
extern float avgPercFreedFull;
extern float partialAlpha;
extern float fullAlpha;
extern float beta;
extern float avgPercFreedPartial;
extern float avgPercFreedFull;
extern FILE* fileLog;
extern bool schedGC;
extern bool resizeOnGC;
extern size_t thresholdOnGC;
extern size_t maxAdd;
extern size_t minAdd;
extern bool firstExhaustSinceGC;
extern bool dumpHeap;
extern bool inZygote;

extern size_t lastRequestedSize;
extern string processName;
extern int freeHistory[10]; // histogram
extern size_t threshold; // threshold for starting concurrent GC
extern size_t freeExhaust; // free 500 ms before exhaust
extern u8 lastGCTime; // for scheduling GCs
extern u8 lastGCCPUTime; // ditto just cpu time instead
extern u8 gcStartTime; // start time of the last GC
extern u8 lastExhaustion; // last memory exhaustion time
extern struct timespec minSleepTime;
static int initLogDone;
static int preinit = 0; // if we're an initialization process (ie zygote or sys server)
extern size_t numBytesFreedLog;
extern size_t objsFreed;
extern size_t lastAllocSize;
extern int lastState;
extern void* spleen;
extern size_t spleenSize;
extern size_t currSpleenSize;
extern size_t totalAlloced;
extern size_t hares[4];

// get current RTC time
u8 dvmGetRTCTimeNsec(void);

/*
* Per-thread CPU time, in millis.
*/
INLINE u8 dvmGetRTCTimeMsec(void) {
    return dvmGetRTCTimeNsec() / 1000000;
}

void _logPrint(int logEventType, bool mallocFail, const GcSpec* spec);

extern int skipLogging;
inline void logPrint(int logEventType, bool mallocFail, const GcSpec* spec)
{
    _logPrint(logEventType, mallocFail, spec);
}

void logPrint(int logEventType, const char *type, const char *customString);

// A few shortcut adapters
inline void logPrint(int logEventType, const GcSpec* spec)
{
    logPrint(logEventType, false, spec);
}

inline void logPrint(int logEventType, const GcSpec* spec, size_t numBytesFreed, size_t numObjectsFreed)
{
    numBytesFreedLog = numBytesFreed;
    objsFreed = numObjectsFreed;
    logPrint(logEventType, false, spec);
    numBytesFreedLog = 0;
    objsFreed = 0;
}

inline void logPrint(int logEventType, bool mallocFail, size_t allocSize, int state)
{
	lastAllocSize = allocSize;
	lastState = state;
    logPrint(logEventType, mallocFail, NULL);
}

inline void logPrint(int logEventType)
{
    logPrint(logEventType, false, NULL);
}

void logGC(const GcSpec* spec);
void logMalloc(bool MallocFail);
void logConcGC(void);
void logGCSched(void);
void logIgnoreExplicit(void);
void logBasicEvent(const char* beginEnd, const char* eventName, int seqNumber);
void logHeapEvent(const char* beginEnd, const char* eventName, int seqNumber);
void logGCEvent(const char* beginEnd, const char* eventName, int seqNumber, const GcSpec *spec);

void writeLogEvent(int eventType, const char* beginEnd, const char* eventName, int seqNumber, const GcSpec *spec, bool mallocFail);

inline void writeLogEvent(int eventType, const char* beginEnd, const char* eventName, int seqNumber, const GcSpec *spec)
{
    writeLogEvent(eventType, beginEnd, eventName, seqNumber, spec, false);
}

/*
* Check and see if GC needs to be intiated
*/
void scheduleConcurrentGC(void);

/*
* Log file initialization
*/
void _initLogFile(void);

/*
* Save memory history
*/
void saveHistory();

/*
* Sets the free space threshold
*/
void setThreshold(void);

/*
 * store the current gc completion time
 */
void storeGCTime(u8 time);

/*
 * Computes the average of the last x GCs
 */
u8 getGCTimeAverage(int numIterations);

/*
 * Compute average of last 5 GCs by default
 */
inline u8 getGCTimeAverage(void)
{
	return getGCTimeAverage(5);
}

/*
 * Adjusts threshold for xxx policy
 */
void adjustThreshold(void);

void logMeInit(void);

void removeNewLines(char *input);

/* Wrapper for log file initialization. */

inline void initLogFile()
{
  if (initLogDone && !preinit) return;
  _initLogFile();
}

/**
* used to open and read cpu speed
* as of now it's being tested on maguro
* For the moment the file is opened
* and closed as needed since we don't
* know what kind of access issues we may have
*/

void logCPUSpeed(char* speed);

/**
* Checks to see if we run continous GC
* by checking to see if processName.gc exists
*/
 
int continousGC(const GcSpec* spec);

/**
 * Get device name from the build props
 */
void getDeviceName(void);

/*
 * Get the current count if available
 */
unsigned long getCount(int cpu);

/*
 * recomputes full/partial iteration ratio
 */
void computePartialFull(void);

/* 
 * dumps heap and saves to disk
 */
void saveHeap(void);

/*
 * memory dump handler
 */
void memDumpHandler(void* start, void* end, size_t used_bytes,
                                void* arg);

/* 
 * logger that gets called every second for events we want
 * to occur on a timed schedule
 */
void timed(void);

/*
 * save pointer address, and size
 */
void savePtr(void *ptr, size_t size);

/*
 * read and write current GC threshold
 */
void writeThreshold(void);
void readThreshold(void);


/*
 * Get the per-process CPU time, in nanoseconds
 *
 * The amount of time the process had been executing
 */
u8 dvmGetTotalProcessCpuTimeNsec(void);

/*
 * Get the per-thread CPU time, in nanoseconds
 *
 * The amount of time the thread had been executing
 */
u8 dvmGetTotalThreadCpuTimeNsec(void);

/*
* Per-Process CPU time, in micros
*/
INLINE u8 dvmGetTotalProcessCpuTimeMsec(void) {
    return dvmGetTotalProcessCpuTimeNsec() / 1000000;
}

/*
* Per-Thread CPU time, in micros
*/
INLINE u8 dvmGetTotalThreadCpuTimeMsec(void) {
    return dvmGetTotalThreadCpuTimeNsec() / 1000000;
}

/*
 * Gets current cpu stats as string
 */
void getCPUStats(char *output);

#endif // ROBUST_LOG_H_

