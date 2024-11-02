// Kernel functions
// J Losh

//-----------------------------------------------------------------------------
// Hardware Target
//-----------------------------------------------------------------------------

// Target uC:       TM4C123GH6PM
// System Clock:    40 MHz

//-----------------------------------------------------------------------------
// Device includes, defines, and assembler directives
//-----------------------------------------------------------------------------

#include <stdint.h>
#include <stdio.h>
#include "tm4c123gh6pm.h"
#include "mm.h"
#include "kernel.h"
#include "uart0.h"
#include "asm_src.h"
#include "c_fnc.h"

//-----------------------------------------------------------------------------
// RTOS Defines and Kernel Variables
//-----------------------------------------------------------------------------

// mutex
typedef struct _mutex
{
    bool lock;
    uint8_t queueSize;
    uint8_t processQueue[MAX_MUTEX_QUEUE_SIZE];
    uint8_t lockedBy;
} mutex;
mutex mutexes[MAX_MUTEXES];

// semaphore
typedef struct _semaphore
{
    uint8_t count;
    uint8_t queueSize;
    uint8_t processQueue[MAX_SEMAPHORE_QUEUE_SIZE];
} semaphore;
semaphore semaphores[MAX_SEMAPHORES];

// Service Call (SVC) types
#define START  0                  // starts the first task
#define YIELD  1                  // sets pendSV to switch task if any ready
#define SLEEP  2                  // disables a task for x millisecond
#define LOCK   3                  // starts using mutex
#define UNLOCK 4                  // frees mutex
#define WAIT   5                  // use semaphore access
#define POST   6                  // free semaphore

// task states
#define STATE_INVALID           0 // no task
#define STATE_STOPPED           1 // stopped, all memory freed
#define STATE_READY             2 // has run, can resume at any time
#define STATE_DELAYED           3 // has run, but now awaiting timer
#define STATE_BLOCKED_MUTEX     4 // has run, but now blocked by semaphore
#define STATE_BLOCKED_SEMAPHORE 5 // has run, but now blocked by semaphore

// task
uint8_t taskCurrent = 0;          // index of last dispatched task
uint8_t taskCount = 0;            // total number of valid tasks

// control
bool priorityScheduler = true;    // priority (true) or round-robin (false)
bool priorityInheritance = false; // priority inheritance for mutexes
bool preemption = true;           // preemption (true) or cooperative (false)

// tcb
#define NUM_PRIORITIES   16
struct _tcb
{
    uint8_t state;                 // see STATE_ values above
    void *pid;                     // used to uniquely identify thread (add of task fn)
    void *spInit;                  // original top of stack
    void *sp;                      // current stack pointer
    uint8_t priority;              // 0=highest
    uint8_t currentPriority;       // 0=highest (needed for pi)
    uint32_t ticks;                // ticks until sleep complete
    uint64_t srd;                  // MPU subregion disable bits
    char name[16];                 // name of task used in ps command
    uint8_t mutex;                 // index of the mutex in use or blocking the thread
    uint8_t semaphore;             // index of the semaphore that is blocking the thread
} tcb[MAX_TASKS];

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

bool initMutex(uint8_t mutex)
{
    bool ok = (mutex < MAX_MUTEXES);
    if (ok)
    {
        mutexes[mutex].lock = false;
        mutexes[mutex].lockedBy = 0;
        mutexes[mutex].queueSize = 0;
    }
    return ok;
}

bool initSemaphore(uint8_t semaphore, uint8_t count)
{
    bool ok = (semaphore < MAX_SEMAPHORES);
    if (ok)
    {
        semaphores[semaphore].count = count;
        semaphores[semaphore].queueSize = 0;
    }
    return ok;
}

// REQUIRED: initialize systick for 1ms system timer
void initRtos(void)
{
    uint8_t i;
    // no tasks running
    taskCount = 0;
    // clear out tcb records
    for (i = 0; i < MAX_TASKS; i++)
    {
        tcb[i].state = STATE_INVALID;
        tcb[i].pid = 0;
    }
}

// REQUIRED: Implement prioritization to NUM_PRIORITIES
uint8_t rtosScheduler(void)
{
    bool ok;
    static uint8_t task = 0xFF;
    static uint8_t priorityTaskCounter[16] = {0};
    uint8_t i, j, priority = 0;     // always start from priority 0
    ok = false;
    while (!ok)
    {
        i = priorityTaskCounter[priority];  // start from the last checked task at the current priority.

        for (j = 0; j < taskCount; j++)
        {
            if (tcb[i].priority == priority && tcb[i].state == STATE_READY)
            {
                ok = true;                              // ready task is found.
                priorityTaskCounter[priority] = i;      // save the index for next scheduling cycle.
                return i;                               // return the index of the ready task.
            }
            i = (i + 1) % taskCount;        // move to the next task.
        }
        // if no ready task is found at the current priority, go to the next priority.
        priority = (priority + 1) % NUM_PRIORITIES;
    }

    while (!ok)
    {
        task++;
        if (task >= MAX_TASKS)
            task = 0;
        ok = (tcb[task].state == STATE_READY);
    }
    return task;
}

// REQUIRED: modify this function to start the operating system
// by calling scheduler, set srd bits, setting PSP, ASP bit, call fn with fn addr in R0
// fn set TMPL bit, and PC <= fn
void startRtos(void)
{
    // enable Memory Protection Unit
    NVIC_MPU_CTRL_R |= NVIC_MPU_CTRL_PRIVDEFEN | NVIC_MPU_CTRL_ENABLE;
    setPsp((uint32_t*)0x20008000);
    goThreadMode();         // switch to thread mode by setting ASP
    //goUserMode();           // changes from privilege to un-privilege,  TMPL bit
    __asm(" SVC #0");       // service call, requesting kernel to do privilege task
}

// REQUIRED:
// add task if room in task list
// store the thread name
// allocate stack space and store top of stack in sp and spInit
// set the srd bits based on the memory allocation
// initialize the created stack to make it appear the thread has run before
bool createThread(_fn fn, const char name[], uint8_t priority, uint32_t stackBytes)
{
    bool ok = false;
    uint8_t i = 0;
    bool found = false;
    if (taskCount < MAX_TASKS)
    {
        // make sure fn not already in list (prevent reentrancy)
        while (!found && (i < MAX_TASKS))
        {
            found = (tcb[i++].pid ==  fn);
        }
        if (!found)
        {
            void* spBase = mallocFromHeap(stackBytes);
            if (!spBase)
                return ok;
            // find first available tcb record
            i = 0;
            while (tcb[i].state != STATE_INVALID) {i++;}
            tcb[i].state = STATE_READY;
            tcb[i].pid = fn;
            tcb[i].sp = (void*)((uint32_t)spBase + stackBytes);         // initially points top of the stack
            tcb[i].spInit = (void*)((uint32_t)spBase + stackBytes);     // points top of the stack
            tcb[i].priority = priority;
            tcb[i].srd = createNoSramAccessMask();
            addSramAccessWindow(&(tcb[i].srd), (uint32_t*)spBase, stackBytes);
            strCpy(name, tcb[i].name);

            tcb[i].sp = runFn(tcb[i].sp, tcb[i].pid);       // runs fn (stores registers on stack and update sp)

            taskCount++;            // increment task count
            ok = true;
        }
    }
    return ok;
}

// REQUIRED: modify this function to restart a thread
void restartThread(_fn fn)
{
}

// REQUIRED: modify this function to stop a thread
// REQUIRED: remove any pending semaphore waiting, unlock any mutexes
void stopThread(_fn fn)
{
}

// REQUIRED: modify this function to set a thread priority
void setThreadPriority(_fn fn, uint8_t priority)
{
}

// REQUIRED: modify this function to yield execution back to scheduler using pendsv
void yield(void)
{
    __asm(" SVC #1");
}

// REQUIRED: modify this function to support 1ms system timer
// execution yielded back to scheduler until time elapses using pendsv
void sleep(uint32_t tick)
{
    __asm(" SVC #2");
}

// REQUIRED: modify this function to lock a mutex using pendsv
void lock(int8_t mutex)
{
    __asm(" SVC #3");
}

// REQUIRED: modify this function to unlock a mutex using pendsv
void unlock(int8_t mutex)
{
    __asm(" SVC #4");
}

// REQUIRED: modify this function to wait a semaphore using pendsv
void wait(int8_t semaphore)
{
    __asm(" SVC #5");
}

// REQUIRED: modify this function to signal a semaphore is available using pendsv
void post(int8_t semaphore)
{
    __asm(" SVC #6");
}

// REQUIRED: modify this function to add support for the system timer
// REQUIRED: in preemptive code, add code to request task switch
void systickIsr(void)
{
    uint8_t i = 0;
    for (i = 0; i < taskCount; i++)
    {
        if (tcb[i].state == STATE_DELAYED)
        {
            if (tcb[i].ticks != 0)          // task ticker
                (tcb[i].ticks)--;
            if (tcb[i].ticks == 0)          // timer expired, do task
                tcb[i].state = STATE_READY;
        }
    }
}

// REQUIRED: in coop and preemptive, modify this function to add support for task switching
// REQUIRED: process UNRUN and READY tasks differently
//__attribute__((naked)), alternative solution to POP and PUSH at end and beginning of the function
    // attribute naked tells GCC function should be generated without any prologue or epilogue code
void pendSvIsr(void)
{
    __asm(" PUSH {LR}");                        // exception return value, changes on any fnc call so store it
    tcb[taskCurrent].sp = storeRegs();          // stores R4-11 and LR, and updates psp and psp value
    taskCurrent = rtosScheduler();
    setPsp((uint32_t*)tcb[taskCurrent].sp);
    applySramAccessMask(tcb[taskCurrent].srd);
    restoreRegs();                              // restore r4-11 and updates psp
    __asm(" POP {LR}");                         // load exception return value
}

// REQUIRED: modify this function to add support for the service call
// REQUIRED: in preemptive code, add code to handle synchronization primitives
//__attribute__((naked))
void svCallIsr(void)
{
    __asm(" PUSH {LR}");
    uint32_t* psp = (uint32_t*)getPsp();
    uint32_t r0 = *psp;
    // first add 6 to point at PC address and cast to uint8 double pointer
    uint8_t** pcPtr = (uint8_t**)(psp + 6);
    // SVC half-word instruction, so decrement by 2 to point at immediate value (8b)
    uint8_t imm = (uint8_t)(*(*pcPtr - 2));
    switch(imm)
    {
    case START:
        taskCurrent = rtosScheduler();
        setPsp((uint32_t*)tcb[taskCurrent].sp);
        applySramAccessMask(tcb[taskCurrent].srd);
        restoreRegs();
        setExecpLr();   // does not return
        break;
    case YIELD:
        // sets pendSV pending state
        NVIC_INT_CTRL_R |= NVIC_INT_CTRL_PEND_SV;
        break;
    case SLEEP:
        tcb[taskCurrent].ticks = r0;                    // r0 holds sleep time (ms)
        tcb[taskCurrent].state = STATE_DELAYED;
        NVIC_INT_CTRL_R |= NVIC_INT_CTRL_PEND_SV;       // task switch
        break;
    case LOCK:
        // the value of R0, which mutex is being used - always be 0
        if (r0 >= MAX_MUTEXES)                  // requested non-exist resource
            break;

        if (!mutexes[r0].lock)                  // mutex is available
        {
            tcb[taskCurrent].mutex = r0;
            mutexes[r0].lock = true;            // lock mutex
            mutexes[r0].lockedBy = taskCurrent; // store who is locking, only that can free mutex
        }
        else
        {
            if (mutexes[r0].queueSize < MAX_MUTEX_QUEUE_SIZE)
            {
                tcb[taskCurrent].mutex = r0;                                   // stores which mutex block the task
                tcb[taskCurrent].state = STATE_BLOCKED_MUTEX;                  // stop task until resource is available
                // put task in queue, and update queue
                mutexes[r0].processQueue[mutexes[r0].queueSize++] = taskCurrent;
            }
            NVIC_INT_CTRL_R |= NVIC_INT_CTRL_PEND_SV;          // task switch, can't let task run with resource
        }
        break;
    case UNLOCK:
        // the value of R0, defines which mutex is being used - always be 0
        if (r0 >= MAX_MUTEXES)                  // accessing non-exist resource
            break;

        if (mutexes[r0].lockedBy == taskCurrent)
        {
            mutexes[r0].lock = false;           // unlock resource
            if (mutexes[r0].queueSize > 0)      // if any task in queue for resource, then lock it again
            {
                uint8_t i, nextTaskId = mutexes[r0].processQueue[0];
                tcb[nextTaskId].state = STATE_READY;
                tcb[nextTaskId].mutex = r0;
                mutexes[r0].lock = true;                        // lock mutex
                mutexes[r0].lockedBy = nextTaskId;              // store who is locking, only that can free mutex
                mutexes[r0].queueSize--;
                for (i = 0; i < mutexes[r0].queueSize; i++)     // update the queue, shift the task down by 1 index
                    mutexes[r0].processQueue[i] = mutexes[r0].processQueue[i + 1];
            }
        }
        else        // unprotected access, kill pid
        {
            tcb[taskCurrent].state = STATE_STOPPED;
            freeToHeap(tcb[taskCurrent].spInit);
            NVIC_INT_CTRL_R |= NVIC_INT_CTRL_PEND_SV;       // task is stopped, start other
        }
        break;
    case WAIT:
        if (r0 >= MAX_SEMAPHORES)                // accessing non-existing semaphore, exit
            break;
        if (semaphores[r0].count > 0)
            semaphores[r0].count--;
        else
        {
            if (semaphores[r0].queueSize < MAX_SEMAPHORE_QUEUE_SIZE)
            {
                tcb[taskCurrent].semaphore = r0;                            // store which semaphore blocked the task
                tcb[taskCurrent].state = STATE_BLOCKED_SEMAPHORE;           // stop task until resource is available
                // put task in queue, and update queue
                semaphores[r0].processQueue[semaphores[r0].queueSize++] = taskCurrent;
            }
            NVIC_INT_CTRL_R |= NVIC_INT_CTRL_PEND_SV;       // task is stopped, start other
        }
        break;
    case POST:
        // the value of R0, defines which semaphore is being used - always be 0
        if (r0 >= MAX_SEMAPHORES)                  // accessing non-exist resource
            break;
        semaphores[r0].count++;                    // free shared access

        if (semaphores[r0].queueSize > 0)      // if any task in queue for resource, then give it access
        {
            uint8_t i, nextTaskId = semaphores[r0].processQueue[0];
            semaphores[r0].count--;
            semaphores[r0].queueSize--;
            tcb[nextTaskId].state = STATE_READY;
            for (i = 0; i < semaphores[r0].queueSize; i++)     // update the queue, shift the task down by 1 index
                semaphores[r0].processQueue[i] = semaphores[r0].processQueue[i + 1];
        }
       break;
    }

    __asm(" POP {LR}");

/** next cmd is a problem because, GCC PUSH register(s) on fnc calls
 ** but since fnc exits to 0xFFFFFFFD the stack keeps increasing on every isr run, leading to hard fault */
    //setExecpLr();
}

