// Tasks
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
#include <stdbool.h>
#include "tm4c123gh6pm.h"
#include "gpio.h"
#include "wait.h"
#include "kernel.h"
#include "tasks.h"
#include "mm.h"

#define BLUE_LED   PORTF,2 // on-board blue LED
#define RED_LED    PORTA,2 // off-board red LED
#define ORANGE_LED PORTA,3 // off-board orange LED
#define YELLOW_LED PORTA,4 // off-board yellow LED
#define GREEN_LED  PORTE,0 // off-board green LED

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

// Initialize Hardware
// REQUIRED: Add initialization for blue, orange, red, green, and yellow LEDs
//           Add initialization for 6 pushbuttons
void initHw(void)
{
    // Setup LEDs and pushbuttons
    enablePort(PORTA);  // output LEDs
    enablePort(PORTC);  // input push buttons
    enablePort(PORTD);  // input push buttons
    enablePort(PORTE);  // output LEDs
    enablePort(PORTF);  // output LEDs

    // Pin D7 is special consideration, need to unlock pin then configure as GPIO use
    GPIO_PORTD_LOCK_R = GPIO_LOCK_KEY;      // unlock port d
    GPIO_PORTD_CR_R = 0x80;                 // allow access to bit/pin 7
    selectPinDigitalInput(PORTD, 7);        // drives red LED
    enablePinPullup(PORTD, 7);              // sets pin high
    GPIO_PORTD_LOCK_R = GPIO_LOCK_M;        // lock port d, pull-up write

    selectPinPushPullOutput(PORTF, 2);  // blue, indicates pendSv fault
    selectPinPushPullOutput(PORTE, 0);  // green, indicates MPU fault
    selectPinPushPullOutput(PORTA, 4);  // yellow, indicates hard fault
    selectPinPushPullOutput(PORTA, 3);  // orange, indicates usage fault
    selectPinPushPullOutput(PORTA, 2);  // red, indicates bus fault
    selectPinPushPullOutput(PORTF, 3);  // green, testing peripheral access

    selectPinDigitalInput(PORTC, 4);    // ?
    selectPinDigitalInput(PORTC, 5);    // drives blue LED
    selectPinDigitalInput(PORTC, 6);    // drives green LED
    selectPinDigitalInput(PORTC, 7);    // drives yellow LED
    selectPinDigitalInput(PORTD, 6);    // drives orange LED

    enablePinPullup(PORTC, 4);
    enablePinPullup(PORTC, 5);
    enablePinPullup(PORTC, 6);
    enablePinPullup(PORTC, 7);
    enablePinPullup(PORTD, 6);

    // Power-up flash
    setPinValue(GREEN_LED, 1);
    waitMicrosecond(250000);
    setPinValue(GREEN_LED, 0);
    waitMicrosecond(250000);

    // enable usage, bus, and memory management faults
    NVIC_CFG_CTRL_R |= NVIC_CFG_CTRL_DIV0;
    NVIC_SYS_HND_CTRL_R |= NVIC_SYS_HND_CTRL_USAGE | NVIC_SYS_HND_CTRL_BUS | NVIC_SYS_HND_CTRL_MEM;
}

// REQUIRED: add code to return a value from 0-63 indicating which of 6 PBs are pressed
uint8_t readPbs(void)
{
    uint8_t pbValue = 0x00;   // none pressed
    if (!getPinValue(PORTC, 4)) pbValue |= 0x01;
    if (!getPinValue(PORTC, 5)) pbValue |= 0x02;
    if (!getPinValue(PORTC, 6)) pbValue |= 0x04;
    if (!getPinValue(PORTC, 7)) pbValue |= 0x08;
    if (!getPinValue(PORTD, 6)) pbValue |= 0x10;
    if (!getPinValue(PORTD, 7)) pbValue |= 0x20;

    return pbValue;
}

// one task must be ready at all times or the scheduler will fail
// the idle task is implemented for this purpose
void idle(void)
{
    while(true)
    {
        setPinValue(ORANGE_LED, 1);
        waitMicrosecond(1000);
        setPinValue(ORANGE_LED, 0);
        yield();
    }
}

void idleDos(void)
{
    while(true)
    {
        setPinValue(GREEN_LED, 1);
        waitMicrosecond(1000);
        setPinValue(GREEN_LED, 0);
        yield();
    }
}

void flash4Hz(void)
{
    while(true)
    {
        setPinValue(GREEN_LED, !getPinValue(GREEN_LED));
        sleep(125);
    }
}

void oneshot(void)
{
    while(true)
    {
        wait(flashReq);
        setPinValue(YELLOW_LED, 1);
        sleep(1000);
        setPinValue(YELLOW_LED, 0);
    }
}

void partOfLengthyFn(void)
{
    // represent some lengthy operation
    waitMicrosecond(990);
    // give another process a chance to run
    yield();
}

void lengthyFn(void)
{
    uint16_t i;
    uint8_t* mem;
    mallocRequest(5000, (void**)&mem);
    //mem = mallocFromHeap(5000 * sizeof(uint8_t));
    while(true)
    {
        lock(resource);
        for (i = 0; i < 5000; i++)
        {
            partOfLengthyFn();
            mem[i] = i % 256;
        }
        setPinValue(RED_LED, !getPinValue(RED_LED));
        unlock(resource);
    }
}

void readKeys(void)
{
    uint8_t buttons;
    while(true)
    {
        wait(keyReleased);
        buttons = 0;
        while (buttons == 0)
        {
            buttons = readPbs();
            yield();
        }
        post(keyPressed);
        if ((buttons & 1) != 0)
        {
            setPinValue(YELLOW_LED, !getPinValue(YELLOW_LED));
            setPinValue(RED_LED, 1);
        }
        if ((buttons & 2) != 0)
        {
            post(flashReq);
            setPinValue(RED_LED, 0);
        }
        if ((buttons & 4) != 0)
        {
            restartThread(flash4Hz);
        }
        if ((buttons & 8) != 0)
        {
            stopThread(flash4Hz);
        }
        if ((buttons & 16) != 0)
        {
            setThreadPriority(lengthyFn, 4);
        }
        yield();
    }
}

void debounce(void)
{
    uint8_t count;
    while(true)
    {
        wait(keyPressed);
        count = 10;
        while (count != 0)
        {
            sleep(10);
            if (readPbs() == 0)
                count--;
            else
                count = 10;
        }
        post(keyReleased);
    }
}

void uncooperative(void)
{
    while(true)
    {
        while (readPbs() == 8)
        {
        }
        yield();
    }
}

void errant(void)
{
    uint32_t* p = (uint32_t*)0x20000000;
    while(true)
    {
        while (readPbs() == 32)
        {
            *p = 0;
        }
        yield();
    }
}

void important(void)
{
    while(true)
    {
        lock(resource);
        setPinValue(BLUE_LED, 1);
        sleep(1000);
        setPinValue(BLUE_LED, 0);
        unlock(resource);
    }
}
