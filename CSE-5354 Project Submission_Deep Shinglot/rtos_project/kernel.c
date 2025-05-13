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

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "tm4c123gh6pm.h"
#include "mm.h"
#include "kernel.h"
#include "uart0.h"
#include "asm_src.h"
#include "c_fnc.h"
#include "shell.h"

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
#define START   0                 // starts the first task
#define YIELD   1                 // sets pendSV to switch task if any ready
#define SLEEP   2                 // disables a task for x millisecond
#define LOCK    3                 // starts using mutex
#define UNLOCK  4                 // frees mutex
#define WAIT    5                 // use semaphore access
#define POST    6                 // free semaphore
#define MALLOC  7                 // process a malloc request
#define IPCS    8                 // display mutex and semaphore status
#define KILL    9                 // kills a task by given pid #
#define PKILL   10                // kills a task by given task's name
#define PIDOF   11                // gets the pid value of task by its name
#define SCHED   12                // switches between priority and round robin scheduling
#define PREEMPT 13                // switches between cooperative and preemptive RTOS
#define PI      14                // enables/disables priority inheritance
#define MEMINFO 15                // outputs tasks' allocation on UART0
#define REBOOT  16                // allows to reboot M4
#define RESTART 17                // reset a task
#define NAME_R  18                // reset a task by name
#define SET_PRI 19                // changes priority of a task
#define PS      20                // stores ps data

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
    uint32_t size;                 // size of the task stack
    uint32_t ticks;                // ticks until sleep complete
    uint32_t clockA;               // time the task takes of CPU (CPU use) buffer A, wr when pingpong 0 else rd
    uint32_t clockB;               // time the task takes of CPU (CPU use) buffer B, wr when pingpong 1 else rd
    uint64_t srd;                  // MPU subregion disable bits
    char name[16];                 // name of task used in ps command
    uint8_t mutex;                 // index of the mutex in use or blocking the thread
    uint8_t semaphore;             // index of the semaphore that is blocking the thread
} tcb[MAX_TASKS];

#define TASK_CPU_TIME_PERIOD 2000  // x milliseconds to update CPU time consumed by each task
bool pingPong = false;
uint16_t clockCounter = 0;         // keeps a timer
uint32_t startTimer = 0, endTimer = 0;

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
        tcb[i].clockA = 0;
        tcb[i].clockB = 0;
    }

    // setup system timer
    NVIC_ST_RELOAD_R  = 40e3 - 1;   // sysTick will 1 millisecond muti-shot timer
    NVIC_ST_CURRENT_R = 0;          // W1C register, NOTE: current and reload are only 24 bit
    // user system clock, enable interrupt, enable muti-shot (keeps reloading)
    NVIC_ST_CTRL_R    = NVIC_ST_CTRL_CLK_SRC | NVIC_ST_CTRL_INTEN | NVIC_ST_CTRL_ENABLE;
}

// REQUIRED: Implement prioritization to NUM_PRIORITIES
uint8_t rtosScheduler(void)
{
    bool ok;
    static uint8_t task = 0xFF;
    static uint8_t priorityTaskCounter[16] = {0};
    uint8_t i, priorityValue = 0xFF;     // always start from priority 0
    ok = false;

    if(priorityScheduler)
    {
        for (i = 0; i < taskCount; i++)
        {
            uint8_t taskPrio = tcb[i].currentPriority;
            if (taskPrio < priorityValue && tcb[i].state == STATE_READY)
                priorityValue = taskPrio;
        }
    }

    while (!ok)
    {
        if (priorityScheduler)
        {
            uint8_t currInx = (priorityTaskCounter[priorityValue] + 1) % taskCount;
            for (i = 0; i < taskCount; i++)
            {
                ok = (tcb[currInx].state == STATE_READY && tcb[currInx].currentPriority == priorityValue);
                if (ok)
                {
                    task = currInx;
                    priorityTaskCounter[priorityValue] = currInx;
                    break;
                }
                currInx = (currInx + 1) % taskCount;
            }
            if (!ok)
            {
                ok = true;
                task = priorityTaskCounter[priorityValue];
            }
        }
        else
        {
            task++;
            if (task >= MAX_TASKS)
                task = 0;
            ok = (tcb[task].state == STATE_READY);
        }
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
    goUserMode();           // changes from privilege to un-privilege,  TMPL bit
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
            tcb[i].currentPriority = priority;
            tcb[i].size = stackBytes;
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

void mallocRequest(uint32_t size, void** address)
{
    __asm(" SVC #7");
    __asm(" STR R0, [R1]");
}

// REQUIRED: modify this function to restart a thread
void restartThread(_fn fn)
{
    __asm(" SVC #17");
}

// REQUIRED: modify this function to stop a thread
// REQUIRED: remove any pending semaphore waiting, unlock any mutexes
void stopThread(_fn fn)
{
    __asm(" SVC #9");
}

// REQUIRED: modify this function to set a thread priority
void setThreadPriority(_fn fn, uint8_t priority)
{
    __asm(" SVC #19");
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
    if (preemption)
        NVIC_INT_CTRL_R |= NVIC_INT_CTRL_PEND_SV;

    clockCounter++;
    if (clockCounter >= TASK_CPU_TIME_PERIOD)   // marks x seconds timer, in this project 2s
    {
        clockCounter = 0;
        pingPong = !pingPong;           // switch clock buffers
        if (!pingPong)
            for (i = 0; i < taskCount; i++)
                tcb[i].clockA = 0;
        else
            for (i = 0; i < taskCount; i++)
                tcb[i].clockB = 0;
    }
}

// REQUIRED: in coop and preemptive, modify this function to add support for task switching
// REQUIRED: process UNRUN and READY tasks differently
//__attribute__((naked)), alternative solution to POP and PUSH at end and beginning of the function
    // attribute naked tells GCC function should be generated without any prologue or epilogue code
__attribute__((naked)) void pendSvIsr(void)
{
    // __asm(" PUSH {LR}");                     // exception return value, changes on any fnc call so store it'
    __asm(" MOV R1, LR");
    tcb[taskCurrent].sp = storeRegs();          // stores R4-11 and LR, and updates psp and psp value

    endTimer = NVIC_ST_CURRENT_R;
    // if true, SysTick timer has counted to 0 since the last time this bit was read
    if (startTimer < endTimer)
        if (pingPong == false)
            tcb[taskCurrent].clockA += (40e3 - endTimer) + startTimer;
        else
            tcb[taskCurrent].clockB += (40e3 - endTimer) + startTimer;
    else
        if (pingPong == false)
            tcb[taskCurrent].clockA += startTimer - endTimer;
        else
            tcb[taskCurrent].clockB += startTimer - endTimer;


    if((NVIC_FAULT_STAT_R & NVIC_FAULT_STAT_IERR) || (NVIC_FAULT_STAT_R & NVIC_FAULT_STAT_DERR))
    {
        NVIC_FAULT_STAT_R |= NVIC_FAULT_STAT_IERR | NVIC_FAULT_STAT_DERR;       // W1C
        putsUart0("called from MPU\n");
        uint32_t psp = getPsp();
        uint8_t i;
        for (i = 0; i < taskCount; i++)
        {
            uint32_t upperBound = (uint32_t)allocatedData[i].heapAddr;
            uint32_t lowerBound = (uint32_t)allocatedData[i].heapAddr - allocatedData[i].size;
            if (psp <= upperBound && psp > lowerBound)
            {
                freeToHeap(allocatedData[i].heapAddr);
                break;
            }
        }
        char hexString[10];
        putsUart0("killed PID: "); putsUart0(uint32ToHexString((uint32_t*)&tcb[i].pid, hexString)); putsUart0("\n\n");
        tcb[i].state = STATE_STOPPED;
        putsUart0("\nuser@rtos:~$ ");
    }
    taskCurrent = rtosScheduler();
    applySramAccessMask(tcb[taskCurrent].srd);
    setPsp((uint32_t*)tcb[taskCurrent].sp);
    startTimer = NVIC_ST_CURRENT_R;
    restoreRegs();                              // restore r4-11 and never returns
}

void* pidOfTask(char taskName[])
{
    uint8_t i;
    for (i = 0; i < taskCount; i++)
    {
        if (strCmp(tcb[i].name, taskName))
            return tcb[i].pid;
    }
    return NULL;
}

uint32_t* getCurrentPid()
{
    return (uint32_t*)&tcb[taskCurrent].pid;
}

void killThread(_fn fn)
{
    void* pid = (void*)fn;
    uint8_t i,k;
    for (i = 0; i < taskCount; i++)
    {
        if (pid == tcb[i].pid)
        {
            tcb[i].state = STATE_STOPPED;
            freeToHeap(tcb[i].spInit);
            tcb[i].srd = createNoSramAccessMask();
            for (k = 0; k < MAX_MEMORY_ALLOCATION; k++)
            {
                if (allocatedData[k].heapAddr == tcb[i].spInit) // mark the parent task, not in use
                    allocatedData[k].inUse = false;
                if (tcb[i].pid == allocatedData[k].fnPid)
                {
                    freeToHeap(allocatedData[k].heapAddr);     // if parent is removed, remove child allocation
                    allocatedData[k].inUse = false;
                }
            }
            uint8_t j;
            // takes the task out of the mutex lock (giving it next in queue) or mutex queue (updates it)
            for (j = 0; j < MAX_MUTEXES; j++)
            {
                if (mutexes[j].lockedBy == i)
                {
                    mutexes[j].lock = false;            // unlock resource
                    if (mutexes[j].queueSize > 0)       // if any task in queue for resource, then lock it again
                    {
                        uint8_t k, nextTaskId = mutexes[j].processQueue[0];
                        tcb[nextTaskId].state = STATE_READY;
                        tcb[nextTaskId].mutex = j;
                        mutexes[j].lock = true;                        // lock mutex
                        mutexes[j].lockedBy = nextTaskId;              // store who is locking, only that can free mutex
                        mutexes[j].queueSize--;
                        for (k = 0; k < mutexes[j].queueSize; k++)     // update the queue, shift the task down by 1 index
                            mutexes[j].processQueue[k] = mutexes[j].processQueue[k + 1];
                    }
                }
                else if (mutexes[j].queueSize > 0)
                {
                    uint8_t k;
                    for (k = 0; k < mutexes[j].queueSize; k++)     // update the queue, shift the task down by 1 index
                    {
                        if (mutexes[j].processQueue[k] == i)
                        {
                            mutexes[j].queueSize--;
                            if (k+1 < MAX_MUTEX_QUEUE_SIZE)
                                mutexes[j].processQueue[k] = mutexes[j].processQueue[k + 1];
                        }
                    }
                }
            }
            // checks semaphore queue, takes the task out if in queue while updating queue
            for (j = 0; j < MAX_SEMAPHORES; j++)
            {
                if (semaphores[j].queueSize > 0)
                {
                    uint8_t k;
                    for (k = 0; k < semaphores[j].queueSize; k++)     // update the queue, shift the task down by 1 index
                    {
                        if (semaphores[j].processQueue[k] == i)
                        {
                            semaphores[j].queueSize--;
                            if (k+1 < MAX_SEMAPHORE_QUEUE_SIZE)
                                semaphores[j].processQueue[k] = semaphores[j].processQueue[k + 1];
                        }
                    }
                }
            }
        }
    }
}

// REQUIRED: modify this function to add support for the service call
// REQUIRED: in preemptive code, add code to handle synchronization primitives
//__attribute__((naked))
void svCallIsr(void)
{
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

                if (priorityInheritance)
                {
                    if (tcb[taskCurrent].priority < tcb[mutexes[r0].lockedBy].priority)
                        tcb[mutexes[r0].lockedBy].currentPriority = tcb[taskCurrent].priority;
                }
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
            tcb[taskCurrent].currentPriority = tcb[taskCurrent].priority;
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
    case MALLOC:
    {
        uint32_t address = (uint32_t)mallocFromHeap(r0);
        if (address >= 0x20001000 && address < 0x20008000)
        {
            *psp = address;
            uint64_t srdMask = createNoSramAccessMask();
            addSramAccessWindow(&srdMask, (uint32_t*)address, r0);
            tcb[taskCurrent].srd &= srdMask;
            applySramAccessMask(tcb[taskCurrent].srd);

            uint8_t i;
            for (i = 0; i < MAX_MEMORY_ALLOCATION; i++)
            {
                if ((void*)(address+allocatedData[i].size) == allocatedData[i].heapAddr)
                {
                    allocatedData[i].fnPid = tcb[taskCurrent].pid;     // assign the allocated parent
                    break;
                }
            }
        }
    }
        break;
    case IPCS:
    {
        uint8_t i = 0;
        putsUart0("------ Mutex Queues ------\n");
        if(mutexes[i].lock)
        {
            putsUart0("  name\t\t owner\t\t status\n");
            putsUart0("resource"); putcUart0('\t'); putsUart0(tcb[mutexes[i].lockedBy].name); putsUart0("\tin use\n");
            if (mutexes[i].queueSize > 0)      // if any task in queue for resource, then lock it again
            {
                uint8_t j;
                for (j = 0; j < mutexes[i].queueSize; j++)     // update the queue, shift the task down by 1 index
                {
                    putsUart0("resource"); putcUart0('\t'); putsUart0(tcb[mutexes[i].processQueue[j]].name); putsUart0("\tin queue\n");
                }
            }
        }
        putsUart0("\n------ Semaphore Queues ------\n");
        putsUart0("  name\t\t owner\t\t perms\t\t nsems\n");
        for (i = 0; i < MAX_SEMAPHORES; i++)     // update the queue, shift the task down by 1 index
        {
            uint8_t j;
            if (i == keyPressed)
                putsUart0("> keyPressed");
            else if (i == keyReleased)
                putsUart0("> keyReleased");
            else if (i == flashReq)
                putsUart0("> flashReq");
            char count[5];
            numToStr(semaphores[i].count, count);
            putsUart0("  \t  user\t\t  666\t\t"); putsUart0(count); putcUart0('\n');
            putsUart0("\t--Blocked task--\t\n");
            for (j = 0; j < semaphores[i].queueSize; j++)     // update the queue, shift the task down by 1 index
            {
                putsUart0(tcb[semaphores[i].processQueue[j]].name); putsUart0(" in queue\n\n");
            }
        }
    }
        break;
    case KILL:
        killThread((_fn)r0);
        break;
    case PKILL:
    {
        _fn pid = (_fn) pidOfTask((char*) r0);
        killThread(pid);
    }
        break;
    case PIDOF:
        *psp = (uint32_t) pidOfTask((char*) r0);
        break;
    case SCHED:
        if(r0)
            priorityScheduler = true;
        else
            priorityScheduler = false;
        break;
    case PREEMPT:
        if(r0)
            preemption = true;
        else
            preemption = false;
        break;
    case PI:
        if(r0)
            priorityInheritance = true;
        else
            priorityInheritance = false;
        break;
    case MEMINFO:
    {
        uint8_t i;
        putsUart0("----Memory Information----\n  Task\t\tSize\t\tPid\t\tStack Address\n");
        char info[20];
        for (i = 0; i < taskCount; i++)
        {
            putsUart0("----------------------------------------------------------------\n");
            if (tcb[i].state != STATE_STOPPED)
            {
                putsUart0(tcb[i].name); putsUart0("    \t");
                putsUart0(numToStr(tcb[i].size, info)); putsUart0(" B    \t0x"); putsUart0(uint32ToHexString((uint32_t*)&tcb[i].pid, info));
                putsUart0("    \t0x"); putsUart0(uint32ToHexString((uint32_t*)&tcb[i].spInit, info)); putsUart0("\n");

                uint8_t j;
                for (j = 0; j < MAX_MEMORY_ALLOCATION; j++)
                {
                    if (tcb[i].pid == allocatedData[j].fnPid)
                    {
                        putsUart0("    \t\t"); putsUart0(numToStr(allocatedData[j].size, info));
                        putsUart0(" B    \t0x"); putsUart0(uint32ToHexString((uint32_t*)&tcb[i].pid, info)); putsUart0("    \t0x");
                        putsUart0(uint32ToHexString((uint32_t*)&allocatedData[j].heapAddr, info)); putsUart0("\n");
                    }
                }
            }
        }
        putsUart0("Free space ");
        putsUart0(numToStr(getFreeSpace(), info));
        putsUart0(" B of 28672 B\n\n");
    }
        break;
    case REBOOT:
        NVIC_APINT_R = NVIC_APINT_VECTKEY | NVIC_APINT_SYSRESETREQ;
        break;
    case RESTART:
    {
        void* pid = (void*)r0;
        uint8_t i;
        for (i = 0; i < taskCount; i++)
        {
            if (pid == tcb[i].pid && tcb[i].state == STATE_STOPPED)
            {
                tcb[i].spInit = (void*)((uint32_t)mallocFromHeap(tcb[i].size) + tcb[i].size);
                tcb[i].sp = tcb[i].spInit;
                tcb[i].sp = runFn(tcb[i].sp, tcb[i].pid);
                tcb[i].state = STATE_READY;
                tcb[i].srd = createNoSramAccessMask();
                addSramAccessWindow(&(tcb[i].srd), (uint32_t*)((uint32_t)tcb[i].spInit - tcb[i].size), tcb[i].size);
                break;
            }
        }
    }
        break;
    case NAME_R:
    {
        void* pid = pidOfTask((char*)r0);
        uint8_t i;
        for (i = 0; i < taskCount; i++)
        {
            if (pid == tcb[i].pid && STATE_STOPPED == tcb[i].state)
            {
                tcb[i].spInit = (void*)((uint32_t)mallocFromHeap(tcb[i].size) + tcb[i].size);
                tcb[i].sp = tcb[i].spInit;
                tcb[i].sp = runFn(tcb[i].sp, tcb[i].pid);
                tcb[i].state = STATE_READY;
                tcb[i].srd = createNoSramAccessMask();
                addSramAccessWindow(&(tcb[i].srd), (uint32_t*)((uint32_t)tcb[i].spInit - tcb[i].size), tcb[i].size);
                break;
            }
        }
    }
        break;
    case SET_PRI:
    {
        uint32_t r1 = *(psp+1);
        void* pid = (void*)r0;;
        uint8_t i;
        for (i = 0; i < taskCount; i++)
        {
            if (pid == tcb[i].pid)
            {
                tcb[i].currentPriority = r1;
                tcb[i].priority = r1;
                break;
            }
        }
    }
        break;
    case PS:        // r0 have the address of ps struct
    {
        PS_DATA* psInfo = (PS_DATA*)r0;
        uint32_t r1 = *(psp + 1);
        uint8_t i,j;
        uint32_t totalTime = 0, cpuPeriod = (TASK_CPU_TIME_PERIOD * 40e3);
        for (j = 0; j < taskCount; j++)
            if (pingPong == false)
                totalTime += tcb[j].clockB;
            else
                totalTime += tcb[j].clockA;
        for (i = 0; i < taskCount; i++)
        {
            psInfo[i].isData = true;
            strCpy(tcb[i].name, psInfo[i].taskName);
            uint32_t taskTime = (pingPong == false) ? tcb[i].clockB : tcb[i].clockA;
            //psInfo[i].cpuPercent = ((float)taskTime / totalTime) * 10000;
            psInfo[i].cpuPercent = ((float)taskTime / cpuPeriod) * 10000;
            psInfo[i].memory = (uint16_t) tcb[i].size;
            uint8_t taskState = tcb[i].state;
            switch (taskState)
            {
            case STATE_BLOCKED_MUTEX:
                strCpy("BLOCKED_MUTEX    ", psInfo[i].state);
                break;
            case STATE_BLOCKED_SEMAPHORE:
                strCpy("BLOCKED_SEMAPHORE", psInfo[i].state);
                break;
            case STATE_DELAYED:
                strCpy("DELAYED          ", psInfo[i].state);
                break;
            case STATE_INVALID:
                strCpy("INVALID          ", psInfo[i].state);
                break;
            case STATE_READY:
                strCpy("READY            ", psInfo[i].state);
                break;
            case STATE_STOPPED:
                strCpy("STOPPED          ", psInfo[i].state);
                break;
            }
            if (taskState == STATE_BLOCKED_MUTEX)
            {
                switch(tcb[i].mutex)
                {
                case resource:
                    strCpy("resource", psInfo[i].mutex);
                    break;
                default:
                    strCpy("N/A     ", psInfo[i].mutex);
                    break;
                }
            }
            else
                strCpy("N/A     ", psInfo[i].mutex);
            if (taskState == STATE_BLOCKED_SEMAPHORE)
            {
                switch(tcb[i].semaphore)
                {
                case keyPressed:
                    strCpy("keyPressed ", psInfo[i].semaphore);
                    break;
                case keyReleased:
                    strCpy("keyReleased", psInfo[i].semaphore);
                    break;
                case flashReq:
                    strCpy("flashReq   ", psInfo[i].semaphore);
                    break;
                default:
                    strCpy("N/A        ", psInfo[i].semaphore);
                    break;
                }
            }
            else
                strCpy("N/A", psInfo[i].semaphore);
            uint16_t kernelTime = (float)(cpuPeriod - totalTime) / cpuPeriod * 10e3;
            setR1(kernelTime, r1);
        }
    }
        break;
    }


/** next cmd is a problem because, GCC PUSH register(s) on fnc calls
 ** but since fnc exits to 0xFFFFFFFD the stack keeps increasing on every isr run, leading to hard fault */
    //setExecpLr();
}

