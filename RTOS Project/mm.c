// Memory manager functions
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
#include "tm4c123gh6pm.h"
#include "mm.h"

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

// REQUIRED: add your malloc code here and update the SRD bits for the current thread
void * mallocFromHeap(uint32_t size_in_bytes)
{
    return 0;
}

// REQUIRED: add your free code here and update the SRD bits for the current thread
void freeToHeap(void *pMemory)
{
}

// REQUIRED: include your solution from the mini project
void allowFlashAccess(void)
{
}

void allowPeripheralAccess(void)
{
}

void setupSramAccess(void)
{
}

uint64_t createNoSramAccessMask(void)
{
    return 0;
}

void addSramAccessWindow(uint64_t *srdBitMask, uint32_t *baseAdd, uint32_t size_in_bytes)
{
}

void applySramAccessMask(uint64_t srdBitMask)
{
}


