#ifndef PERF_COUNTS_H_
#define PERF_COUNTS_H_

/*
 * This handles the interface to the linux
 * perf_events system. It setups the PMU
 * event counters, starts them, resets them,
 * and reads them.
 */ 
#include <linux/types.h>
//#include "../../../kernel/samsung/tuna/include/linux/perf_event.h"
#include <asm/unistd.h>
#include <unistd.h>
#include <inttypes.h>
 
// definitions that are arch dependent

#define ARCH "ARM_Cortex_A9"
#define MAX_EVENTS 7

// these are the locations of the counters
// perf_events can't be trusted since some 
// of the counters in perf_events read the 
// wrong thing
#define CYCLE_COUNT 0x11
#define INSTR_COUNT 0x68
#define ICACHE_STALL 0x60
#define DCACHE_STALL 0x61
#define TLB_STALL 0x62
#define ISSUE_EMPTY 0x67
#define NO_DISPATCH 0x66


//// variable for the events
//extern struct perf_event_attr event[MAX_EVENTS];
//extern struct perf_event_attr cycleCount;
//extern long long count[MAX_EVENTS];
//extern int fd[MAX_EVENTS];
//extern int cycleCountFD;
 
/*
 * setup the event counters
 */
void setupCounters(void);

/* 
 * if the counters are ready
 */
int countersReady(void);
/*
 * reset the event counters
 */
void resetCounters(void);

/*
 * enable the event counters
 */
void enableCounters(void);

/*
 * disable the event counters
 */
void disableCounters(void);

 /*
  * close the event counters
  */
 void closeCounters(void);
 
/*
 * reads the values from the counters into array result
 */ 
void readCounters(uint64_t results[]);
 
#endif // PERF_COUNTS
