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
u8 rootScanTime = 0;
int currInterval = 0;
bool threshSet;
struct timespec startTime;
int mallocsDone;

//GC Policies
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
  "baseline",
  1,
  0,
  0
};
GcPolSpec *stock = &stockPol;

static GcPolSpec GcPolMI2 = {
  "MI2",
  2,
  2000,
  0
};

GcPolSpec *MI2 = &GcPolMI2;

static GcPolSpec GcPolMI2S = {
  "MI2S",
  3,
  2000,
  5
};

GcPolSpec *MI2S = &GcPolMI2S;

static GcPolSpec GcPolMI2A = {
  "MI2A",
  4,
  2000,
  5
};

GcPolSpec *MI2A = &GcPolMI2A;

static GcPolSpec GcPolMI2AE = {
  "MI2AE",
  5,
  2000,
  5
};

GcPolSpec *MI2AE = &GcPolMI2AE;

const GcPolSpec policies[5] = {stockPol, GcPolMI2, GcPolMI2S, GcPolMI2A, GcPolMI2AE};
GcPolSpec policy;

void _logPrint(int logEventType, bool mallocFail, const GcSpec* spec)
{
    static int numEvents = 0;
    if (skipLogging) {
        numEvents++;
        if (numEvents >= 500) {
            scheduleConcurrentGC();
            numEvents =0;
        }
        return;
    }
    
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
        case LOG_GC_SCHED:
                logGCSched();
                break;
    }
}

void logGC(const GcSpec* spec)
{
    static int logStage = 0;
    static int thisGCSeqNumb;
    string beginOrEnd;
    
    switch (logStage)
    {
        case 0:
            beginOrEnd = "begin";
            thisGCSeqNumb = seqNumber++;
            logStage = 1;
            break;
        case 1:
            logStage = 2;
            rootScanTime = dvmGetTotalProcessCpuTimeMsec();
            return;
        default:
            beginOrEnd = "end";
            logStage = 0;
    }
    /*
    if (logStage == 0) {
        beginOrEnd = "begin";
        thisGCSeqNumb = seqNumber++;
        logStage = 1;
    }
    else if (logStage == 1) {
        logStage = 2;
        rootScanTime = dvmGetTotalProcessCpuTimeMsec();
        return;
    }        
    else {
        beginOrEnd = "end";
        logStage = 0;
    }*/

    writeLogEvent(LOG_GC, beginOrEnd.c_str(), "GC", thisGCSeqNumb, spec);
    
}

void logMalloc(bool mallocFail)
{
    static bool logStart = true;
    static int thisMallocSeqNumb;
    static int maxMallocs = 2000;
    static int numChecks = 0;
    string beginOrEnd;

    static u8 lastMallocTime = 0;
    //u8 currentMallocTime = dvmGetTotalProcessCpuTimeMsec();
    static int numMallocs = 0;

    // to keep the compiler quiet
    logStart = logStart;

    if (logStart) {
        // if last malloc was more than 100 ms ago
        // or if last malloc was less than 100 ms ago and malloc failed
        // log this malloc
        if (mallocFail) {
            beginOrEnd = "begin";
            thisMallocSeqNumb = seqNumber++;
            logStart = false;
            mallocsDone = (maxMallocs * numChecks) + numMallocs;
            writeLogEvent(LOG_TRY_MALLOC, beginOrEnd.c_str(), "TryMalloc", thisMallocSeqNumb, NULL, true);
            //lastMallocTime = currentMallocTime;
            numMallocs = 0;
        }
        
        if (numMallocs > maxMallocs) {
            u8 currentMallocTime = dvmGetRTCTimeMsec();
            numChecks++;
            if (currentMallocTime - lastMallocTime > 100) {
                beginOrEnd = "begin";
                thisMallocSeqNumb = seqNumber++;
                logStart = false;
                mallocsDone = maxMallocs * numChecks;
                writeLogEvent(LOG_TRY_MALLOC, beginOrEnd.c_str(), "TryMalloc", thisMallocSeqNumb, NULL);
                // schedule concurrent GC if needed
                scheduleConcurrentGC();
                lastMallocTime = currentMallocTime;
                //ALOGD("maxMallocs %d numChecks %d currentMallocTime %llu lastMallocTime %llu", maxMallocs, numChecks, currentMallocTime, lastMallocTime);
                maxMallocs = numChecks * maxMallocs / 10;
                // under certain (rare) circumstances max can go very low
                // this is to prevent that
                if (maxMallocs < 100) {
                    maxMallocs = 100;
                }
                numChecks = 0;
            }
            numMallocs = 0;
        }
        numMallocs++;
    }
    else {
        beginOrEnd = "end";
        logStart = true;
        writeLogEvent(LOG_TRY_MALLOC, beginOrEnd.c_str(), "TryMalloc", thisMallocSeqNumb, NULL);
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

    writeLogEvent(LOG_WAIT_CONC_GC, beginOrEnd.c_str(), "WaitConcGC", thisGcSeqNumb, NULL);
}

/*
 * Logs the time when a GC has been sheduled
 */
void logGCSched(void)
{
    // all we need to log is time so no need to get complicated
    writeLogEvent(LOG_GC_SCHED, "", "schedGC", seqNumber++, NULL);   
}

/*
 * builds a string of the basic log info
 */
char* buildBasicEvent(const char* beginEnd,const char* eventName, int seqNumber, char output[])
{
    u8 wcTime = dvmGetRTCTimeMsec();
    u8 appTime = dvmGetTotalProcessCpuTimeMsec();

    // not sure how reliable this next section is
    //char cpuSpeed[] = "noData";
    //logCPUSpeed(cpuSpeed);

    sprintf(output, "@%s%s{\"seqNum\":%d,\"wcTime-ms\":%llu,\"appTime-ms\":%llu",
        beginEnd, eventName, seqNumber, wcTime, appTime);
    return output;
}

/*
 * builds a string of log info with heap stats
 */
void buildHeapEvent(const char* beginEnd,const char* eventName, int seqNumber, char output[])
{
    char partial[MAX_STRING_LENGTH];
    size_t heapsAlloc[2], heapsFootprint[2], heapsMax[2], numObjects[2];
    float thresholdKb = threshold / 1024.0;
	heapsAlloc[1] = heapsFootprint[1] = heapsAlloc[0] = heapsFootprint[0] = heapsMax[0] = heapsMax[1] = 0;

    dvmHeapSourceGetValue(HS_BYTES_ALLOCATED, heapsAlloc, 2);
    dvmHeapSourceGetValue(HS_FOOTPRINT, heapsFootprint, 2);
    dvmHeapSourceGetValue(HS_ALLOWED_FOOTPRINT, heapsMax, 2);
    dvmHeapSourceGetValue(HS_OBJECTS_ALLOCATED, numObjects, 2);

    heapsAlloc[0] = heapsAlloc[0] / 1024;
    heapsFootprint[0] = heapsFootprint[0] / 1024;
    heapsMax[0] = heapsMax[0] / 1024;
    heapsAlloc[1] = heapsAlloc[1] / 1024;
    heapsFootprint[1] = heapsFootprint[1] / 1024;
    heapsMax[1] = heapsMax[1] / 1024;

    buildBasicEvent(beginEnd, eventName, seqNumber, partial);

    sprintf(output, "%s,\"currAlloc0-kB\":%d,\"currFootprint0-kB\":%d,\"currMax0-kB\":%d,\"numObjects0\":%d,\"currAlloc1-kB\":%d,\"currFootprint1-kB\":%d,\"currMax1-kB\":%d,\"numObjects1\":%d,\"threshold-kB\":%f",
        partial, heapsAlloc[0], heapsFootprint[0], heapsMax[0], numObjects[0], heapsAlloc[1], heapsFootprint[1], heapsMax[1], numObjects[1], thresholdKb); 
}

/*
 * builds a string of heap stats with a GC type
 */
void buildGCEvent(const char* beginEnd,const char* eventName, int seqNumber, const GcSpec *spec, char output[])
{
    char partial[MAX_STRING_LENGTH];
    buildHeapEvent(beginEnd, eventName, seqNumber, partial);
    char cpuSpeed[] = "noData   ";
    logCPUSpeed(cpuSpeed);

    sprintf(output, "%s,\"GCType\":\"%s\",\"cpuSpeed-Hz\":\"%s\",\"rootScanTime\":%llu", partial, spec->reason, cpuSpeed, rootScanTime);
}

/*
 * writes a log event to a file
 */
void writeLogEvent(int eventType,const char* beginEnd, const char* eventName, int seqNumber, const GcSpec *spec, bool mallocFail)
{
    char partialEntry[MAX_STRING_LENGTH];
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
                char temp[MAX_STRING_LENGTH];
                buildHeapEvent(beginEnd, eventName, seqNumber, temp);
                sprintf(partialEntry, "%s,\"mallocFail\":\"%d\",\"numMallocs\":%d", temp, (int)mallocFail, mallocsDone);
                break;
        case LOG_GC:
                buildGCEvent(beginEnd, eventName, seqNumber, spec, partialEntry);
                break;
        case LOG_GC_SCHED:
        case LOG_WAIT_CONC_GC:
                buildBasicEvent(beginEnd, eventName, seqNumber, partialEntry);
                break;
    }   

    fprintf(fileLog, "%s}\n", partialEntry);
    numLogEvents++;    
}

void scheduleConcurrentGC() 
{
  // only adaptive policies schedule concurrent GC
  if (policyNumber >= 4) {
	u8 timeSinceLastGC = dvmGetRTCTimeMsec() - lastGCTime;

	// check and see if we're at the min time from a concurrent GC
	if (timeSinceLastGC > minGCTime)	{
	  // FIXME check heap 0 instead of whole heap set last GC time on GC completion
	  // if we've hit the threshold schedule a concurrent GC
	  size_t heapsAlloc[2], heapsFootprint[2];
	  heapsAlloc[1] = heapsFootprint[1] = heapsAlloc[0] = heapsFootprint[0] = 0;

	  dvmHeapSourceGetValue(HS_BYTES_ALLOCATED, heapsAlloc, 2);
	  dvmHeapSourceGetValue(HS_FOOTPRINT, heapsFootprint, 2);

	  if ((threshold >= (heapsFootprint[0] - heapsAlloc[0])) || schedGC) {
        logPrint(LOG_GC_SCHED);
        schedGC = false;
        // to prevent succesive calls from succeding
        // if gc doesn't complete soon
        lastGCTime = dvmGetRTCTimeMsec();
		dvmInitConcGC();
	  }
	}
  }
}

/*
 * Saves free memory history
 */

void saveHistory(){

    //if (!threshSet) {
        size_t heapsAlloc[2], heapsFootprint[2];
        heapsAlloc[1] = heapsFootprint[1] = heapsAlloc[0] = heapsFootprint[0] = 0;

        dvmHeapSourceGetValue(HS_BYTES_ALLOCATED, heapsAlloc, 2);
        dvmHeapSourceGetValue(HS_FOOTPRINT, heapsFootprint, 2);
		freeHistory[currInterval] = heapsFootprint[0] - heapsAlloc[0];
		currInterval = (currInterval + 1) % intervals;
	//} 
}

void setThreshold(void)
{
    // originally was only set once now we set each time
	threshold = freeHistory[(currInterval + (intervals + 1)) % intervals];
	threshSet = true;
}
    
/* 
 * Initializes the robust logfile
 */
void _initLogFile()
{
    initLogDone = 0;
    skipLogging = 0;

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

    char polFile[] = "/sdcard/robust/GCPolicy.txt";
    char polVal[2];
    GcPolSpec policy;

    // check and see if we have GC policy
    // file to read the policy from
    FILE *fdPol = fopen(polFile, "rt");
    //ALOGD("Robust Log attempting to open Policy file %s", polFile);
    if (fdPol != NULL) {
        //ALOGD("Robust Log policy file open");
        fscanf(fdPol, "%s", polVal);
        fclose(fdPol);
    }
	
	// defaults 
	// MI2A and no logging
    policy = policies[0];

    if (polVal[0]) {
	    int polNumb = atoi(polVal);
	    ALOGD("Policy Number %d", polNumb);
	    
	    // check if logging should be skipped
	    if (polNumb < 0) {
	        skipLogging = 1;
	        polNumb = polNumb * -1;
	        ALOGD("Skipping GC/Malloc logging using policy %d skipLogging %d", polNumb, skipLogging);
	    } 
	    if ((polNumb > 0) && (polNumb <= NUM_POLICIES)) {
	      policy = policies[polNumb - 1];
	      skipLogging = 0;
	    }
    }
    else {
        policy = policies[3];
        skipLogging = 1;
    }
	
    // set up the policy we'll be executing
    // read the numbers from the list we have stored
    // TODO: we'll get better granularity with the
    // hires timer but overhead might be costly
    //const char *policyName = policy.name;
    policyNumber = policy.policyNumber;
    minGCTime = policy.minTime;
    intervals = policy.intervals;
    ALOGD("Robust Log policy Number %d", policyNumber);
	ALOGD("MinGCTime %d", minGCTime);
	
	// set initial values	
    logReady = true;
    seqNumber = 1;
    threshold = (128 << 10);
    threshSet = false;
    schedGC = false;
	
    if (skipLogging) {
        return;
    }
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

    //LOGD("Robust Log Granularity is %llu", diff2);

    char baseDir[] = "/sdcard/robust/";
    char fileName[128];
    strcpy(fileName, baseDir);
    strcat(fileName, processName);
    strcat(fileName, ".txt");
    
    // create the directory for log files
    mkdir("/sdcard/robust",  S_IRWXU | S_IRWXG | S_IRWXO);

    //ALOGD("Robust Log ||%s||", fileName);
    fileLog = fopen(fileName, "at" );
    if (fileLog != NULL) {
        // bump our buffer log size to 12k
        setvbuf(fileLog, NULL, _IOFBF, 12287);
        fprintf(fileLog, "\n\n@header{\"device\":\"maguro\",\"process\":\"%s\",\"policy\":\"%d\",\"appStartTime-ms\":%llu,\"startTime\":\"%s\",\"timerResolution-ns\":%llu}\n", 
            processName, policyNumber,dvmGetRTCTimeMsec(), timeStart,diff2);
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
    schedGC = false;
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

void logCPUSpeed(char* speed)
{
    char cpuFileName[] = "/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_cur_freq";
    FILE* cpuFile = fopen(cpuFileName, "rt" );

    if (cpuFile && fscanf(cpuFile, "%s", speed)) {
        fclose(cpuFile);
    }
    else {
        speed[0] = '\0';
        ALOGD("Robust Log Failed to open CPU speed file: %s",strerror(errno));
    }
    
}
    

int skipLogging = 0;
int seqNumber;
bool logReady;
bool schedGC; // Sched GC
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
