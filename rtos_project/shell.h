// Shell functions
// J Losh

//-----------------------------------------------------------------------------
// Hardware Target
//-----------------------------------------------------------------------------

// Target uC:       TM4C123GH6PM
// System Clock:    40 MHz

#ifndef SHELL_H_
#define SHELL_H_
#include "kernel.h"
//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

typedef struct _PS_DATA
{
    bool     isData;
    char     taskName[NAME_SIZE];
    uint16_t cpuPercent;
    uint16_t memory;
    char     state[NAME_SIZE];
    char     mutex[NAME_SIZE];
    char     semaphore[NAME_SIZE];
} PS_DATA;

void shell(void);

#endif
