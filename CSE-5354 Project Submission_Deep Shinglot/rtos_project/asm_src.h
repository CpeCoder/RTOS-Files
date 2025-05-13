// All Arm Thumb Assembly Functions
// Deep Shinglot

//-----------------------------------------------------------------------------
// Hardware Target
//-----------------------------------------------------------------------------

// Target uC:       TM4C123GH6PM
// System Clock:    40 MHz

#ifndef ASM_SRC_H
#define ASM_SRC_H

#include <stdint.h>

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------
extern void setPsp(uint32_t* pspAddr);
extern void goThreadMode(void);
extern uint32_t getPsp();
extern uint32_t getMsp();
extern void getStackDump(uint32_t* arr, uint32_t psp);
extern void goUserMode();
extern void* runFn(void* sp, void* pc);
extern void restoreRegs();
extern void* storeRegs();
extern void setExecpLr();
extern uint32_t reg0();
extern setR1 (uint16_t osTime, uint32_t addr);
#endif
