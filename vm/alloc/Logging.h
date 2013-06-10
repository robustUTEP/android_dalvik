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

#define dvmGetThreadCpuTimeMsec() (dvmGetThreadCpuTimeNsec() / 1000000)
using namespace std;

extern int seqNumber;
extern bool logReady;
extern FILE* fileLog;
extern string policyName;
extern int policyNumber;
extern unsigned int minGCTime;
extern unsigned int intervals;

extern size_t lastRequestedSize;
extern string processName;
extern int freeHistory[10]; // histogram
extern int threshold; // threshold for starting concurrent GC
extern u8 lastGCTime; // for scheduling GCs

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


void logPrint(int logEventType, const GcSpec* spec);
void logPrint(int logEventType, bool mallocFail);
void logPrint(int logEventType);
void logPrint(int logEventType, bool mallocFail, const GcSpec* spec);
void logGC(const GcSpec* spec);
void logMalloc(bool MallocFail);
void logConcGC(void);
void logBasicEvent(const char* beginEnd, const char* eventName, int seqNumber);
void logHeapEvent(const char* beginEnd, const char* eventName, int seqNumber);
void logGCEvent(const char* beginEnd, const char* eventName, int seqNumber, const GcSpec *spec);
void writeLogEvent(int eventType, const char* beginEnd, const char* eventName, int seqNumber, const GcSpec *spec);;

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
static int initLogDone;
inline void initLogFile() 
{
  if (initLogDone) return;
  _initLogFile();
}

#endif  // ROBUST_LOG_H_
