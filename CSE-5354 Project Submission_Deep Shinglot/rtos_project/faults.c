// Shell functions
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
#include "faults.h"
#include "c_fnc.h"
#include "asm_src.h"
#include "uart0.h"
#include "kernel.h"

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

// REQUIRED: If these were written in assembly
//           omit this file and add a faults.s file

// REQUIRED: code this function
void mpuFaultIsr(void)
{
    uint32_t psp = getPsp();    // top of exception entry - regs stacked on PSP
    uint32_t msp = getMsp();    // MSP
    uint32_t mFaultStat = (NVIC_FAULT_STAT_R) & 0xFF;   // if MPU fault, flag describes the type
    uint32_t regData[8];        // holds xPSR, PC, LR, R12, R0-3 (respect to 7..0 index)
    getStackDump(regData, psp); // get registers data from exception entry
    uint32_t mfaultDataAdds = NVIC_MM_ADDR_R;
    // Print all the data
    char buffer[9];
    putsUart0("\nMPU fault by pid:\t0x");   putsUart0(uint32ToHexString(getCurrentPid(), buffer));
    putsUart0("\nFault Instruction Address:\t0x");    putsUart0(uint32ToHexString(&regData[6], buffer));
    putsUart0("\nFault Data Address:\t0x");    putsUart0(uint32ToHexString(&mfaultDataAdds, buffer));
    putcUart0('\n');
    putsUart0("PSP:\t0x");      putsUart0(uint32ToHexString(&psp, buffer));
    putcUart0('\n');
    putsUart0("MSP:\t0x");      putsUart0(uint32ToHexString(&msp, buffer));
    putcUart0('\n');
    putsUart0("mfault:\t0x");   putsUart0(uint32ToHexString(&mFaultStat, buffer));
    putcUart0('\n');
    putsUart0("R0:\t0x");       putsUart0(uint32ToHexString(&regData[0], buffer));
    putcUart0('\n');
    putsUart0("R1:\t0x");       putsUart0(uint32ToHexString(&regData[1], buffer));
    putcUart0('\n');
    putsUart0("R2:\t0x");       putsUart0(uint32ToHexString(&regData[2], buffer));
    putcUart0('\n');
    putsUart0("R3:\t0x");       putsUart0(uint32ToHexString(&regData[3], buffer));
    putcUart0('\n');
    putsUart0("R12:\t0x");      putsUart0(uint32ToHexString(&regData[4], buffer));
    putcUart0('\n');
    putsUart0("LR:\t0x");       putsUart0(uint32ToHexString(&regData[5], buffer));
    putcUart0('\n');
    putsUart0("PC:\t0x");       putsUart0(uint32ToHexString(&regData[6], buffer));
    putcUart0('\n');
    putsUart0("xPSR:\t0x");     putsUart0(uint32ToHexString(&regData[7], buffer));
    putcUart0('\n');

    // turn off MPU pending bit
    NVIC_SYS_HND_CTRL_R &= ~(NVIC_SYS_HND_CTRL_MEMP);
    // sets pendSV pending state
    NVIC_INT_CTRL_R |= NVIC_INT_CTRL_PEND_SV;
}

// REQUIRED: code this function
void hardFaultIsr(void)
{
    uint32_t psp = getPsp();    // top of exception entry - regs stacked on PSP
    uint32_t msp = getMsp();    // MSP
    uint32_t mFaultStat = (NVIC_FAULT_STAT_R) & 0xFF;   // if MPU fault, flag describes the type
    uint32_t regData[8];        // holds xPSR, PC, LR, R12, R0-3 (respect to 7..0 index)
    getStackDump(regData, psp); // get registers data from exception entry

    // Print all the data
    char buffer[9];
    putsUart0("\nMPU fault by pid:\t0x");   putsUart0(uint32ToHexString(getCurrentPid(), buffer));
    putsUart0("\nOffending Address:\t0x");    putsUart0(uint32ToHexString(&regData[6], buffer));
    putcUart0('\n');
    putsUart0("PSP:\t0x");      putsUart0(uint32ToHexString(&psp, buffer));
    putcUart0('\n');
    putsUart0("MSP:\t0x");      putsUart0(uint32ToHexString(&msp, buffer));
    putcUart0('\n');
    putsUart0("mfault:\t0x");   putsUart0(uint32ToHexString(&mFaultStat, buffer));
    putcUart0('\n');
    putsUart0("R0:\t0x");       putsUart0(uint32ToHexString(&regData[0], buffer));
    putcUart0('\n');
    putsUart0("R1:\t0x");       putsUart0(uint32ToHexString(&regData[1], buffer));
    putcUart0('\n');
    putsUart0("R2:\t0x");       putsUart0(uint32ToHexString(&regData[2], buffer));
    putcUart0('\n');
    putsUart0("R3:\t0x");       putsUart0(uint32ToHexString(&regData[3], buffer));
    putcUart0('\n');
    putsUart0("R12:\t0x");      putsUart0(uint32ToHexString(&regData[4], buffer));
    putcUart0('\n');
    putsUart0("LR:\t0x");       putsUart0(uint32ToHexString(&regData[5], buffer));
    putcUart0('\n');
    putsUart0("PC:\t0x");       putsUart0(uint32ToHexString(&regData[6], buffer));
    putcUart0('\n');
    putsUart0("xPSR:\t0x");     putsUart0(uint32ToHexString(&regData[7], buffer));
    putcUart0('\n');
    // turn off bus pending bit
    NVIC_SYS_HND_CTRL_R &= ~NVIC_SYS_HND_CTRL_BUSP;
    // turn on bus fault
    NVIC_SYS_HND_CTRL_R |= NVIC_SYS_HND_CTRL_BUS;
    while(1){}
}

// REQUIRED: code this function
void busFaultIsr(void)
{
    putsUart0("\nBus fault in process pid\n");
    // turn off bus pending bit
    NVIC_SYS_HND_CTRL_R &= ~NVIC_SYS_HND_CTRL_BUSP;
    while(1){}
}

// REQUIRED: code this function
void usageFaultIsr(void)
{
    putsUart0("\nUsage fault in process pid\n");
    // turn off usage pending bit
    NVIC_SYS_HND_CTRL_R &= ~NVIC_SYS_HND_CTRL_USAGEP;
    while(1){}
}

