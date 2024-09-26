// Deep Shinglot

#include <stdint.h>
#include "asm_src.h"
#include "uart0.h"
#include "c_fnc.h"
#include "tm4c123gh6pm.h"

void usageFaultISR()
{
    putsUart0("\nUsage fault in process pid\n");
    // turn off usage pending bit
    NVIC_SYS_HND_CTRL_R &= ~NVIC_SYS_HND_CTRL_USAGEP;
    // disable faulting with divisor of 0
    NVIC_CFG_CTRL_R &= ~NVIC_CFG_CTRL_DIV0;
    //while(1){}
}

void busFaultISR()
{
    putsUart0("\nBus fault in process pid\n");
    // turn off bus pending bit
    NVIC_SYS_HND_CTRL_R &= ~NVIC_SYS_HND_CTRL_BUSP;
    //while(1){}
}

void hardFaultISR()
{
    uint32_t psp = getPsp();    // top of exception entry - regs stacked on PSP
    uint32_t msp = getMsp();    // MSP
    uint32_t mFaultStat = (NVIC_FAULT_STAT_R) & 0xFF;   // if MPU fault, flag describes the type
    uint32_t regData[8];        // holds xPSR, PC, LR, R12, R0-3 (respect to 7..0 index)
    getStackDump(regData, psp); // get registers data from exception entry

    // Print all the data
    char buffer[9];
    putsUart0("\nHard fault in process pid\n");
    putsUart0("Offending Address(PC):\t0x");    putsUart0(uint32ToHexString(&regData[6], buffer));
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
    putsUart0("xPSR:\t0x");     putsUart0(uint32ToHexString(&regData[7], buffer));
    putcUart0('\n');
    // turn off bus pending bit
    NVIC_SYS_HND_CTRL_R &= ~NVIC_SYS_HND_CTRL_BUSP;
    // turn on bus fault
    NVIC_SYS_HND_CTRL_R |= NVIC_SYS_HND_CTRL_BUS;
    //while(1){}
}

void mpuFaultISR()
{
    uint32_t psp = getPsp();    // top of exception entry - regs stacked on PSP
    uint32_t msp = getMsp();    // MSP
    uint32_t mFaultStat = (NVIC_FAULT_STAT_R) & 0xFF;   // if MPU fault, flag describes the type
    uint32_t regData[8];        // holds xPSR, PC, LR, R12, R0-3 (respect to 7..0 index)
    getStackDump(regData, psp); // get registers data from exception entry
    uint32_t mfaultDataAdds = NVIC_MM_ADDR_R;
    // Print all the data
    char buffer[9];
    putsUart0("\nMPU fault in process N\n");
    putsUart0("Fault Instruction Address(PC):\t0x");    putsUart0(uint32ToHexString(&regData[6], buffer));
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
    putsUart0("xPSR:\t0x");     putsUart0(uint32ToHexString(&regData[7], buffer));
    putcUart0('\n');

    // turn off MPU
    NVIC_MPU_CTRL_R &= ~(NVIC_MPU_CTRL_PRIVDEFEN | NVIC_MPU_CTRL_ENABLE);
    // turn off MPU pending bit
    NVIC_SYS_HND_CTRL_R &= ~(NVIC_SYS_HND_CTRL_MEMP);
    // sets pendSV pending state
    NVIC_INT_CTRL_R |= NVIC_INT_CTRL_PEND_SV;
}

void pendSvISR()
{
    putsUart0("\nPendsv in process N\n");
    uint32_t faultStatusValue = NVIC_FAULT_STAT_R;
    if((faultStatusValue & NVIC_FAULT_STAT_IERR) || (faultStatusValue & NVIC_FAULT_STAT_DERR))
    {
        NVIC_FAULT_STAT_R &= ~(NVIC_FAULT_STAT_IERR | NVIC_FAULT_STAT_DERR);
        putsUart0("called from MPU\n");
    }
    //while(1){}
}

void triggerBusFault()
{
    // enables bus fault
    NVIC_SYS_HND_CTRL_R |= NVIC_SYS_HND_CTRL_BUS;
    // writing on reserved memory map address, causes Bus Fault
    uint32_t regAdd = 0x40014000;
    uint32_t* resvReg = (uint32_t*)regAdd;
    *resvReg = 0xFFFFFFFF;
}

void triggerUsageFault()
{
    // enable faulting with divisor of 0
    NVIC_CFG_CTRL_R |= NVIC_CFG_CTRL_DIV0;
    // enables usage fault
    NVIC_SYS_HND_CTRL_R |= NVIC_SYS_HND_CTRL_USAGE;

    uint8_t divisor = 0;
    uint8_t dividend = 1;
    // dividing by 0 is undefined, causes Usage Fault
    uint8_t result = dividend/divisor;
}

void triggerHardFault()
{
    // disable bus fault, leds to hard fault
    NVIC_SYS_HND_CTRL_R &= ~NVIC_SYS_HND_CTRL_BUS;
    // writing on reserved memory map address, causes hard Fault since bus is disabled
    uint32_t regAdd = 0x40014000;
    uint32_t* resvReg = (uint32_t*)regAdd;
    *resvReg = 0xFFFFFFFF;
}

void triggerMpuFault()
{
    // set region number (0 - 7)
    NVIC_MPU_NUMBER_R |= 0x0;
    // set region base address (N=log2(1KB)) and let it use MPUNUMBER
        // region 0 : 0x20001400 - 0x20001800
    NVIC_MPU_BASE_R |= (0x80005 << 10) | (0 << 4);
    // set region to allow processor to fetch in exception, read-only both user and privilege,
        // (tex-s-c-b) see pg.130, no sub-regions, size encoding pg.92, enable the region
    NVIC_MPU_ATTR_R |= (0 << 28) | (0x7 << 24) | (0 << 19) | (1 << 18) | (1 << 17) | (0 << 16) |
                        (0 << 8) | (0x09 << 1) | NVIC_MPU_ATTR_ENABLE;
    // default memory map as background and enable MPU
    NVIC_MPU_CTRL_R |= NVIC_MPU_CTRL_PRIVDEFEN | NVIC_MPU_CTRL_ENABLE;

    // enable memory management fault
    NVIC_SYS_HND_CTRL_R |= NVIC_SYS_HND_CTRL_MEM;

    uint32_t readOnlyAdds = 0x20001500;
    uint32_t* readOnlyPtr = (uint32_t*) readOnlyAdds;
    *readOnlyPtr = 1;
}

void triggerPendSv()
{
    // sets pendSV pending state
    NVIC_INT_CTRL_R |= NVIC_INT_CTRL_PEND_SV;
}
