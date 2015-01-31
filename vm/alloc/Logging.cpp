#include <iostream>
#include <fstream>
#include <string>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <inttypes.h>

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
#include "sched.h"
#include "alloc/DlMalloc.h"

#define BUILD_ID "0725-2130-logOthers-noDalvik-saveStats"
#define CONCURRENT_START_DEFAULT (128 << 10)
#define NUM_GC_AVERAGES 10
#define DEFAULT_POLICY 13

// comment out to remove 
// logcat debugging s

//#define snappyDebugging 1
//#define snappyDebuggingHistory 1
//#define logWriteTime 1

// To log processes that don't have sdcard access to 
// a community file
#define logOthers 1

// To not log system processes
// logging system processes causes the system to 
// crash after a period of time
#define dontLogAll 1

int tryMallocSequenceNumber = 0;
unsigned short timeToAdd;
u8 currentMallocTime = 0;
u8 rootScanTime = 0;
u8 gcStartTime; // start time of the last GC
u8 lastExhaustion;
size_t allocHistory[] = {0,0,0};
size_t hares[] = {0,0,0};
size_t baseAddr;
int currAllocHistory = 0;
int currInterval = 0;
bool threshSet;
struct timespec startTime;
int mallocsDone;
char thisProcessName[80];
char logPrefix[80];	// for the apps that don't have sdcard access
char deviceName[25];
int inGC;
int preDone = 0; // if dex or sysopt is done initing
struct timespec minSleepTime;
FILE* fileTest = NULL;
FILE* others = NULL;
FILE* notLogged = NULL;
FILE* memDumpFile = NULL;

int testTime(void);
char fileName[128];

u8 gcAverage[10] = {0,0,0,0,0,0,0,0,0,0};
int currGCAverage;

//GC Policies
//typedef struct GcPolSpec GcPolSpec
struct GcPolSpec {
        /* Name of the policy */
        const char *name;
        /* Policy Number */
        int policyNumber;
        /* Policy Type */
        int type;
        /* If STW (foreground only policy) */
        bool MIS;
        /* Minimum GC time in ms*/
        unsigned int minTime;
        /* number of 100ms increments keep size log */
        unsigned int intervals;
        /* if policy actively schedules GC */
        bool schedulesGC;
		/* is this an adaptive policy */
		unsigned int adaptive;
		/* is this a spleen policy */
		bool spleen;
		/* resize threshold on GC */
		unsigned int resizeThreshold;
		/* disable heap resize on GC */
		bool resizeOnGC;
		/* number of ms to push forward if GC finishes early */
		unsigned short timeToAdd;
		/* number of ms to wait for conc GC to complete */
		unsigned short timeToWait;
		/* number of times to wait before running full GC */
		unsigned short numIterations;
		/* alpha for partial GC weight average */
		float partialAlpha;
		/* alpha for full GC weight average */
		float fullAlpha;
		/* beta for partial/full GC scalling */
		float beta;
};

static GcPolSpec stockPol = {
	"baseline",
	1,		// Policy Number
	BASELINE,       // Policy Type
	false,  // MIS policy
	0,		// minGCTime
	0,		// 100ms intervals for histogram
	false,  // if we actively schedule GC
	0,		// adaptive
	false,  // spleen policy
	0,		// resize threshold on GC
	true,	// resize heap on gc
	0,		// time to add
	0,		// time to wait
	0,		// number of iterations to wait	
	1,		// alpha for partial gc average
	1,		// alpha for full gc average
	1		// beta
};
GcPolSpec *stock = &stockPol;

static GcPolSpec GcPolMI2 = {
	"MI2",
	2,		// Policy Number
	MIN_INTERVAL,       // Policy Type
	false,  // MIS policy
	2000,	// minGCTime
	0,		// 100ms intervals for histogram
	false,  // if we actively schedule GC
	0,		// adaptive
	false,  // spleen policy
	0,		// resize threshold on GC
	true,	// resize heap on gc
	0,		// time to add
	0,		// time to wait
	0,		// number of iterations to wait	
	1,		// alpha for partial gc average
	1,		// alpha for full gc average
	1		// beta
};
GcPolSpec *MI2 = &GcPolMI2;

static GcPolSpec GcPolMI2S = {
	"MI2S",
	3,		// Policy Number
	STW + IGNORE_EXPLICIT,        // Policy Type
	true,  // MIS policy
	2000,	// minGCTime
	5,		// 100ms intervals for histogram
	false,  // if we actively schedule GC
	0,		// adaptive
	false,  // spleen policy
	0,		// resize threshold on GC
	true,	// resize heap on gc
	0,		// time to add
	0,		// time to wait
	0,		// number of iterations to wait	
	1,		// alpha for partial gc average
	1,		// alpha for full gc average
	1		// beta
};

GcPolSpec *MI2S = &GcPolMI2S;

static GcPolSpec GcPolMI2A = {
	"MI2A",
	4,		// Policy Number
	MIN_INTERVAL + ADAPTIVE,     // Policy Type
	false,  // MIS policy
	2000,	// minGCTime
	5,		// 100ms intervals for histogram
	true,  // if we actively schedule GC
	1,		// adaptive
	false,  // spleen policy
	0,		// resize threshold on GC
	true,	// resize heap on gc
	0,		// time to add
	0,		// time to wait
	0,		// number of iterations to wait	
	1,		// alpha for partial gc average
	1,		// alpha for full gc average
	1		// beta
};

GcPolSpec *MI2A = &GcPolMI2A;

static GcPolSpec GcPolMI2AE = {
	"MI2AE",
	5,		// Policy Number
	MIN_INTERVAL + ADAPTIVE + IGNORE_EXPLICIT + EXPLICIT_HINT,   // Policy Type
	false,  // MIS policy
	2000,	// minGCTime
	5,		// 100ms intervals for histogram
	true,  // if we actively schedule GC
	1,		// adaptive
	false,  // spleen policy
	0,		// resize threshold on GC
	true,	// resize heap on gc
	0,		// time to add
	0,		// time to wait
	0,		// number of iterations to wait	
	1,		// alpha for partial gc average
	1,		// alpha for full gc average
	1		// beta
};

GcPolSpec *MI2AE = &GcPolMI2AE;

static GcPolSpec GcPolMI2AI = {
	"MI2AI",
	6,		// Policy Number
	MIN_INTERVAL + ADAPTIVE + IGNORE_EXPLICIT,   // Policy Type
	false,  // MIS policy
	2000,	// minGCTime
	5,		// 100ms intervals for histogram
	true,   // if we actively schedule GC
	1,		// adaptive
	false,  // spleen policy
	0,		// resize threshold on GC
	true,	// resize heap on gc
	0,		// time to add
	0,		// time to wait
	0,		// number of iterations to wait	
	1,		// alpha for partial gc average
	1,		// alpha for full gc average
	1		// beta
};

GcPolSpec *MI2AI = &GcPolMI2AI;

static GcPolSpec GcPolMI1AI = {
	"MI1AI",
	7,		// Policy Number
	MIN_INTERVAL + ADAPTIVE + IGNORE_EXPLICIT,   // Policy Type
	false,  // MIS policy
	1000,	// minGCTime
	5,		// 100ms intervals for histogram
	true,  // if we actively schedule GC
	1,		// adaptive
	false,  // spleen policy
	0,		// resize threshold on GC
	true,	// resize heap on gc
	0,		// time to add
	0,		// time to wait
	0,		// number of iterations to wait	
	1,		// alpha for partial gc average
	1,		// alpha for full gc average
	1		// beta
};

GcPolSpec *MI1AI = &GcPolMI1AI;

static GcPolSpec GcPolMI2AD = {
	"MI2AD",
	8,		// Policy Number
	MIN_INTERVAL + ADAPTIVE + IGNORE_EXPLICIT,   // Policy Type
	false,  // MIS policy
	2000,	// minGCTime
	5,		// 100ms intervals for histogram
	true,  // if we actively schedule GC
	1,		// adaptive
	false,  // spleen policy
	1,		// resize threshold on GC
	true,	// resize heap on gc
	0,		// time to add
	0,		// time to wait
	0,		// number of iterations to wait	
	1,		// alpha for partial gc average
	1,		// alpha for full gc average
	1		// beta
};

GcPolSpec *MI2AD = &GcPolMI2AD;

static GcPolSpec GcPolMI1D = {
	"MI1D",
	9,		// Policy Number
	MIN_INTERVAL + ADAPTIVE + IGNORE_EXPLICIT,   // Policy Type
	false,  // MIS policy
	1000,	// minGCTime
	5,		// 100ms intervals for histogram
	true,  // if we actively schedule GC
	1,		// adaptive
	false,  // spleen policy
	1,		// resize threshold on GC
	true,	// resize heap on gc
	0,		// time to add
	0,		// time to wait
	0,		// number of iterations to wait	
	1,		// alpha for partial gc average
	1,		// alpha for full gc average
	1		// beta
};

GcPolSpec *MI1D = &GcPolMI1D;

static GcPolSpec GcPolMMI2AD = {
	"MMI2AD",
	10,		// Policy Number
	MIN_INTERVAL + ADAPTIVE + IGNORE_EXPLICIT + MIN_CPU_INTERVAL,   // Policy Type
	false,  // MIS policy
	2000,	// minGCTime
	5,		// 100ms intervals for histogram
	true,  // if we actively schedule GC
	1,		// adaptive
	false,  // spleen policy
	1,		// resize threshold on GC
	true,	// resize heap on gc
	0,		// time to add
	0,		// time to wait
	0,		// number of iterations to wait	
	1,		// alpha for partial gc average
	1,		// alpha for full gc average
	1		// beta
};

static GcPolSpec GcPolMMI1D = {
	"MMI1D",
	11,		// Policy Number
	MIN_INTERVAL + ADAPTIVE + IGNORE_EXPLICIT + MIN_CPU_INTERVAL,   // Policy Type
	false,  // MIS policy
	1000,	// minGCTime
	5,		// 100ms intervals for histogram
	true,  // if we actively schedule GC
	1,		// adaptive
	false,  // spleen policy
	0,		// resize threshold on GC
	true,	// resize heap on gc
	0,		// time to add
	0,		// time to wait
	0,		// number of iterations to wait	
	1,		// alpha for partial gc average
	1,		// alpha for full gc average
	1		// beta
};

GcPolSpec *MMI1D = &GcPolMMI1D;

static GcPolSpec GcPolRT1 = {
	"RT1",
	12,		// Policy Number
	MIN_INTERVAL + ADAPTIVE + IGNORE_EXPLICIT + MIN_CPU_INTERVAL + THROTTLE_THRESHOLD,   // Policy Type
	false,  // MIS policy
	1000,	// minGCTime
	5,		// 100ms intervals for histogram
	true,  // if we actively schedule GC
	1,		// adaptive
	false,  // spleen policy
	0,		// resize threshold on GC
	false,	// resize heap on gc
	10,		// time to add
	15,		// time to wait
	50,		// number of iterations to wait	
	.2,		// alpha for partial gc average
	.5,		// alpha for full gc average
	1		// beta
};
GcPolSpec *RT1 = &GcPolRT1;

static GcPolSpec GcPolRT2 = {
	"RT2",
	13,		// Policy Number
	MIN_INTERVAL + ADAPTIVE + IGNORE_EXPLICIT + MIN_CPU_INTERVAL + THROTTLE_THRESHOLD,   // Policy Type
	false,  // MIS policy
	2000,	// minGCTime
	5,		// 100ms intervals for histogram
	true,  // if we actively schedule GC
	1,		// adaptive
	false,  // spleen policy
	0,		// resize threshold on GC
	false,	// resize heap on gc
	10,		// time to add
	15,		// time to wait
	50,		// number of iterations to wait	
	.2,		// alpha for partial gc average
	.5,		// alpha for full gc average
	1		// beta
};

GcPolSpec *RT2 = &GcPolRT2;

static GcPolSpec GcPolRT4 = {
	"RT4",
	14,		// Policy Number
	MIN_INTERVAL + ADAPTIVE + IGNORE_EXPLICIT + MIN_CPU_INTERVAL + THROTTLE_THRESHOLD,   // Policy Type
	false,  // MIS policy
	4000,	// minGCTime
	5,		// 100ms intervals for histogram
	true,  // if we actively schedule GC
	1,		// adaptive
	false,  // spleen policy
	0,		// resize threshold on GC
	false,	// resize heap on gc
	10,		// time to add
	15,		// time to wait
	50,		// number of iterations to wait	
	.2,		// alpha for partial gc average
	.5,		// alpha for full gc average
	1		// beta
};

GcPolSpec *RT4 = &GcPolRT4;

static GcPolSpec GcPolSpleen4 = {
	"HareySpleen4",
	15,		// Policy Number
	MIN_INTERVAL + ADAPTIVE + IGNORE_EXPLICIT + MIN_CPU_INTERVAL + THROTTLE_THRESHOLD + SPLEEN,   // Policy Type
	false,  // MIS policy
	4000,	// minGCTime
	5,		// 100ms intervals for histogram
	true,  // if we actively schedule GC
	1,		// adaptive
	true,  // spleen policy
	0,		// resize threshold on GC
	false,	// resize heap on gc
	10,		// time to add
	15,		// time to wait
	50,		// number of iterations to wait	
	.2,		// alpha for partial gc average
	.5,		// alpha for full gc average
	1		// beta
};

GcPolSpec *Spleen4 = &GcPolSpleen4;

static GcPolSpec GcPolSpleen1 = {
	"HareySpleen1",
	16,		// Policy Number
	MIN_INTERVAL + ADAPTIVE + IGNORE_EXPLICIT + MIN_CPU_INTERVAL + THROTTLE_THRESHOLD + SPLEEN,   // Policy Type
	false,  // MIS policy
	1000,	// minGCTime
	5,		// 100ms intervals for histogram
	true,  // if we actively schedule GC
	1,		// adaptive
	true,  // spleen policy
	0,		// resize threshold on GC
	false,	// resize heap on gc
	10,		// time to add
	15,		// time to wait
	50,		// number of iterations to wait	
	.2,		// alpha for partial gc average
	.5,		// alpha for full gc average
	1		// beta
};

GcPolSpec *Spleen1 = &GcPolSpleen1;

static GcPolSpec GcPolSpleen2 = {
	"HareySpleen2",
	17,		// Policy Number
	MIN_INTERVAL + ADAPTIVE + IGNORE_EXPLICIT + MIN_CPU_INTERVAL + THROTTLE_THRESHOLD + SPLEEN,   // Policy Type
	false,  // MIS policy
	2000,	// minGCTime
	5,		// 100ms intervals for histogram
	true,  // if we actively schedule GC
	1,		// adaptive
	true,  // spleen policy
	0,		// resize threshold on GC
	false,	// resize heap on gc    
	10,		// time to add
	15,		// time to wait
	50,		// number of iterations to wait	
	.2,		// alpha for partial gc average
	.5,		// alpha for full gc average
	1		// beta
};

GcPolSpec *Spleen2 = &GcPolSpleen2;

static GcPolSpec GcPolMI1S = {
	"MI1S",
	18,		// Policy Number
	STW + IGNORE_EXPLICIT,      // Policy Type
	true,  // MIS policy
	1000,	// minGCTime
	5,		// 100ms intervals for histogram
	false,  // if we actively schedule GC
	0,		// adaptive
	false,  // spleen policy
	0,		// resize threshold on GC
	true,	// resize heap on gc
	0,		// time to add
	0,		// time to wait
	0,		// number of iterations to wait	
	1,		// alpha for partial gc average
	1,		// alpha for full gc average
	1		// beta
};

GcPolSpec *MI1S = &GcPolMI1S;

static GcPolSpec GcPolMI4S = {
	"MI4S",
	19,		// Policy Number
	STW + IGNORE_EXPLICIT,      // Policy Type
	true,  // MIS policy
	4000,	// minGCTime
	5,		// 100ms intervals for histogram
	false,  // if we actively schedule GC
	0,		// adaptive
	false,  // spleen policy
	0,		// resize threshold on GC
	true,	// resize heap on gc
	0,		// time to add
	0,		// time to wait
	0,		// number of iterations to wait	
	1,		// alpha for partial gc average
	1,		// alpha for full gc average
	1		// beta
};

GcPolSpec *MI4S = &GcPolMI4S;

static GcPolSpec GcPolMAXS = {
	"MAXS",
	20,		// Policy Number
	MAX_GROW_STW + IGNORE_EXPLICIT ,      // Policy Type this grows heap to max so rest doesn't matter
	false,  // MIS policy
	0,  	// minGCTime
	5,		// 100ms intervals for histogram
	false,  // if we actively schedule GC
	0,		// adaptive
	false,  // spleen policy
	0,		// resize threshold on GC
	true,	// resize heap on gc
	0,		// time to add
	0,		// time to wait
	0,		// number of iterations to wait	
	1,		// alpha for partial gc average
	1,		// alpha for full gc average
	1		// beta
};

GcPolSpec *MAXS = &GcPolMAXS;

static GcPolSpec GcPolMAXG = {
	"MAXG",
	21,		// Policy Number
	MAX_GROW_BG + IGNORE_EXPLICIT,      // Policy Type this grows heap to max so rest doesn't matter
	false,  // MIS policy
	0,	// minGCTime
	5,		// 100ms intervals for histogram
	false,  // if we actively schedule GC
	0,		// adaptive
	false,  // spleen policy
	0,		// resize threshold on GC
	true,	// resize heap on gc
	0,		// time to add
	0,		// time to wait
	0,		// number of iterations to wait	
	1,		// alpha for partial gc average
	1,		// alpha for full gc average
	1		// beta
};

GcPolSpec *MAXG = &GcPolMAXG;

const GcPolSpec policies[NUM_POLICIES] = {stockPol, 
										  GcPolMI2, 
										  GcPolMI2S, 
										  GcPolMI2A, 
										  GcPolMI2AE, 
										  GcPolMI2AI, 
										  GcPolMI1AI, 
										  GcPolMI2AD, 
										  GcPolMI1D, 
										  GcPolMMI2AD, 
										  GcPolMMI1D, 
										  GcPolRT1, 
										  GcPolRT2,
										  GcPolRT4,
										  GcPolSpleen4,
										  GcPolSpleen1,
										  GcPolSpleen2, 
										  GcPolMI1S, 
										  GcPolMI4S,
										  GcPolMAXS,
										  GcPolMAXG};
GcPolSpec policy;

/*
 * sets constraints to the correct policy
 */
void setPolicy(int policyNumb);


void _logPrint(int logEventType, bool mallocFail, const GcSpec* spec)
{
        
    if (skipLogging) {
        #ifdef snappyDebugging
        ALOGD("Skipping Logging %d", logEventType);
        #endif
        return;
    }
    
    initLogFile();
    if((!logReady) || skipLogging) {
        #ifdef snappyDebugging
        ALOGD("Robust Log Not Ready %d", logEventType);
        #endif
        return;
    }
	
    if (fileLog == NULL) {
        //#ifdef snappyDebugging
        ALOGD("GC Logging file closed after succesful open, assertion would have failed");
        //#endif
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
        case LOG_IGNORE_EXPLICIT:
                logIgnoreExplicit();
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
			rootScanTime = 0;
			lastGCSeqNumber = thisGCSeqNumb;
            break;
        case 1:
            logStage = 2;
            rootScanTime = dvmGetTotalThreadCpuTimeMsec();
            return;
        default:
            beginOrEnd = "end";
            logStage = 0;
            
            // experimental
            // read the performance counters and print results
            char custMsg[256];
            uint64_t *results;//[5] = {0,0,0,0,0};
            results = (uint64_t*)malloc(sizeof(uint64_t) * 10);
            results[0] = results[1] = results[2] = results[3] = results[4] = 0;
            #ifdef snappyDebugging
            ALOGD("Robust Log reading pefcounters");
            #endif
            readCounters(results);
            sprintf(custMsg, ",\"CPUType\":\""ARCH"\",\"cycles\":%llu,\"instructions\":%llu,\"tlb-stalls\":%llu,\"instr-stalls\":%llu,"
                            "\"data-stalls\":%llu,\"issue-empty\":%llu,\"no-dispatch\":%llu,\"page-faults\":%llu"
                ,results[0]
                ,results[1]
                ,results[2]
                ,results[3]
                ,results[4]
                ,results[5]
                ,results[6]
                ,results[7]);
            logPrint(LOG_CUSTOM,"perfCountGC", (char*)custMsg);
            free(results);
    }

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
            mallocsDone += numMallocs;
            writeLogEvent(LOG_TRY_MALLOC, beginOrEnd.c_str(), "TryMalloc", thisMallocSeqNumb, NULL, true);
            //lastMallocTime = currentMallocTime;
            numMallocs = 0;
        }
        
        if (numMallocs > maxMallocs) {
            u8 currentMallocTime = dvmGetRTCTimeMsec();
            numChecks++;
			//maxMallocs = ((numMallocs >> 4)  * (100.0 / (((float)currentMallocTime - (float)lastMallocTime) + 1.0))) + 50.0;
			mallocsDone +=  numMallocs;
            numMallocs = 0;
            if (currentMallocTime - lastMallocTime > 100) {
                beginOrEnd = "begin";
                thisMallocSeqNumb = seqNumber++;
                logStart = false;
                #ifdef snappyDebugging
                ALOGD("Robust maxMallocs %d, numChecks %d", maxMallocs, numChecks);
                #endif
                writeLogEvent(LOG_TRY_MALLOC, beginOrEnd.c_str(), "TryMalloc", thisMallocSeqNumb, NULL);
                
                lastMallocTime = currentMallocTime;
				maxMallocs = (mallocsDone >> 4) + 50;
                numChecks = 0;
				mallocsDone = 0;

				// if we've run over a second
			    timed();
            }
        }
        numMallocs++;
    }
    else {
        beginOrEnd = "end";
        logStart = true;
        writeLogEvent(LOG_TRY_MALLOC, beginOrEnd.c_str(), "TryMalloc", thisMallocSeqNumb, NULL);
        
        // write out the hares
        char custMsg[128];
        sprintf(custMsg, ",\"hare0\":%zu,\"hare1\":%zu,\"hare2\":%zu",hares[0],hares[1],hares[2]);
        hares[0] = hares[1] = hares[2] = 0;
        logPrint(LOG_CUSTOM,"hareSize", (char*)custMsg);
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

void logIgnoreExplicit() {
    writeLogEvent(LOG_IGNORE_EXPLICIT, "begin", "IgnoreExplicit", seqNumber++, NULL);
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
	u8 threadTime = dvmGetTotalThreadCpuTimeMsec();
    unsigned long count;
    
    // get the cpu we're running on
    // and it's associated count
    int cpu = sched_getcpu();
    //getcpu(&cpu, NULL, NULL);
    count = getCount(cpu);
    

    sprintf(output, "@%s%s{\"seqNum\":%d,\"cpu\":%d,\"count\":%lu,\"wcTime-ms\":%llu,\"appTime-ms\":%llu,\"threadTime-ms\":%llu,\"priority\":%d",
        beginEnd, eventName, seqNumber, cpu, count, wcTime, appTime, threadTime, os_getThreadPriorityFromSystem());
    return output;
}

/*
* builds a string of log info with heap stats
*/
void buildHeapEvent(const char* beginEnd,const char* eventName, int seqNumber, char output[])
{
    char partial[MAX_STRING_LENGTH];
    size_t heapsAlloc[2], heapsFootprint[2], heapsMax[2], numObjects[2], heapsLimit[2], heapsBase[2];
    float thresholdKb = threshold / 1024.0;
        heapsAlloc[1] = heapsFootprint[1] = heapsAlloc[0] = heapsFootprint[0] = heapsMax[0] = heapsMax[1] = 0;

    dvmHeapSourceGetValue(HS_BYTES_ALLOCATED, heapsAlloc, 2);
    dvmHeapSourceGetValue(HS_FOOTPRINT, heapsFootprint, 2);
    dvmHeapSourceGetValue(HS_ALLOWED_FOOTPRINT, heapsMax, 2);
    dvmHeapSourceGetValue(HS_OBJECTS_ALLOCATED, numObjects, 2);
	dvmHeapSourceGetValue(HS_VM_SIZE, heapsLimit, 2);
	dvmHeapSourceGetValue(HS_BASE, heapsBase, 2);
	baseAddr = heapsBase[0];

    if (spleenGC)
        heapsAlloc[0] -= oldSpleenSize;
    heapsAlloc[0] = heapsAlloc[0] / 1024;
    heapsFootprint[0] = heapsFootprint[0] / 1024;
    heapsMax[0] = heapsMax[0] / 1024;
	heapsLimit[0] = heapsLimit[0] / 1024;
	heapsBase[0] = heapsBase[0] / 1024;
    heapsAlloc[1] = heapsAlloc[1] / 1024;
    heapsFootprint[1] = heapsFootprint[1] / 1024;
    heapsMax[1] = heapsMax[1] / 1024;
	heapsLimit[1] = heapsLimit[1] / 1024;

    buildBasicEvent(beginEnd, eventName, seqNumber, partial);

    sprintf(output, "%s,\"currAlloc0-kB\":%d,\"currFootprint0-kB\":%d,\"currLimit0-kB\":%d,\"currMax0-kB\":%d,\"numObjects0\":%d,\"base0-kB\":%d,\"minAdd0-kB\":%d,\"maxAdd0-kB\":%d,\"arenaSize0-kB\":%d,"
                    "\"currAlloc1-kB\":%d,\"currFootprint1-kB\":%d,\"currLimit1-kB\":%d,\"currMax1-kB\":%d,\"numObjects1\":%d,\"threshold-kB\":%f,\"totalAlloced-kB\":%zu",
        partial, 
		heapsAlloc[0], 
		heapsFootprint[0], 
		heapsLimit[0],
		heapsMax[0], 
		numObjects[0], 
		heapsBase[0],
		minAdd / 1024,
		maxAdd / 1024,
		(maxAdd - minAdd) / 1024,
		heapsAlloc[1], 
		heapsFootprint[1], 
		heapsLimit[1],
		heapsMax[1], 
		numObjects[1], 
		thresholdKb,
		totalAlloced);
}

/*
* builds a string of heap stats with a GC type
*/
void buildGCEvent(const char* beginEnd,const char* eventName, int seqNumber, const GcSpec *spec, char output[])
{
    char partial[MAX_STRING_LENGTH];
    buildHeapEvent(beginEnd, eventName, seqNumber, partial);
    char cpuSpeed[] = "noData ";
    logCPUSpeed(cpuSpeed);

    sprintf(output, "%s,\"GCType\":\"%s\",\"cpuSpeed-Hz\":\"%s\",\"rootScanTime\":%llu,\"bytesFreed-kb\":%f,\"objectsFreed\":%u,\"numIterations\":%u,\"spleenSize-kb\":%zu",
        partial, spec->reason, cpuSpeed, rootScanTime, numBytesFreedLog / 1024.0, objsFreed, numIterations, (spleenSize / 1024));
}

/*
 * Writes a nonstandard log event with a custom message
 */ 
void logPrint(int logEventType, const char *eventName, const char *customString)
{
	char partialEntry[MAX_STRING_LENGTH];

	if (!fileLog)
		return;
	buildBasicEvent("", eventName, seqNumber++, partialEntry);
	fprintf(fileLog, "%s%s%s}\n", logPrefix, partialEntry, customString);
	return;
}

/*
* writes a log event to a file
*/
void writeLogEvent(int eventType,const char* beginEnd, const char* eventName, int seqNumber, const GcSpec *spec, bool mallocFail)
{
    char partialEntry[MAX_STRING_LENGTH];
    static int numLogEvents = 0;
    int GCSeqNumb = 0;
	#ifdef logWriteTime
	char before[256];
	char after[256];
	u8 beforeWrite;
	u8 afterWrite;
	#endif	
	
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
                if (lastState > 0)
                    GCSeqNumb = lastGCSeqNumber;
                sprintf(partialEntry, "%s,\"mallocFail\":\"%d\",\"numMallocs\":%d,\"lastAllocSize-B\":%d,\"lastEvent\":%d,\"lastGCSeq\":%d"
                        ,temp
                        ,(int)mallocFail
                        ,mallocsDone
                        ,lastAllocSize
                        ,lastState
                        ,GCSeqNumb);
                break;
        case LOG_GC:
                buildGCEvent(beginEnd, eventName, seqNumber, spec, partialEntry);
                break;
        case LOG_GC_SCHED:
        case LOG_WAIT_CONC_GC:
        case LOG_IGNORE_EXPLICIT:
                buildBasicEvent(beginEnd, eventName, seqNumber, partialEntry);
                break;
    }

	#ifdef logWriteTime
	getCPUStats(before);
	beforeWrite = dvmGetRTCTimeMsec();
	#endif
    fprintf(fileLog, "%s%s}\n", logPrefix, partialEntry);
	#ifdef snappyDebugging //tmp renable 
	fflush(fileLog);
	#endif
	#ifdef logWriteTime
	afterWrite = dvmGetRTCTimeMsec();
	getCPUStats(after);
	fprintf(fileLog, "@writeBench{\"startTime\":%llu,\"endTime\":%llu,\"cpuStatsBefore\":\"%s\",\"cpuStatsAfter\":\"%s\"}\n",
			beforeWrite,
			afterWrite,
			before,
			after);
	#endif
	
	numLogEvents++;
}

void scheduleConcurrentGC()
{
    #ifdef snappyDebugging
    ALOGD("Robust Schedule Concurrent");
    #endif
    // only adaptive policies schedule concurrent GC
    if (scheduleConcGC) {
        u8 timeSinceLastGC = dvmGetRTCTimeMsec() - lastGCTime;
		u8 timeSinceLastGCCPU = dvmGetTotalProcessCpuTimeMsec() - lastGCCPUTime;

        // check and see if we're at the min time from a concurrent GC
        if (((timeSinceLastGC > minGCTime) && !(policyType & MIN_CPU_INTERVAL)) || ((timeSinceLastGC > minGCTime) || (timeSinceLastGCCPU > (minGCTime / 2)))) {

            #ifdef snappyDebugging
            ALOGD("Robust Schedule Concurrent Min Time Complete");
            #endif
            
            // if we've hit the threshold schedule a concurrent GC
            size_t heapsAlloc[2], heapsFootprint[2];
            heapsAlloc[1] = heapsFootprint[1] = heapsAlloc[0] = heapsFootprint[0] = 0;

            dvmHeapSourceGetValue(HS_BYTES_ALLOCATED, heapsAlloc, 2);
            //dvmHeapSourceGetValue(HS_FOOTPRINT, heapsFootprint, 2);
            size_t arenaSize = maxAdd - baseAddr;

            if ((threshold >= (arenaSize - heapsAlloc[0])) || schedGC) {
                char custMsg[256];
                sprintf(custMsg,",\"arenaSize\":%zu,\"allocSize\":%zu,\"thresholdSize\":%zu,\"free\":%zu,\"schedGC\":%u"
		            ,arenaSize, heapsAlloc[0], (arenaSize - heapsAlloc[0]), threshold, schedGC);
                logPrint(LOG_CUSTOM,"debugGCSched", (char*)custMsg);
                logPrint(LOG_GC_SCHED);
                schedGC = false;
                // to prevent succesive calls from succeding
                // if gc doesn't complete soon
                lastGCTime = dvmGetRTCTimeMsec();
			    lastGCCPUTime = dvmGetTotalProcessCpuTimeMsec();
                dvmInitConcGC();
            }
        }
    }
    #ifdef snappyDebugging
    ALOGD("Robust Schedule Concurrent Complete");
    #endif
}

/*
 * Saves free memory history
 */

void saveHistory()
{
    #ifdef snappyDebuggingHistory
    ALOGD("Robust Saving History");
    #endif 

    // Save history every tenth second or so
    // make sure we don't do time calls to 
    // often otherwise sys slows down.
    static int timesCalled = 0; // # of times called
    static int callsThreshold = 500; // threshold before we check time
	static int totalCalled = 0;
    static u8 lastSaveTime = 0;
	strcpy(fileName,"./");
    u8 currentTime;
    timesCalled++;
    
    if (timesCalled > callsThreshold) {
        // check if its been 1/10th of a second
        currentTime = dvmGetRTCTimeMsec();

		// set the call threshold to 1/8 of what happened  + 50
        //callsThreshold = ((timesCalled >> 4) * (100.0 / ((float)currentTime - (float)lastSaveTime) )) + 10.0;           
        timesCalled = 0;
		totalCalled += timesCalled;
        if ((currentTime - lastSaveTime) > 100) {
        
            #ifdef snappyDebuggingHistory
            ALOGD("Robust Saving History Min Time Reached");
            #endif
            // and check to see if we should run a concurrent GC
            // we do this before otherwise we only go back .4 sec
            scheduleConcurrentGC();
            
            // save the current free space
            size_t heapsAlloc[2], heapsFootprint[2], heapsArenas[2];
            int tmp;
            heapsAlloc[1] = heapsFootprint[1] = heapsAlloc[0] = heapsFootprint[0] = 0;

            dvmHeapSourceGetValue(HS_BYTES_ALLOCATED, heapsAlloc, 2);
            //dvmHeapSourceGetValue(HS_FOOTPRINT, heapsFootprint, 2);
            heapsArenas[0] = maxAdd - minAdd;
            tmp = heapsArenas[0] - heapsAlloc[0];
            if (tmp < 0) {
                tmp = tmp * -1;
            }
            freeHistory[currInterval] = tmp;
            currInterval = (currInterval + 1) % intervals;
            
            // set this time as the last save time
            lastSaveTime = currentTime;
			callsThreshold = (totalCalled >> 4) + 50;
			totalCalled = 0;
        }
   }
   #ifdef snappyDebuggingHistory
   ALOGD("Robust Saving History Complete");
   #endif
}

void setThreshold(void)
{
    #ifdef snappyDebugging
    ALOGD("Robust Setting Threshold");
    #endif 
    // originally was only set once now we set each time
    threshold = freeHistory[(currInterval + (intervals + 1)) % intervals];
    threshSet = true;
    #ifdef snappyDebugging
    ALOGD("Robust Setting Threshold Complete");
    #endif 
}

/*
 * store the current gc completion time
 */
void storeGCTime(u8 time)
{
	gcAverage[currGCAverage] = time;
	currGCAverage = (currGCAverage + 1) % NUM_GC_AVERAGES;
}

/*
 * Computes the average of the last numIterations GCs
 */
u8 getGCTimeAverage(int numIterations)
{
	int i = 0;
	int curr = currGCAverage;
	u8 sum = 1;
	u8 currTime;
	for (i = 0; i < numIterations; i++) {
		currTime = gcAverage[curr];
		if (currTime > 0) {		
			sum += gcAverage[curr];
		}
		curr = (curr + 1) % NUM_GC_AVERAGES;
	}
	return sum / numIterations;
}

/*
 * Adjusts Threshold for ### policies
 */
void adjustThreshold()
{
	// make sure vars have been set up first
	if (!logReady || !(policyType & THROTTLE_THRESHOLD))
		return;
	// get freespace 500ms ago
	size_t free500msAgo = freeHistory[(currInterval + (intervals + 1)) % intervals];

	// if gc completed early bump up the
	// threshold so an additional 20ms
	// should elapse before gc runs

	char *custMsg;
	custMsg = (char *)malloc(512);
	size_t heapsLimit[2], heapsBase[2];
	dvmHeapSourceGetValue(HS_LIMIT, heapsLimit, 2);
	dvmHeapSourceGetValue(HS_BASE, heapsBase, 2);
	sprintf(custMsg,",\"base\":%d,\"limit\":%d,\"lastGCTime\":%llu,\"lastExhaustion\":%llu,\"free500msAgo\":%d,\"timeToAdd\":%d,\"firstExhaust\":\"%s\",\"freeHistory\":%d|%d|%d|%d|%d"
			,heapsBase[0]
			,heapsLimit[0]			
			,lastGCTime
			,lastExhaustion
			,free500msAgo
			,timeToAdd
			,(firstExhaustSinceGC ? "true" : "false")
			,freeHistory[0],freeHistory[1],freeHistory[2],freeHistory[3],freeHistory[4]);
	logPrint(LOG_CUSTOM,"debugLogAdjustNumbs", (char*)custMsg);
	free(custMsg);

	// if there hasn't been an exhaustion since GC
	// then we shrink the threshold
	// otherwise if we did exhaust just go back 500ms
	if (firstExhaustSinceGC) {
		// in the event we want to shrink the heap to a size
		// less than zero set it to something reasonably small
		size_t temp = ((free500msAgo * timeToAdd)/500.0);
		if (temp >= threshold)
			threshold = 100;
		else
			threshold -= temp;
	} else {
		setThreshold();
	}
	// just in case
	if (threshold == 0) {
		threshold = CONCURRENT_START_DEFAULT;
	}
}
		
    
/*
 * Initializes the robust logfile
 */
void _initLogFile()
{
    //GcPolSpec policy = policies;
    
    initLogDone = 0;
    skipLogging = 0;
    dlmStats = 0;

    /* Get process name */
    const char* processName = get_process_name();
    pid_t pid = 1111;// getpid();
    strcpy(thisProcessName, processName);
    
    // set initial values        
    logReady = false;
    seqNumber = 1;
    threshold = (128 << 10);
    threshSet = false;
    schedGC = false;
	firstExhaustSinceGC = true;
	lastExhaustion = 0;
    inGC = 0;   
	thresholdOnGC = 0;
	minSleepTime.tv_sec = 0;
	minSleepTime.tv_nsec = 1000000L; // 1ms
	currIterations = 0;
    maxAdd = 0;
	minAdd = (size_t) - 1;
 
    // defaults
    // MI2A and no logging
    int polNumb = DEFAULT_POLICY;

    // if we're a blacklisted process
    // skip logging
	// Original code that skips certain processes
	#ifdef dontLogAll
    if (!strncmp(processName, "zygote",6)) {
        // zygote is where everyone's spawned so we can't turn off logging here
        #ifdef snappyDebugging
        ALOGD("Robust skipping process %s",processName);
        #endif 
        // se if we can open file here
        if (fileTest != NULL) 
            return; //file previously opened
        //testTime();
        fileTest = fopen("/sdcard/robust/testFile.txt", "at" );
        notLogged = fopen("/sdcard/robust/notLogged.txt", "at" );
        
        //still in zygote so we have root access, enable cpu perf_events
        FILE *proc;
        proc = fopen("/proc/sys/kernel/perf_event_paranoid", "wt");
        if (proc)
            fprintf(proc,"-1");
        return;
    } 
    if (!strncmp(processName, "dexopt", 6)
            || !strncmp(processName, "system_server", 6) || !strncmp(processName, "unknown", 6)
            || !strncmp(processName, "<pre", 4)) {
        //#ifdef snappyDebugging
        ALOGD("Robust skipping process %s",processName);
        //#endif 
        // we're a valid process so set
        // defaults, we may not be a 
        // full process yet so wait a bit
        // for full init
        skipLogging = 1;
        setPolicy(polNumb);
        return;
    }
	#else
	// check if we're some sort of preinitialization process
	if (!strncmp(processName, "zygote",6) || !strncmp(processName, "system_server", 6) || !strncmp(processName, "unknown", 6)
            || !strncmp(processName, "<pre", 4) || !strncmp(processName, "dexopt", 6)) {

		// if we are and we've already been initialized bail out
		if (preinit && preDone) {
			return;
		}

		notLogged = fopen("/sdcard/robust/notLogged.txt", "at" );
		others = fopen("/sdcard/robust/others.txt", "at" );
        if (notLogged != NULL) {
            ALOGD("Robust Test file open successful");
			// set the buffer for others to something fairly large
			// since it's shared across processes
			setvbuf(fileLog, NULL, _IOFBF, 40960);
        } else {
            ALOGD("Robust Log Test fail open /sdcard/robust/notLogged.txt %s",strerror(errno));
        }
		preinit = 1;
	} else {
		preinit = 0; // we're a regular process
	}
	#endif

    //#ifdef snappyDebugging
    ALOGD("Robust Log %s != zygote not skipping", processName);
    //#endif

    // we're a valid process so set
    // initialization as complete
    initLogDone = 1;
    
    pid = getpid();    
    
    // get device name
    getDeviceName();

    /* Get start time so we can identify seperate runs */
    clock_gettime(CLOCK_REALTIME, &startTime);
    time_t mytime;
    mytime = time(NULL);
    char *timeStart = ctime(&mytime);        
    removeNewLines(timeStart);        

    char polFile[] = "/sdcard/robust/GCPolicy.txt";
    char polVal[5];
    char uniqName[64];
	char enableDump[] = "       false";

    // check and see if we have GC policy
    // file to read the policy from
    FILE *fdPol = fopen(polFile, "rt");
    //#ifdef snappyDebugging
    ALOGD("Robust Log attempting to open Policy file %s", polFile);
    //#endif
    if (fdPol != NULL) {
        #ifdef snappyDebugging
        ALOGD("Robust Log policy file open");
        #endif
        fscanf(fdPol, "%s", polVal);
        fscanf(fdPol, "%s", uniqName);
		fscanf(fdPol, "%s", enableDump);
        fclose(fdPol);
    } else {
        // defaults
        // MI2A and no logging
        // yes it's duplicated but the compiler 
        // misses the fact that it's above
        polNumb = DEFAULT_POLICY;
        skipLogging = 1;
        dlmStats = 0;
        setPolicy(polNumb);
        logReady = true;
        return; // GC Policy file doesn't exist so we just run defaults anyway
    }
    
    // defaults
    // MI2A and no logging
    // yes it's duplicated but the compiler 
    // misses the fact that it's above
    skipLogging = 1;
    
    if (polVal[0]) {
         polNumb = atoi(polVal);
         #ifdef snappyDebugging
         ALOGD("Robust Log Policy Number %d", polNumb);
         #endif
         // check if logging should be skipped
         if (polNumb > 0) {
            skipLogging = 0;
         }
         if (polNumb < 0) {
             skipLogging = 1;
             polNumb = polNumb * -1;
             //#ifdef snappyDebugging
             ALOGD("Robust Log Skipping GC/Malloc logging using policy %d skipLogging %d", polNumb, skipLogging);
             //#endif
         }
         if ((polNumb > 0) && (polNumb <= NUM_POLICIES)) {
            polNumb = polNumb;
         } else {
			polNumb = DEFAULT_POLICY;
		}
		ALOGD("Robust Log enable dlmStats %s",enableDump);
        dlmStats = atoi(enableDump);
		if (dlmStats) {
			ALOGD("Robust Log Enabling dlmStats");
		}
    }
        
    // set up the policy we'll be executing
    // read the numbers from the list we have stored
    setPolicy(polNumb);
    ALOGD("Robust Log policy Number %d", policyNumber);
    ALOGD("MinGCTime %d", minGCTime);
    
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
    strcpy(fileName, baseDir);
    strcat(fileName, processName);
    strcat(fileName, ".txt");

	char memDup[100];
	char memDir[] = "/sdcard/robust_data/";
	strcpy(memDup, memDir);
    strcat(memDup, processName);
    strcat(memDup, ".dxt");
    
    // create the directory for log files
    mkdir("/sdcard/robust", S_IRWXU | S_IRWXG | S_IRWXO);

	// create the directory for data files
    mkdir("/sdcard/robust_data", S_IRWXU | S_IRWXG | S_IRWXO);

    #ifdef snappyDebugging
    ALOGD("Robust Log ||%s||", fileName);
	ALOGD("Filelog %p",fileLog);
    #endif
    fileLog = fopen(fileName, "at" );
	memDumpFile = fopen(memDup, "at" );
	

	logPrefix[0] = '\0';
	#ifdef logOthers
	// if we couldn't open the log for whatever reason
	// print logs in the others catchall log
	if (!fileLog) {
		fileLog = others;
		sprintf(logPrefix, "%s.txt#%llu",processName,dvmGetRTCTimeMsec());
	}
	#endif
    if (fileLog != NULL) {
        // bump our buffer log size to 12k
        setvbuf(fileLog, NULL, _IOFBF, 12287);

		// catchall log needs to be a bit bigger to 
		// cover all procs
		if (fileLog == others)
			setvbuf(fileLog, NULL, _IOFBF, 40000);

        fprintf(fileLog, "\n\n%s@header{\"deviceName\":\"%s\",\"deviceID\":\"%s\",\"process\":\"%s\",\"pid\":%d,\"policy\":\"%d\",\"appStartTime-ms\":%llu,\"startTime\":\"%s\",\"timerResolution-ns\":%llu,\"Build_ID\":\""BUILD_ID"\"}\n",
            logPrefix, deviceName, uniqName, processName, pid, policyNumber,dvmGetRTCTimeMsec(), timeStart,diff2);
		if (policyType & THROTTLE_THRESHOLD)
			readThreshold();
		ALOGD("Robust log reading threshold %d",threshold);
        logReady = true;
        inZygote = false; // out of zygote mode
		preDone = 1; // we're a preinitialization process and we should be done here
		ALOGD("Robust Log opened file %s", fileName);
        return;
    } else {
        // all else failed
        fprintf(notLogged, "%s %d\n",processName, policyNumber);
        fflush(notLogged);
        ALOGD("Robust Log fail open %s %s", fileName,strerror(errno));
        logReady = false;
		inZygote = false;
		preDone = 1; // inits should never reach here but just in case
        return;
    }
}

void logMeInit()
{
    initLogFile();
}

void logCPUSpeed(char* speed)
{
    char cpuFileName[] = "/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_cur_freq";
    static bool errorLogged = false;
    FILE* cpuFile = fopen(cpuFileName, "rt" );

    if (cpuFile && fscanf(cpuFile, "%s", speed)) {
        fclose(cpuFile);
    }
    else {
        speed = (char *)"00000\0";
        if (!errorLogged) {
            ALOGD("Robust Log Failed to open CPU speed file: %s",strerror(errno));
            errorLogged = true;
        }
    }
    
}

int continousGC(const GcSpec* spec)
{
    char baseDir[] = "/sdcard/robust/";
    char gcfileName[128];
    strcpy(gcfileName, baseDir);
    strcat(gcfileName, thisProcessName);
    strcat(gcfileName, ".gc");
    
    // check and see if we can open
    // processName.gc if so return 1
    // else return 0
    FILE *gcFile = fopen(gcfileName, "rt");
    while (gcFile && !inGC) {
        inGC = 1;
        fclose(gcFile);
        dvmCollectGarbageInternal(spec);
        gcFile = fopen(gcfileName, "rt");
    }
    inGC = 0;
    return 0;
}

/*
 * Sets constraints to the appropriate policies
 */
void setPolicy(int policyNumb)
{
    lastGCSeqNumber = 0;
    spleen = NULL;
    spleenSize = 0;
    totalAlloced = 0;
    spleenGC = false;
    // set max grow size to 7/8s max heap size
    maxGrowSize = dvmHeapSourceGetMaximumSize() * (7.0 / 8.0);
    ALOGD("Robust Log max grow size = %u",maxGrowSize);
	policy = policies[policyNumb - 1];

	policyNumber = policy.policyNumber;
	policyType = policy.type;
	MIS = policy.MIS;
    minGCTime = policy.minTime;
    intervals = policy.intervals;
	adaptive = policy.adaptive;
	scheduleConcGC = policy.schedulesGC;
	spleenPolicy = policy.spleen;
	resizeThreshold = policy.resizeThreshold;
	resizeOnGC = policy.resizeOnGC;	
	timeToAdd = policy.timeToAdd;
	timeToWait = policy.timeToWait;
	numIterations = policy.numIterations;
	partialAlpha = policy.partialAlpha;
	fullAlpha = policy.fullAlpha;
	beta = policy.beta;
	
}

/*
 * recomputes full/partial iteration ratio
 */
void computePartialFull(void)
{
	if (avgPercFreedFull > 0)
		numIterations = beta * numIterations * avgPercFreedPartial / avgPercFreedFull;
	else
		numIterations = 100;
}

/*
 * save pointer address, and size
 */
void savePtr(void *ptr, size_t size)
{
	size_t tmp = (((size_t)(ptr)) - 0);
	if (tmp < minAdd)
		minAdd = tmp;
	if (tmp + size > maxAdd)
		maxAdd = tmp + size;
}

void memDumpHandler(void* start, void* end, size_t used_bytes,
                                void* arg)
{
	static int count = 0;
	count ++;
	return;
	if (memDumpFile) {
		ALOGD("Heap %p %p %u\n", start, end, used_bytes);
		fprintf(memDumpFile,"%p %p %u\n", start, end, used_bytes);
		if (count > 1000)
			fflush(memDumpFile);
	}			
}

/*
 * writes current GC threshold to file
 */ 
void writeThreshold(void) {
	char threshFileName[128];
	char baseDir[] = "/sdcard/robust_data/";

	//mkdir("/sdcard/robust_data", 0777);
    strcpy(threshFileName, baseDir);
    strcat(threshFileName, thisProcessName);
	strcat(threshFileName, "_thresh\0");
    strcat(threshFileName, ".txt");
	
	FILE *tFile = fopen(threshFileName, "wt");
	
	if (tFile) {
		fprintf(tFile,"%d", threshold);
		fclose(tFile);
	}
}

void readThreshold(void) {
	char threshFileName[128];
	char baseDir[] = "/sdcard/robust_data/";
    strcpy(threshFileName, baseDir);
    strcat(threshFileName, thisProcessName);
	strcat(threshFileName, "_thresh\0");
    strcat(threshFileName, ".txt");
	
	FILE *tFile = fopen(threshFileName, "rt");
	
	if (tFile) {
		if (!fscanf(tFile,"%d", &threshold)) {
			threshold = (128 << 10);
		}
		fclose(tFile);
	}
	// check to avoid bad thresholds from data corruption
	// if the threshold is too large or small set it to default
	// Min is 100 bytes Max is 10 meg
	ALOGD("Robust Log Threshold before check %d", threshold);
	if ((threshold < 100) || (threshold > (1024 * 1024 * 10))) {
	    threshold = CONCURRENT_START_DEFAULT;
	}
	ALOGD("Robust Log Threshold after check %d", threshold);
}

void timed(void) 
{
	static int seconds = 0;
	static int tenths = 0;
	static size_t currTick = 0;

    currTick++;	
	// get alloc rate over last 200ish ms
	size_t allocDelta = allocHistory[currAllocHistory] - allocHistory[(currAllocHistory + 1) % 3];
	
	// save current alloc history and increment count
	currAllocHistory = (currAllocHistory + 1) % 3;
	allocHistory[currAllocHistory] = totalAlloced;
	
	// wait 15 seconds to avoid startup behaviour
	if (currTick > 150) {
	    // if our delta is bigger than the current spleen size, change the spleen size
	    if (spleenSize < allocDelta) {
	        size_t heapsFootprint[2];
	        dvmHeapSourceGetValue(HS_FOOTPRINT, heapsFootprint, 2);
	        if (allocDelta < (heapsFootprint[0] * .1)) {
	            spleenSize = allocDelta;
	        } else {
	            spleenSize = heapsFootprint[0] * .1;
	        }
	    }
	
	    //ALOGD("Robust Log Spleen Size after adjust %zu, Delta %zu", spleenSize, allocDelta);
	}
	
	
	if (tenths == 10) {
	    // stuff that needs to happen every second goes here
	    if (seconds == 10) {
	        // stuff that needs to happen every 10 seconds goes here
		    writeThreshold();
		    seconds = 0;
	    }
	    seconds++;
	    tenths = 0;
    }
    tenths++;
}
/***************************************************
 * Utility functions below
 * These are just utility functions need for logging
 * and timekeeping. Some used to be in Misc.cpp but
 * were moved out to make patching new versions of
 * CM easier. They could be moved back eventually
 ***************************************************/

// get time from RTC
// could shift slightly due to time corrections
// but shouldn't affect us much
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
        // get rid of colons
        if (input[i] == ':') {
            input[i] = '-';
        }
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

/*
 * Get the per-process CPU time, in nanoseconds
 *
 * The amount of time the process had been executing
 */
u8 dvmGetTotalProcessCpuTimeNsec(void)
{
#ifdef HAVE_POSIX_CLOCKS
    struct timespec now;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &now);
    return (u8)now.tv_sec*1000000000LL + now.tv_nsec;
#else
    return (u8) -1;
#endif
}

/*
 * Get the per-thread CPU time, in nanoseconds
 *
 * The amount of time the thread had been executing
 */
u8 dvmGetTotalThreadCpuTimeNsec(void)
{
#ifdef HAVE_POSIX_CLOCKS
    struct timespec now;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now);
    return (u8)now.tv_sec*1000000000LL + now.tv_nsec;
#else
    return (u8) -1;
#endif
}
    
/*
 * get the device name for the log
 */
void getDeviceName() 
{
    #ifdef snappyDebugging
    ALOGD("Robust Log Get Device Name");
    #endif
    // get deviceName
    FILE *devName = fopen("/system/build.prop", "rt");
    char buffer[75] = "NotFound"; 
    if (devName != NULL) {
        while (fscanf(devName, "%s", buffer) != EOF) {
            #ifdef snappyDebugging
            ALOGD("Robust Log buffer %s\n", buffer);
            #endif
            if (strncmp(buffer, "ro.product.device",17) == 0) {
                memcpy(deviceName, buffer + 18, strlen(buffer) - 17);
                break;   
            }
        }
        fclose(devName);
    }
    else {
        ALOGD("Snappy Log failed to open build.prop!");
    }
    //#ifdef snappyDebugging
    ALOGD("Robust Log Get Device Name Done");
    //#endif
}

/*
 * Get the current count on this cpu if available
 */
unsigned long getCount(int cpu)
{
    FILE *countFile = fopen("/proc/SnappyCount", "rt");
    char buffer[24];
    int i;
    if (countFile) {
        // move to the correct count value for multi cpu
        // systems
        for (i = -1; i < cpu; i++) {
            fscanf(countFile, "%s",buffer);
        }
        fclose(countFile);
        return atol(buffer);
    } else {
        return 0;
    }
}

/*
 * Gets current cpu stats as string
 */
void getCPUStats(char *output) 
{
	static FILE* loadavgFile;
	static FILE* cpuStats;
	char buff[12];
	long double a[10], b[10], c[10];
	long double load[4];

	cpuStats = fopen("/proc/stat","r");
	loadavgFile = fopen("/proc/loadavg","r");
	if (cpuStats) {
		fscanf(cpuStats,"%*s %Lf %Lf %Lf %Lf %Lf %Lf %Lf %Lf %Lf %Lf",&a[0],&a[1],&a[2],&a[3],&a[4],&a[5],&a[6],&a[7],&a[8],&a[9]);
		fscanf(cpuStats,"%*s %Lf %Lf %Lf %Lf %Lf %Lf %Lf %Lf %Lf %Lf",&b[0],&b[1],&b[2],&b[3],&b[4],&b[5],&b[6],&b[7],&b[8],&b[9]);
		fscanf(cpuStats,"%*s %Lf %Lf %Lf %Lf %Lf %Lf %Lf %Lf %Lf %Lf",&c[0],&c[1],&c[2],&c[3],&c[4],&c[5],&c[6],&c[7],&c[8],&c[9]);
		fclose(cpuStats);
	}
	if (loadavgFile) {
		fscanf(loadavgFile,"%Lf %Lf %Lf %s", &load[0], &load[1], &load[2], buff);
		fclose(loadavgFile);
	}
	sprintf(output,"cpu %Lf %Lf %Lf %Lf cpu0 %Lf %Lf %Lf %Lf cpu1 %Lf %Lf %Lf %Lf loadavg %Lf %Lf %Lf %d"
			,a[0],a[1],a[2],a[3]
			,b[0],b[1],b[2],b[3]
			,c[0],c[1],c[2],c[3]
			, load[0], load[1], load[2], buff[0] - 48);
}

// internal test function remove before release
int testTime(void)
{
    struct timespec start, stop;
    //int i = 0;
    //unsigned int j = 0;
    //int k = 0;
    FILE *outfile;
    long shortTime;
    long longTime;
    char string[] = "This is a string";
    
    int counter = 0;
    
    outfile = fopen("/sdcard/robust/printfile.txt","at");
    
    if (!outfile) {
        //printf("aborting file not opened");
        return(0);
    }
    
    clock_gettime(CLOCK_REALTIME, &start);
    for (counter = 0; counter < 1000; counter++) {
        fprintf(outfile, "%s\n",string);
    }
    sleep(5);
    for (counter = 0; counter < 1000; counter++) {
        fprintf(outfile, "%s\n",string);
    }
    clock_gettime(CLOCK_REALTIME, &stop);
    shortTime = (stop.tv_sec-start.tv_sec)*1000000000LL + (stop.tv_nsec-start.tv_nsec);
    
    fclose(outfile);
    outfile = fopen("/sdcard/robust/printfile2.txt","at");
    if (!outfile) {
        //printf("aborting file2 not opened");
        return(0);
    }
    
    clock_gettime(CLOCK_REALTIME, &start);
    for (counter = 0; counter < 1000; counter++) {
        fprintf(outfile, "%s\n",string);
    }
    sleep(5);
    for (counter = 0; counter < 1000; counter++) {
        fprintf(outfile, "%s\n",string);
    }
    clock_gettime(CLOCK_REALTIME, &stop);
    longTime = (stop.tv_sec-start.tv_sec)*1000000000LL + (stop.tv_nsec-start.tv_nsec);
    
    fclose(outfile);
    outfile = fopen("/sdcard/robust/results.txt","at");
    if (outfile) {
        fprintf(outfile, "times %li %li\n",shortTime, longTime);
    }
    fclose(outfile);
    return(0);
}

FILE* mallocLogFile = NULL;
bool ifProcessInit = false;
int testMethod(char *logMessage)
{
	u8 wcTime = dvmGetRTCTimeMsec();
	u8 appTime = dvmGetTotalProcessCpuTimeMsec();
	int mSuccess = -1;
	
	if (!ifProcessInit) {
		const char* processName = get_process_name();
		char myProcessName[128];
		strcpy(myProcessName, processName);
		char baseDir[] = "/sdcard/robust/";
		char mFileName[128];
		strcpy(mFileName, baseDir);
		strcat(mFileName, myProcessName);
		strcat(mFileName, ".dlm");
		mallocLogFile = fopen(mFileName, "at");
		ifProcessInit = true;
		
		if (mallocLogFile) {
			fprintf(mallocLogFile, "Begin logging:\nwcTime-ms:%llu\nappTime-ms:%llu\n%s\n",
					wcTime, appTime, logMessage);
			fflush(mallocLogFile);
			mSuccess = 1;
		} else {
			ALOGD("dlmstats_init %s can't open file %s\n", myProcessName, strerror(errno));
			mSuccess = 0;
		}
		logMessage[0] = '\0';
	} else {
		if (mallocLogFile) {
			fprintf(mallocLogFile, "wcTime-ms:%llu\nappTime-ms:%llu\n%s\n",
					wcTime, appTime, logMessage);
			fflush(mallocLogFile);
			mSuccess = 1;
		} else {
			ALOGD("dlmstats %s can't open file %s\n", get_process_name(), strerror(errno));
			mSuccess = 0;
		}
		logMessage[0] = '\0';
	}
	return mSuccess;
}

int dlmStats = 0;
int skipLogging = 0;
size_t thresholdOnGC = 0;
int seqNumber;
int policyType;
bool logReady;
bool schedGC; // Sched GC
bool resizeOnGC;
bool firstExhaustSinceGC;
bool spleenGC;
bool MIS;
bool spleenPolicy;
bool scheduleConcGC;
FILE* fileLog;
//string policyName;
int policyNumber;
unsigned int minGCTime;
unsigned int intervals;
unsigned int adaptive;
unsigned int resizeThreshold;
unsigned short timeToWait;
unsigned short numIterations;
unsigned short currIterations;
int lastState;
float partialAlpha;
float fullAlpha;
float beta;
float avgPercFreedPartial;
float avgPercFreedFull;
size_t numBytesFreedLog;
size_t objsFreed;
size_t lastAllocSize;
size_t minAdd;
size_t maxAdd;
size_t maxGrowSize;
bool inZygote = true;
int lastGCSeqNumber;
void* spleen;
size_t spleenSize;
size_t oldSpleenSize;
size_t currSpleenSize;
size_t totalAlloced;


size_t lastRequestedSize = 0;
//string processName;
int freeHistory[10] = 
	{CONCURRENT_START_DEFAULT,
	 CONCURRENT_START_DEFAULT,
	 CONCURRENT_START_DEFAULT,
	 CONCURRENT_START_DEFAULT,
	 CONCURRENT_START_DEFAULT}; // histogram
size_t threshold; // threshold for starting concurrent GC
size_t freeExhaust;
u8 lastGCTime = 0; // for scheduling GCs
u8 lastGCCPUTime = 0;

