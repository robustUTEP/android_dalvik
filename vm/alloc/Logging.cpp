#include <iostream>
#include <fstream>
#include <string>
#include <time.h>
#include <sys/time.h>
#include <errno.h>

#include "Dalvik.h"
#include "cutils/process_name.h"
#include "alloc/HeapBitmap.h"
#include "alloc/Verify.h"
#include "alloc/Heap.h"
#include "alloc/HeapInternal.h"
#include "alloc/DdmHeap.h"
#include "alloc/HeapSource.h"
#include "os/os.h"
#include "sys/stat.h"
#include "alloc/Logging.h"

int tryMallocSequenceNumber = 0;
u8 currentMallocTime = 0;
int currInterval = 0;
bool threshSet;
struct timespec startTime;

// GC Policies
//typedef struct GcPolSpec GcPolSpec
struct GcPolSpec {
	/* Name of the policy */
	const char *name;
    /* Policy Number */
    int policyNumber;
	/* Minimum GC time in ms*/
	unsigned int minTime;
	/* number of 100ms increments keep size log */
	unsigned int intervals;
};

static GcPolSpec stockPol = {
  "STW Only",
  1,
  0,
  0
};
GcPolSpec *stock = &stockPol;

static GcPolSpec GcPolMI2 = {
  "MI2S",
  2,
  2000,
  0
};

GcPolSpec *MI2 = &GcPolMI2;

static GcPolSpec GcPolMI2A = {
  "MI2A",
  3,
  2000,
  5
};

GcPolSpec *MI2A = &GcPolMI2A;

static GcPolSpec GcPolMI4 = {
  "MI4",
  4,
  4000,
  0
};

GcPolSpec *MI4 = &GcPolMI4;

static GcPolSpec GcPolMI4A = {
  "MI4A",
  5,
  4000,
  5
};

GcPolSpec *MI4A = &GcPolMI4A;

const GcPolSpec policies[5] = {stockPol, GcPolMI2, GcPolMI2A, GcPolMI4, GcPolMI4A};
GcPolSpec policy;

void logPrint(int logEventType, const GcSpec* spec)
{
    logPrint(logEventType, false, spec);
}

void logPrint(int logEventType, bool mallocFail)
{
    logPrint(logEventType, mallocFail, NULL);
}

void logPrint(int logEventType)
{
    logPrint(logEventType, false, NULL);
}

void logPrint(int logEventType, bool mallocFail, const GcSpec* spec)
{

    initLogFile();
    if(!logReady) {
        return;
    }

    if (fileLog == NULL) {
        ALOGD("GC Logging file closed after succesful open, assertion would have failed");
        return;
    }

    switch (logEventType)
    {
        case LOG_TRY_MALLOC:
                logMalloc(mallocFail);
                break;
        case LOG_GC:
                logGC(spec);
                break; 
        case LOG_WAIT_CONC_GC:
                logConcGC();
                break;
    }
}

void logGC(const GcSpec* spec)
{
    static bool logStart = true;
    static int thisGCSeqNumb;
    string beginOrEnd;
    

    if (logStart) {
        beginOrEnd = "begin";
        thisGCSeqNumb = seqNumber++;
        logStart = false;
    }
    else {
        beginOrEnd = "end";
        logStart = true;
    }

    writeLogEvent(LOG_GC, beginOrEnd.c_str(), "GC", thisGCSeqNumb, spec);
    
}

void logMalloc(bool mallocFail)
{
    static bool logStart = true;
    static int thisMallocSeqNumb;
    string beginOrEnd;

    static u8 lastMallocTime = 0;
    u8 currentMallocTime = dvmGetTotalProcessCpuTimeMsec();

    // to keep the compiler quiet
    logStart = logStart;

    if (logStart) {
        // if last malloc was more than 100 ms ago
        // or if last malloc was less than 100 ms ago and malloc failed
        // log this malloc 
        if (((currentMallocTime - lastMallocTime) > 100) || mallocFail) {
            beginOrEnd = "begin";
            thisMallocSeqNumb = seqNumber++;
            logStart = false;
            writeLogEvent(LOG_TRY_MALLOC, beginOrEnd.c_str(), "TryMalloc", thisMallocSeqNumb, NULL);
            lastMallocTime = currentMallocTime;
        }
    }
    else {
        beginOrEnd = "end";
        logStart = true;
        writeLogEvent(LOG_TRY_MALLOC, beginOrEnd.c_str(), "TryMalloc", thisMallocSeqNumb, NULL);
        // schedule GC if needed
        scheduleConcurrentGC();
    }
    
}

void logConcGC()
{
    static bool logStart = true;
    static int thisGcSeqNumb;
    string beginOrEnd;
    

    if (logStart) {
        beginOrEnd = "begin";
        thisGcSeqNumb = seqNumber++;
        logStart = false;
    }
    else {
        beginOrEnd = "end";
        logStart = true;
    }

    writeLogEvent(LOG_WAIT_CONC_GC, beginOrEnd.c_str(), "TryMalloc", thisGcSeqNumb, NULL);
    
}

/*
 * builds a string of the basic log info
 */
char* logBasicEvent(const char* beginEnd,const char* eventName, int seqNumber, char output[])
{
    u8 wcTime = dvmGetRTCTimeMsec();
    u8 appTime = dvmGetTotalProcessCpuTimeMsec();

    sprintf(output, "@%s%s{\"seqNum\":%d,\"wcTime-ms\":%llu,\"appTime-ms\":%llu",
        beginEnd, eventName, seqNumber, wcTime, appTime);
    return output;
}

/*
 * builds a string of log info with heap stats
 */
void logHeapEvent(const char* beginEnd,const char* eventName, int seqNumber, char output[])
{
    char partial[90];
    size_t heapsAlloc[2], heapsFootprint[2], heapsMax[2];
	heapsAlloc[1] = heapsFootprint[1] = heapsAlloc[0] = heapsFootprint[0] = heapsMax[0] = heapsMax[1] = 0;

    dvmHeapSourceGetValue(HS_BYTES_ALLOCATED, heapsAlloc, 2);
    dvmHeapSourceGetValue(HS_FOOTPRINT, heapsFootprint, 2);
    dvmHeapSourceGetValue(HS_ALLOWED_FOOTPRINT, heapsMax, 2);

    heapsAlloc[0] = heapsAlloc[0] / 1024;
    heapsFootprint[0] = heapsFootprint[0] / 1024;
    heapsMax[0] = heapsMax[0] / 1024;
    heapsAlloc[1] = heapsAlloc[1] / 1024;
    heapsFootprint[1] = heapsFootprint[1] / 1024;
    heapsMax[1] = heapsMax[1] / 1024;

    logBasicEvent(beginEnd, eventName, seqNumber, partial);

    sprintf(output, "%s,\"currAlloc0-kB\":%d,\"currFootprint0-kB\":%d,\"currMax0-kB\":%d,\"currAlloc1-kB\":%d,\"currFootprint1-kB\":%d,\"currMax1-kB\":%d",
        partial, heapsAlloc[0], heapsFootprint[0], heapsMax[0], heapsAlloc[1], heapsFootprint[1], heapsMax[1]); 
}

/*
 * builds a string of heap stats with a GC type
 */
void logGCEvent(const char* beginEnd,const char* eventName, int seqNumber, const GcSpec *spec, char output[])
{
    char partial[200];
    logHeapEvent(beginEnd, eventName, seqNumber, partial);

    sprintf(output, "%s,\"GCType\":\"%s\"", partial, spec->reason);
}

/*
 * writes a log event to a file
 */
void writeLogEvent(int eventType,const char* beginEnd, const char* eventName, int seqNumber, const GcSpec *spec)
{
    char partialEntry[225];
    static int numLogEvents = 0;

    // to keep compiler quiet
    numLogEvents = numLogEvents;

    // flush the log after 20 events
    if (numLogEvents == 20) {
        fflush(fileLog);
        numLogEvents = 0;
	}

    switch (eventType)
    {
        case LOG_TRY_MALLOC:
                logHeapEvent(beginEnd, eventName, seqNumber, partialEntry);
                break;
        case LOG_GC:
                logGCEvent(beginEnd, eventName, seqNumber, spec, partialEntry);
                break; 
        case LOG_WAIT_CONC_GC:
                logBasicEvent(beginEnd, eventName, seqNumber, partialEntry);
                break;
    }   

    fprintf(fileLog, "%s}\n", partialEntry);
    numLogEvents++;    
}

void scheduleConcurrentGC() 
{
  if (policyNumber != 0) {
	u8 timeSinceLastGC = dvmGetTotalProcessCpuTimeMsec() - lastGCTime;

	// check and see if we're at the min time from a concurrent GC
	if ((timeSinceLastGC) > minGCTime)	{
	  // FIXME check heap 0 instead of whole heap set last GC time on GC completion
	  // if we've hit the threshold schedule a concurrent GC
	  size_t heapsAlloc[2], heapsFootprint[2];
	  heapsAlloc[1] = heapsFootprint[1] = heapsAlloc[0] = heapsFootprint[0] = 0;

	  dvmHeapSourceGetValue(HS_BYTES_ALLOCATED, heapsAlloc, 2);
	  dvmHeapSourceGetValue(HS_FOOTPRINT, heapsFootprint, 2);

	  if (threshold >= (heapsFootprint[0] - heapsAlloc[0])) {
		dvmInitConcGC();
	  }
	}
  }
}

/*
 * Saves free memory history
 */

void saveHistory(){

    if (!threshSet) {
        size_t heapsAlloc[2], heapsFootprint[2];
        heapsAlloc[1] = heapsFootprint[1] = heapsAlloc[0] = heapsFootprint[0] = 0;

        dvmHeapSourceGetValue(HS_BYTES_ALLOCATED, heapsAlloc, 2);
        dvmHeapSourceGetValue(HS_FOOTPRINT, heapsFootprint, 2);
		freeHistory[currInterval] = heapsFootprint[0] - heapsAlloc[0];
		currInterval = (currInterval + 1) % intervals;
	} 
}

void setThreshold(void)
{
	if (!threshSet) {
		threshold = freeHistory[(currInterval + (intervals + 1)) % intervals];
		threshSet = true;
	}
}
    
/* 
 * Initializes the robust logfile
 */
void _initLogFile()
{
    initLogDone = 0;

    /* Get process name
     */
    const char *processName = get_process_name();
    
    // if we're a blacklisted process
    // skip logging
    if (!strncmp(processName, "zygote",6) || !strncmp(processName, "dexopt", 6) 
            || !strncmp(processName, "system_server", 6)) {
        return;
    }

    ALOGD("Robust Log %s != zygote not skipping", processName);

    // we're a valid process so set
    // initialization as complete
    initLogDone = 1;

    /* Get start time so we can identify seperate runs
     */
    clock_gettime(CLOCK_REALTIME, &startTime);
    time_t mytime;
    mytime = time(NULL);
    char *timeStart = ctime(&mytime);	
    removeNewLines(timeStart);	 

    char polFile[] = "/sdcard/robust/GCPolicy";
    char polVal[2];
    GcPolSpec policy;

    // check and see if we have GC policy
    // file to read the policy from
    FILE *fdPol = fopen(polFile, "rt");
    ALOGD("Robust Log attempting to open Policy file %s", polFile);
    if (fdPol != NULL) {
        ALOGD("Robust Log policy file open");
        fscanf(fdPol, "%s", polVal);
        fclose(fdPol);
    }
	
    policy = policies[0];

    if (polVal[0]) {
	    int polNumb = atoi(polVal);
	    if ((polNumb >= 0) && (polNumb < NUM_POLICIES)) {
	      policy = policies[polNumb - 1];
	    }
    }
	
    // set up the policy we'll be executing
    // read the numbers from the list we have stored
    // TODO: we'll get better granularity with the
    // hires timer but overhead might be costly
    const char *policyName = policy.name;
    policyNumber = policy.policyNumber;
    minGCTime = policy.minTime;
    intervals = policy.intervals;
    ALOGD("Robust Log policy Number %d", policyNumber);
	
    // figure out actual timer granularity
    u8 startG = dvmGetTotalProcessCpuTimeNsec();
    u8 end = startG;
    while(true)
    {
        end=dvmGetTotalProcessCpuTimeNsec();
        if(end!=startG) {
        break;
        }
    } 
    u8 diff2=end-startG;

    ALOGD("Robust Log Granularity is %llu", diff2);

    char baseDir[] = "/sdcard/robust/";
    char fileName[128];
    strcpy(fileName, baseDir);
    strcat(fileName, processName);

    ALOGD("Robust Log ||%s||", fileName);
    fileLog = fopen(fileName, "at" );
    if (fileLog != NULL) {		
        fprintf(fileLog, "\n\n@header{\"device\":maguro,\"process\":\"%s\",\"policy\":\"%s\",\"appStartTime-ms\":%llu,\"startTime\":\"%s\",\"timerResolution-ns\":%llu}\n", 
            processName, policyName,dvmGetRTCTimeMsec(), timeStart,diff2);
    }
    else {
        ALOGD("Robust Log fail open %s %s", processName,strerror(errno));
        logReady = false;
        return;
    }
	
    // set initial values	
    logReady = true;
    seqNumber = 1;
    threshold = (128 << 10);
    threshSet = false;
}

// get time from RTC
// could shift slightly but shouldn't affect us much
u8 dvmGetRTCTimeNsec()
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

void removeNewLines(char *input)
{
    // erase any stray newline characters
    // in time
    int i = 0;
    while ((i >= 0) && (i < 128)) {
        if (input[i] == '\n') {
            input[i] = ' ';
            i = -2;
        }
        // if we hit null exit
        if (input[i] == '\0') {
            i = -2;
        }
        i++;
    }
}

void logMeInit() 
{
    initLogFile();
}
int seqNumber;
bool logReady;
FILE* fileLog;
//string policyName;
int policyNumber;
unsigned int minGCTime;
unsigned int intervals;

size_t lastRequestedSize = 0;
//string processName;
int freeHistory[10]; // histogram
int threshold; // threshold for starting concurrent GC
u8 lastGCTime = 0; // for scheduling GCs
