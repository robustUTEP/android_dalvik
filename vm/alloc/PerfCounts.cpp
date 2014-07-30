#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <asm/unistd.h>
#include <sys/syscall.h> 
#include "../../../bionic/libc/kernel/common/linux/perf_event.h"
#include "alloc/PerfCounts.h"
#include "Dalvik.h"

// need this to actual access the perf event call
long perf_event_open( struct perf_event_attr *hw_event, pid_t pid,
                  int cpu, int group_fd, unsigned long flags )
{
    int ret;

    ret = syscall( __NR_perf_event_open, hw_event, pid, cpu,
               group_fd, flags );
    return ret;
}


// variable for the events
struct perf_event_attr event[MAX_EVENTS], e1;
struct perf_event_attr cycleCount;
int fd[MAX_EVENTS];
int cycleCountFD;
static bool setupCalled = false;

 /*
  * setup the event counters
  */
void setupCounters(void)
{
    // zero everything out
    cycleCountFD = fd[0] = fd[1] = fd[2] = fd[3] = fd[4] = fd[5] = fd[6] =0;
    
    // setup cycle counter
    memset(&cycleCount, 0, sizeof(struct perf_event_attr));
    cycleCount.type = PERF_TYPE_HARDWARE;
    cycleCount.size = sizeof(struct perf_event_attr);
    cycleCount.config = PERF_COUNT_HW_CPU_CYCLES;
    cycleCount.disabled = 1;
    cycleCount.exclude_user = 0;
    cycleCount.exclude_kernel = 0;
    cycleCount.exclude_idle = 0;
    cycleCount.exclude_hv = 0;
    
    cycleCountFD = perf_event_open(&cycleCount, 0, -1, -1, 0);
    if (cycleCountFD < 1)
        ALOGD("Robust Log perfCount cycle result %d %s",errno, strerror(errno));
    
    // instructions executed
    memset(&event[0], 0, sizeof(struct perf_event_attr));
    event[0].type = PERF_TYPE_HARDWARE;
    event[0].size = sizeof(struct perf_event_attr);
    event[0].config = PERF_COUNT_HW_INSTRUCTIONS;
    event[0].disabled = 1;
    event[0].exclude_user = 0;
    event[0].exclude_kernel = 0;
    event[0].exclude_idle = 0;
    event[0].exclude_hv = 0;
    
    fd[0] = perf_event_open(&event[0], 0, -1, -1, 0);    
    if (fd[0] < 1)  
        ALOGD("Robust Log perfCount instructions result %d %s",errno, strerror(errno));
    
    // tlb stalls
    memset(&event[1], 0, sizeof(struct perf_event_attr));
    event[1].type =  PERF_TYPE_RAW;
    event[1].size = sizeof(struct perf_event_attr);
    event[1].config = TLB_STALL;
    event[1].disabled = 1;
    event[1].exclude_user = 0;
    event[1].exclude_kernel = 0;
    event[1].exclude_idle = 0;
    event[1].exclude_hv = 0;
    
    fd[1] = perf_event_open(&event[1], 0, -1, -1, 0);
    if (fd[1] < 1)  
        ALOGD("Robust Log perfCount tlb result %d %s",errno, strerror(errno));
    
    //  i cache stalls (can be filled from l2 or main memory)
    memset(&event[2], 0, sizeof(struct perf_event_attr));
    event[2].type = PERF_TYPE_RAW;
    event[2].size = sizeof(struct perf_event_attr);
    event[2].config = ICACHE_STALL;
    event[2].disabled = 1;
    event[2].exclude_user = 0;
    event[2].exclude_kernel = 0;
    event[2].exclude_idle = 0;
    event[2].exclude_hv = 0;
    
    fd[2] = perf_event_open(&event[2], 0, -1, -1, 0);
    if (fd[2] < 1)  
        ALOGD("Robust Log perfCount Icache result %d %s",errno, strerror(errno));
    
    //  d cache stalls (can be filled from l2 or main memory)
    memset(&event[3], 0, sizeof(struct perf_event_attr));
    event[3].type = PERF_TYPE_RAW;
    event[3].size = sizeof(struct perf_event_attr);
    event[3].config = DCACHE_STALL;
    event[3].disabled = 1;
    event[3].exclude_user = 0;
    event[3].exclude_kernel = 0;
    event[3].exclude_idle = 0;
    event[3].exclude_hv = 0;
    
    fd[3] = perf_event_open(&event[3], 0, -1, -1, 0);
    if (fd[3] < 1)   
        ALOGD("Robust Log perfCount Dcache result %d %s",errno, strerror(errno)); 
        
    //  # if cycles issue stage is empty
    memset(&event[4], 0, sizeof(struct perf_event_attr));
    event[4].type = PERF_TYPE_RAW;
    event[4].size = sizeof(struct perf_event_attr);
    event[4].config = ISSUE_EMPTY;
    event[4].disabled = 1;
    event[4].exclude_user = 0;
    event[4].exclude_kernel = 0;
    event[4].exclude_idle = 0;
    event[4].exclude_hv = 0;
    
    fd[4] = perf_event_open(&event[4], 0, -1, -1, 0);
    if (fd[4] < 1)   
        ALOGD("Robust Log perfCount Dcache result %d %s",errno, strerror(errno)); 
        
    //  # of cycles instructions aren't dispatched
    memset(&event[5], 0, sizeof(struct perf_event_attr));
    event[5].type = PERF_TYPE_RAW;
    event[5].size = sizeof(struct perf_event_attr);
    event[5].config = NO_DISPATCH;
    event[5].disabled = 1;
    event[5].exclude_user = 0;
    event[5].exclude_kernel = 0;
    event[5].exclude_idle = 0;
    event[5].exclude_hv = 0;
    
    fd[5] = perf_event_open(&event[5], 0, -1, -1, 0);
    if (fd[5] < 1)   
        ALOGD("Robust Log perfCount Dcache result %d %s",errno, strerror(errno)); 
        
    // # of page faults, sw not hw event
    memset(&event[6], 0, sizeof(struct perf_event_attr));
    event[6].type =  PERF_TYPE_SOFTWARE;
    event[6].size = sizeof(struct perf_event_attr);
    event[6].config = PERF_COUNT_SW_PAGE_FAULTS;
    event[6].disabled = 1;
    event[6].exclude_user = 0;
    event[6].exclude_kernel = 0;
    event[6].exclude_idle = 0;
    event[6].exclude_hv = 0;
    
    fd[6] = perf_event_open(&event[6], 0, -1, -1, 0);
    if (fd[6] < 1)  
        ALOGD("Robust Log perfCount page fault result %d %s",errno, strerror(errno));
     
    setupCalled = true;
    ALOGD("Robust Log perf counter initialization complete");
}

/* 
 * if the counters are ready
 */
int countersReady(void)
{
    // see if any of the counters are ready
    return (((cycleCountFD > 0) || (fd[0] > 0) || (fd[1]  > 0) || (fd[2]  > 0) || (fd[3]  > 0)
                || (fd[4]  > 0)|| (fd[5]  > 0)|| (fd[6]  > 0)) && setupCalled);
}

 /*
  * reset the event counters
  */
 void resetCounters(void)
 {
    if (cycleCountFD > 0)
        ioctl(cycleCountFD, PERF_EVENT_IOC_RESET, 0);
    if (fd[0] > 0)
        ioctl(fd[0], PERF_EVENT_IOC_RESET, 0);
    if (fd[1] > 0)
        ioctl(fd[1], PERF_EVENT_IOC_RESET, 0);
    if (fd[2] > 0)
        ioctl(fd[2], PERF_EVENT_IOC_RESET, 0);
    if (fd[3] > 0)
        ioctl(fd[3], PERF_EVENT_IOC_RESET, 0);
    if (fd[4] > 0)
        ioctl(fd[4], PERF_EVENT_IOC_RESET, 0);
    if (fd[5] > 0)
        ioctl(fd[5], PERF_EVENT_IOC_RESET, 0);
    if (fd[6] > 0)
        ioctl(fd[6], PERF_EVENT_IOC_RESET, 0);

}

 /*
  * enable the event counters
  */
 void enableCounters(void)
 {
    if (cycleCountFD > 0)
        ioctl(cycleCountFD, PERF_EVENT_IOC_ENABLE, 0);
    if (fd[0] > 0)
        ioctl(fd[0], PERF_EVENT_IOC_ENABLE, 0);
    if (fd[1] > 0)
        ioctl(fd[1], PERF_EVENT_IOC_ENABLE, 0);
    if (fd[2] > 0)
        ioctl(fd[2], PERF_EVENT_IOC_ENABLE, 0);
    if (fd[3] > 0)
        ioctl(fd[3], PERF_EVENT_IOC_ENABLE, 0);
    if (fd[4] > 0)
        ioctl(fd[4], PERF_EVENT_IOC_ENABLE, 0);
    if (fd[5] > 0)
        ioctl(fd[5], PERF_EVENT_IOC_ENABLE, 0);
    if (fd[6] > 0)
        ioctl(fd[6], PERF_EVENT_IOC_ENABLE, 0);
}

 /*
  * disable the event counters
  */
 void disableCounters(void)
 {
    if (cycleCountFD > 0)
        ioctl(cycleCountFD, PERF_EVENT_IOC_DISABLE, 0);
    if (fd[0] > 0)
        ioctl(fd[0], PERF_EVENT_IOC_DISABLE, 0);
    if (fd[1] > 0)
        ioctl(fd[1], PERF_EVENT_IOC_DISABLE, 0);
    if (fd[2] > 0)
        ioctl(fd[2], PERF_EVENT_IOC_DISABLE, 0);
    if (fd[3] > 0)
        ioctl(fd[3], PERF_EVENT_IOC_DISABLE, 0);
    if (fd[4] > 0)
        ioctl(fd[4], PERF_EVENT_IOC_DISABLE, 0);
    if (fd[5] > 0)
        ioctl(fd[5], PERF_EVENT_IOC_DISABLE, 0);
    if (fd[6] > 0)
        ioctl(fd[6], PERF_EVENT_IOC_DISABLE, 0);
}

 /*
  * close the event counters
  */
 void closeCounters(void)
 {
    if (cycleCountFD > 0)
        close(cycleCountFD);
    if (fd[0] > 0)
        close(fd[0]);
    if (fd[1] > 0)
        close(fd[1]);
    if (fd[2] > 0)
        close(fd[2]);
    if (fd[3] > 0)
        close(fd[3]);
    if (fd[4] > 0)
        close(fd[4]);
    if (fd[5] > 0)
        close(fd[5]);
    if (fd[6] > 0)
        close(fd[6]);
}

/*
 * reads the values from the counters into array result
 */ 
void readCounters(uint64_t *results)
{
//    uint64_t tmp, tmp1, tmp2, tmp3;
//    tmp = tmp1 = tmp2 = 0;
    if (cycleCountFD > 0)
        read(cycleCountFD, &results[0], sizeof(uint64_t));
    if (fd[0] > 0) 
        read(fd[0], &results[1], sizeof(uint64_t));
    if (fd[1] > 0) 
        read(fd[1], &results[2], sizeof(uint64_t));
    if (fd[2] > 0)
        read(fd[2], &results[3], sizeof(uint64_t));
    if (fd[3] > 0)
        read(fd[3], &results[4], sizeof(uint64_t));
    if (fd[4] > 0) 
        read(fd[4], &results[5], sizeof(uint64_t));
    if (fd[5] > 0)
        read(fd[5], &results[6], sizeof(uint64_t));
    if (fd[6] > 0)
        read(fd[6], &results[7], sizeof(uint64_t));
    //ALOGD("Robust Log counters cfd %d fd[0] %d ifd %d tmp %llu tmp1 %llu tmp2 %llu itmp %llu %llu %llu\n", cycleCountFD, fd[0], fd[1], fd1, tmp, tmp1, tmp2, tmp3, results[2], results[3]);
    //ALOGD("Robust Log counters cfd %d instfd %d tfd %d ifd %d dmp %llu tmp1 %llu tmp2 %llu itmp %llu", cycleCountFD, fd[0], fd[1], fd1, tmp, tmp1, tmp2, tmp3);
}
