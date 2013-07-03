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

#define NUM_POLICIES 5
#define LOG_TRY_MALLOC 1
#define LOG_GC 2
#define LOG_WAIT_CONC_GC 3
#define LOG_GC_SCHED 4
#define MAX_STRING_LENGTH 384

#define dvmGetThreadCpuTimeMsec() (dvmGetThreadCpuTimeNsec() / 1000000)
using namespace std;

extern int seqNumber;
extern bool logReady;
extern FILE* fileLog;
extern string policyName;
extern int policyNumber;
extern unsigned int minGCTime;
extern unsigned int intervals;
extern FILE* fileLog;

extern size_t lastRequestedSize;
extern string processName;
extern int freeHistory[10]; // histogram
extern int threshold; // threshold for starting concurrent GC
extern u8 lastGCTime; // for scheduling GCs
static int initLogDone;

/*

extern const GcPolSpec *stock;
extern const GcPolSpec *MI2;
extern const GcPolSpec *MI2A;
extern const GcPolSpec *MI4;
extern const GcPolSpec *MI4A;
*/

// get current RTC time
u8 dvmGetRTCTimeNsec(void);

/*
 * Per-thread CPU time, in millis.
 */
INLINE u8 dvmGetRTCTimeMsec(void) {
    return dvmGetRTCTimeNsec() / 1000000;
}

/*
void logPrint(int logEventType, const GcSpec* spec);
void logPrint(int logEventType, bool mallocFail);
void logPrint(int logEventType);*/

void _logPrint(int logEventType, bool mallocFail, const GcSpec* spec);

extern int skipLogging;
inline void logPrint(int logEventType, bool mallocFail, const GcSpec* spec)
{
    _logPrint(logEventType, mallocFail, spec);
}

// A few shortcut adapters
inline void logPrint(int logEventType, const GcSpec* spec)
{
    logPrint(logEventType, false, spec);
}

inline void logPrint(int logEventType, bool mallocFail)
{
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

void logMeInit(void); 

void removeNewLines(char *input);

/* Wrapper for log file initialization.   */

inline void initLogFile() 
{
  if (initLogDone) return;
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

#endif  // ROBUST_LOG_H_
