// RTOS Framework - Fall 2024
// J Losh

// Student Name: Deep Shinglot
// TO DO: Add your name(s) on this line.
//        Do not include your ID number(s) in the file.

// Please do not change any function name in this code or the thread priorities

//-----------------------------------------------------------------------------
// Hardware Target
//-----------------------------------------------------------------------------

// Target Platform: EK-TM4C123GXL Evaluation Board
// Target uC:       TM4C123GH6PM
// System Clock:    40 MHz

// Hardware configuration:
// 6 Pushbuttons and 5 LEDs, UART
// UART Interface:
//   U0TX (PA1) and U0RX (PA0) are connected to the 2nd controller
//   The USB on the 2nd controller enumerates to an ICDI interface and a virtual COM port
//   Configured to 115,200 baud, 8N1
// Memory Protection Unit (MPU):
//   Region to control access to flash, peripherals, and bitbanded areas
//   4 or more regions to allow SRAM access (RW or none for task)

//-----------------------------------------------------------------------------
// Device includes, defines, and assembler directives
//-----------------------------------------------------------------------------

#include "tm4c123gh6pm.h"
#include "clock.h"
#include "gpio.h"
#include "uart0.h"
#include "wait.h"
#include "mm.h"
#include "kernel.h"
#include "faults.h"
#include "tasks.h"
#include "shell.h"

//-----------------------------------------------------------------------------
// Main
//-----------------------------------------------------------------------------

int main(void)
{
    bool ok;

    // Initialize hardware
    initSystemClockTo40Mhz();
    initHw();
    initUart0();
    allowFlashAccess();
    allowPeripheralAccess();
    setupSramAccess();
    initRtos();

    // Initialize mutexes and semaphores
    initMutex(resource);
    initSemaphore(keyPressed, 1);
    initSemaphore(keyReleased, 0);
    initSemaphore(flashReq, 5);

    // Add required idle process at lowest priority
    ok =  createThread(idle, "Idle", 15, 512);

    // Add other processes
    ok &= createThread(lengthyFn, "LengthyFn", 12, 1024);
    ok &= createThread(flash4Hz, "Flash4Hz", 8, 512);
    ok &= createThread(oneshot, "OneShot", 8, 1536);
    //ok &= createThread(readKeys, "ReadKeys", 12, 1024);
    //ok &= createThread(debounce, "Debounce", 12, 1024);
    ok &= createThread(important, "Important", 0, 1024);
    //ok &= createThread(uncooperative, "Uncoop", 12, 1024);
    //ok &= createThread(errant, "Errant", 12, 512);
    //ok &= createThread(shell, "Shell", 12, 4096);

    // TODO: Add code to implement a periodic timer and ISR
    // Enable clocks
    SYSCTL_RCGCTIMER_R |= SYSCTL_RCGCTIMER_R4;
    _delay_cycles(3);
    // Configure Timer 4 for 1 sec tick
    TIMER4_CTL_R  &= ~TIMER_CTL_TAEN;                // turn-off timer before reconfiguring
    TIMER4_CFG_R   = TIMER_CFG_32_BIT_TIMER;         // configure as 32-bit timer (A+B)
    TIMER4_TAMR_R  = TIMER_TAMR_TAMR_PERIOD;         // configure for periodic mode (count down)
    TIMER4_TAILR_R = 40000000;                       // set load value (1 Hz rate)
    //TIMER4_CTL_R  |= TIMER_CTL_TAEN;                 // turn-on timer
    //TIMER4_IMR_R  |= TIMER_IMR_TATOIM;               // turn-on interrupt
    //NVIC_EN2_R    |= 1 << (INT_TIMER4A-80);          // turn-on interrupt 86 (TIMER4A)

    // Start up RTOS
    if (ok)
        startRtos(); // never returns
    else
        while(true);
}
