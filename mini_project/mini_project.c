/*
 * Name :   Deep Shinglot
 * ID   :   1001885419
 * Title:   RTOS Mini Project
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "uart0.h"
#include "clock.h"
#include "fault_handlers.h"
#include "asm_src.h"
#include "c_fnc.h"
#include "tm4c123gh6pm.h"

// Bitband aliases
#define RED_LED      (*((volatile uint32_t *)(0x42000000 + (0x400253FC-0x40000000)*32 + 1*4)))
#define GREEN_LED    (*((volatile uint32_t *)(0x42000000 + (0x400253FC-0x40000000)*32 + 3*4)))

#define GREEN_LED_MASK 8
#define RED_LED_MASK 2

#define MAX_CHARS 80
#define MAX_FIELDS 5

// Design specific data
#define HEAP_ADDRESS 0x20001000     // base address for tasks space 28KB
#define MAX_1536B_BLOCK 4
#define MAX_1024B_BLOCK 16
#define MAX_512B_BLOCK 24
#define MAX_SUBREGION 40

// data from UI
typedef struct _USER_DATA
{
    char buffer[MAX_CHARS+1];
    uint8_t fieldCount;
    uint8_t fieldPosition[MAX_FIELDS];
    char fieldType[MAX_FIELDS];
} USER_DATA;

// data of allocated task
typedef struct _TASK_DATA
{
    uint16_t size;
    uint32_t pid;
    uint32_t* heapAddr;
} TASK_DATA;

//***************************************************************************************************************
// 63:56 - Sum of 1536B blocks used of 4-MAX
// 55:48 - Sum of 1024B blocks used of 16-MAX
// 47:40 - Sum of 512B blocks used of 24-MAX
//  39:0 - each bit (0 - not in use, 1 - in use) represents a respected sub-region (0 being region at base addr)
// **************************************************************************************************************
uint64_t subregionUseData = 0;

/**
 * puts null anywhere except num or apha, sets field type and field position
 */
void parseFields(USER_DATA* data)
{
    uint8_t bufferIndex = 0, fieldIndex = 0;
    /**
     * flag changed to TRUE on same continuous data type, on change in data type the flag is set to FLASE
     * allowing different types of operation to follow
     */
    bool flag = 0;
    data->fieldCount = 0;

    // traverse to NULL in buffer str
    while(data->buffer[bufferIndex])
    {
        // check valid num of arguments
        if(data->fieldCount == MAX_FIELDS)
        {
            return;
        }
        bool isCharLower = (data->buffer[bufferIndex] >= 97) && (data->buffer[bufferIndex] <= 122);
        bool isCharUpper = (data->buffer[bufferIndex] >= 65) && (data->buffer[bufferIndex] <= 90);
        // respect to ASCII if lower/upper case aplha then
        if(isCharLower || isCharUpper)
        {
            // for case insensitive make all Upper to Lower case
            if(isCharUpper)
            {
                data->buffer[bufferIndex] += 32;
            }
            if(!flag)
            {
                (data->fieldCount)++;
                data->fieldPosition[fieldIndex] = bufferIndex;
                data->fieldType[fieldIndex] = 97; // 'a'
                flag = 1;
                fieldIndex++;
            }
        }
        else if(data->buffer[bufferIndex] >= 48 && data->buffer[bufferIndex] <= 57)
        {
            if(!flag)
            {
                (data->fieldCount)++;
                data->fieldPosition[fieldIndex] = bufferIndex;
                data->fieldType[fieldIndex] = 'n'; // 110
                flag = 1;
                fieldIndex++;
            }
        }
        else
        {
            flag = 0;
            data->buffer[bufferIndex] = 0;
        }
        bufferIndex++;
    }
}

char* getFieldString(USER_DATA* data, uint8_t fieldNumber)
{
    if((fieldNumber < data->fieldCount ) && (data->fieldType[fieldNumber] == 'a'))
    {
        return &(data->buffer[data->fieldPosition[fieldNumber]]);
    }
    return '\0';
}

int32_t getFieldInteger(USER_DATA* data, uint8_t fieldNumber)
{
    if((fieldNumber < data->fieldCount) && (data->fieldType[fieldNumber] == 'n'))
    {
        int32_t returnNum = 0;
        char* number = &data->buffer[data->fieldPosition[fieldNumber]];
        uint8_t i = 0;
        while(number[i])
        {
            uint8_t num = (number[i]-48);
            if(i)
            {
                returnNum *= 10;
            }
            returnNum += num;
            i++;
        }
        return returnNum;
    }
    return 0;
}

bool isCommand(USER_DATA* data, const char strCommand[], uint8_t minArguments)
{
    if(data->fieldCount >= minArguments+1) // plus 1 because fieldCount-1 for command
    {
        uint8_t i = 0;
        char* buffStr = &(data->buffer[data->fieldPosition[0]]);
        while(strCommand[i] || buffStr[i])
        {
            if(buffStr[i] != strCommand[i])
            {
                return false;
            }
            i++;
        }
        return true;
    }
    return false;
}

void initHw()
{
    // Initialize system clock to 40 MHz
    initSystemClockTo40Mhz();

    // Setup UART0
    initUart0();
    setUart0BaudRate(115200, 40e6);

    // Enable clocks
    SYSCTL_RCGCGPIO_R = SYSCTL_RCGCGPIO_R5;
    _delay_cycles(3);

    // Config LEDS
    GPIO_PORTF_DIR_R |= GREEN_LED_MASK | RED_LED_MASK;  // bits 1 and 3 are outputs
    GPIO_PORTF_DR2R_R |= GREEN_LED_MASK | RED_LED_MASK; // set drive strength to 2mA (not needed since default configuration)
    GPIO_PORTF_DEN_R |= GREEN_LED_MASK | RED_LED_MASK;  // enable LEDs
}

void pkill(const char str[])
{
    putsUart0((char*)str);
    putsUart0(" killed");
    putcUart0('\n');
}

void pidof(const char name[])
{
    putsUart0((char*)name);
    putsUart0(" launched");
    putcUart0('\n');
}

void sched(bool prio_on)
{
    if(prio_on)
    {
        putsUart0("sched prio");
        putcUart0('\n');
    }
    else
    {
        putsUart0("sched rr");
        putcUart0('\n');
    }
}

void preempt(bool on)
{
    if(on)
    {
        putsUart0("preempt on");
        putcUart0('\n');
    }
    else
    {
        putsUart0("preempt off");
        putcUart0('\n');
    }
}

void pi(bool on)
{
    if(on)
    {
        putsUart0("pi on");
        putcUart0('\n');
    }
    else
    {
        putsUart0("pi off");
        putcUart0('\n');
    }
}

void kill(uint32_t pid)
{
    char pidStr[MAX_CHARS+1];
    numToStr(pid, pidStr);
    putsUart0(pidStr);
    putsUart0(" killed");
    putcUart0('\n');
}

void ipcs(void)
{
    putsUart0("IPCS called");
    putcUart0('\n');
}

void ps(void)
{
    putsUart0("PS called");
    putcUart0('\n');
}

void yeild(void)
{
    return;
}

void shell(void)
{
    char c;
    bool end;
    uint8_t count = 0;
    USER_DATA data;

    while(true)
    {
        if(kbhitUart0())        // if UART FIFO is full
        {
            c = getcUart0();
            end = (c == 13) || (count == MAX_CHARS);    // 13 is RETURN/ENTER
            if (!end)
            {
                if ((c == 8 || c == 127) && count > 0)  // BACKSPACE or DELETE
                {
                    count--;
                }
                if (c >= 32 && c < 127)    // SPACE (' ') is 32
                {
                    data.buffer[count++] = c;
                }
            }
            else
            {
                data.buffer[count] = '\0';
                count = 0;
                parseFields(&data);
                if(isCommand(&data, "reboot", 0))
                {
                    NVIC_APINT_R = NVIC_APINT_VECTKEY | NVIC_APINT_SYSRESETREQ;
                }
                else if(isCommand(&data, "ps", 0))
                {
                    ps();
                }
                else if(isCommand(&data, "ipcs", 0))
                {
                    ipcs();
                }
                else if(isCommand(&data, "kill", 1))
                {
                    uint32_t pid = getFieldInteger(&data, 1);
                    kill(pid);
                }
                else if(isCommand(&data, "pkill", 1))
                {
                    pkill(getFieldString(&data, 1));
                }
                else if(isCommand(&data, "pi", 1))
                {
                    bool on = strCmp(getFieldString(&data, 1), "on");
                    pi(on);
                }
                else if(isCommand(&data, "preempt", 1))
                {
                    bool on = strCmp(getFieldString(&data, 1), "on");
                    preempt(on);
                }
                else if(isCommand(&data, "sched", 1))
                {
                    bool prio_on = strCmp(getFieldString(&data, 1), "prio");
                    sched(prio_on);
                }
                else if(isCommand(&data, "pidof", 1))
                {
                    pidof(getFieldString(&data, 1));
                }
                else
                {
                    char procArr[2][10] = {"idle", "flash4hz"};
                    uint8_t i = 0;
                    for(i = 0; i < 2; i++)
                    {
                        if(isCommand(&data, procArr[i], 0))
                        {
                            RED_LED ^= 1;
                        }
                    }
                }
            }
        }
        yeild();
    }
}

void calculateBlockRequired(uint32_t requestedSize, uint8_t* blockCount1024, uint8_t* blockCount512)
{
    *blockCount1024 = 0;
    *blockCount512 = 0;
    *blockCount1024 = requestedSize / 1024;        // # of 1024Bblocks are needed
    uint32_t remainingSize = requestedSize % 1024; // remaining size

    // If there's any remaining size, calculate the number of 512-byte blocks needed
    if (remainingSize > 0)
    {
        if (remainingSize > 512)
        {
            *blockCount1024 += 1; // if remains more than 512, allocate one more 1024B block
        }
        else
        {
            *blockCount512 = 1; // allocate 512B block for the remainder
        }
    }
}

bool findConsecutiveSpace()
{
    bool found = 0;
}

void* malloc_from_heap(uint32_t sizeInBytes)
{
    uint8_t* inUse1536 = (uint8_t*)(&subregionUseData) + 7; // point to 63:56 of inUse
    uint8_t* inUse1024 = (uint8_t*)(&subregionUseData) + 6; // point to 55:48 of inUse
    uint8_t* inUse512  = (uint8_t*)(&subregionUseData) + 5; // point to 47:40 of inUse

    uint8_t needed1024Blocks, needed512Blocks;
    calculateBlockRequired(sizeInBytes, &needed1024Blocks, &needed512Blocks);

    if((needed1024Blocks != 0) || (needed512Blocks != 0))
    {
        void* addrPtr;
        if((needed1024Blocks != 0) && (needed512Blocks != 0)) // 1536B multiple
        {

        }
        else if(needed1024Blocks != 0) // 1024B multiple
        {

        }
        else if(needed512Blocks != 0) // 512B multiple
        {

        }
    }
    else    // sizeInBytes = 0
    {
        return NULL;
    }
}

/**
 * mini_project.c
 */
int main(void)
{
    // initialize hardware
    initHw();
/*
    // temporary PSP assigned
    uint32_t* pspPtr = (uint32_t*)PSP_ADDRESS;
    setPsp(pspPtr);
    // set ASP bit in CONTROL register
    setControlReg();
    // trigger bus fault
    triggerBusFault();
    // trigger usage fault
    triggerUsageFault();
    // trigger hard fault
    triggerHardFault();
    // trigger Memory Protection Unit Fault (MPU fault)
    triggerMpuFault();
    // UI in loop
*/

    shell();
    return 0;
}
