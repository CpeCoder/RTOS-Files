// Fault Handling functions
// Deep Shinglot

//-----------------------------------------------------------------------------
// Hardware Target
//-----------------------------------------------------------------------------

// Target uC:       TM4C123GH6PM
// System Clock:    40 MHz#ifndef FAULT_HANDLERS_H

#ifndef FAULT_HANDLERS_H
#define FAULT_HANDLERS_H

// Function prototypes for ISR handlers
void usageFaultISR();
void busFaultISR();
void hardFaultISR();
void mpuFaultISR();
void pendSvISR();

// Function prototypes for fault triggers
void triggerBusFault();
void triggerUsageFault();
void triggerHardFault();
void triggerMpuFault();
void triggerPendSv();

#endif
