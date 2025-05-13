// Memory manager functions
// J Losh

//-----------------------------------------------------------------------------
// Hardware Target
//-----------------------------------------------------------------------------

// Target uC:       TM4C123GH6PM
// System Clock:    40 MHz

#ifndef MM_H_
#define MM_H_

#include  <stdbool.h>

#define NUM_SRAM_REGIONS 4
#define MAX_MEMORY_ALLOCATION 15
//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

// data of allocated task
typedef struct _MALLOC_DATA
{
    bool inUse;
    uint32_t size;
    void* fnPid;
    void* heapAddr;
} MALLOC_DATA;

MALLOC_DATA allocatedData[MAX_MEMORY_ALLOCATION];

void * mallocFromHeap(uint32_t size_in_bytes);
void freeToHeap(void *pMemory);

void allowFlashAccess(void);
void allowPeripheralAccess(void);
void setupSramAccess(void);
uint64_t createNoSramAccessMask(void);
void addSramAccessWindow(uint64_t *srdBitMask, uint32_t *baseAdd, uint32_t size_in_bytes);
void applySramAccessMask(uint64_t srdBitMask);
uint32_t getFreeSpace();

#endif
