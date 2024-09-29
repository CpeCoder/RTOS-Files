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

void* malloc_from_heap(uint32_t sizeInBytes);
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

void pkill(char str[])
{
    putsUart0((char*)str);
    putsUart0(" killed");
    putcUart0('\n');
}

void pidof(char name[])
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
                else if(isCommand(&data, "malloc", 1))
                {
                    uint32_t size = getFieldInteger(&data, 1);
                    uint32_t addr = (uint32_t) malloc_from_heap(size);
                    char addrStr[10];
                    uint32ToHexString(&addr, addrStr);
                    putsUart0(addrStr);
                    putcUart0('\n');
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

void getMallocAddr(uint32_t* regionAddr, uint32_t* subregionAddr, uint8_t allocatedIndex)
{
    // region ladder, using if else-if for simplicity
    if(allocatedIndex < 8)
    {
        *regionAddr = 0x20001000;
        *subregionAddr = (allocatedIndex%8) * 512;
    }
    else if(allocatedIndex < 16)
    {
        *regionAddr = 0x20002000;
        *subregionAddr = (allocatedIndex%8) * 1024;
    }
    else if(allocatedIndex < 24)
    {
        *regionAddr = 0x20004000;
        *subregionAddr = (allocatedIndex%8) * 512;
    }
    else if(allocatedIndex < 32)
    {
        *regionAddr = 0x20005000;
        *subregionAddr = (allocatedIndex%8) * 1024;
    }
    else if(allocatedIndex < 40)
    {
        *regionAddr = 0x20007000;
        *subregionAddr = (allocatedIndex%8) * 512;
    }
}

void calculateBlockRequired(uint32_t requestedSize, uint8_t* blockCount1024, uint8_t* blockCount512)
{
    *blockCount1024 = 0;
    *blockCount512 = 0;
    *blockCount1024 = requestedSize / 1024;        // # of 1024Bblocks are needed
    uint32_t remainingSize = requestedSize % 1024; // remaining size

    // If there's any remaining size, calculate the number of 512-byte blocks needed
    if(remainingSize > 0)
    {
        // if more than 1536B allocate multiple 1024B blocks, else do the special case
        if(remainingSize > 512)
        {
            *blockCount1024 += 1; // if remains more than 512, allocate one more 1024B block
        }
        else
        {
            if(*blockCount1024 > 1)
            {
                *blockCount1024 += 1;
            }
            else
            {
                *blockCount512 = 1; // allocate 512B block for the remainder
            }
        }
    }
}

/* Fnc runs First Fit Algorithm to find space in specified */
/* returns allocatedIndex and true if space found, takes in needed 512B and 1024B blocks */
bool findConsecutiveSpace(uint8_t* allocatedIndex, uint8_t* needed1024Blocks, uint8_t* needed512Blocks)
{
    bool found = 0;
    uint8_t* inUse1536 = (uint8_t*)(&subregionUseData) + 7; // 63:56 of inUse chances
    uint8_t* inUse1024 = (uint8_t*)(&subregionUseData) + 6; // 55:48 of inUse blocks
    uint8_t* inUse512  = (uint8_t*)(&subregionUseData) + 5; // 47:40 of inUse blocks
    uint8_t* block3InUse4KB = (uint8_t*)(&subregionUseData) + 4; //   |  4 KB  |   ' 0x20008000
    uint8_t* block2InUse8KB = (uint8_t*)(&subregionUseData) + 3; //   |  8 KB  |   '
    uint8_t* block2InUse4KB = (uint8_t*)(&subregionUseData) + 2; //   |  4 KB  |   '
    uint8_t* block1InUse8KB = (uint8_t*)(&subregionUseData) + 1; //   |  8 KB  |   '
    uint8_t* block1InUse4KB = (uint8_t*)(&subregionUseData);     //   |  4 KB  |   v 0x20001000

    uint8_t i;
    if((*needed1024Blocks == 1) && (*needed512Blocks == 1)) // 1536B multiple, special case
    {
        for(i = 1; i < 5; i++)
        {
            // region on edge of multiple of 8 and 4KB 8KB block alternates, so check one up and down
            bool lowerRegionEdge = (bool)(subregionUseData & (1 << ((i*8) - 1)));
            bool upperRegionEdge = (bool)(subregionUseData & (1 << ((i*8)) ) );
            if(!lowerRegionEdge && !upperRegionEdge)
            {
                // space found, return allocatedIndex and found
                found = 1;
                (*inUse512)++;
                (*inUse1024)++;
                (*inUse1536)++;
                subregionUseData |= 3 << ((i*8) - 1);     // marks upper and lower as 'in use'
                *allocatedIndex = (i*8)-1;  // assignment of lowerRegion in terms of index
                return found;
            }
        }
        return found;
    }
    else if(*needed1024Blocks != 0) // 1024B multiple
    {
        for(i = 0; i < 2; i++)  // 2 8KB blocks
        {
            uint8_t* currentBlock8kb = (i == 0) ? block1InUse8KB : block2InUse8KB;
            uint8_t j, count1024B = 0;
            for(j = 0; j < 8; j++)  // 8 sub-regions in one 8KB block
            {
                // saving edge sub-regions if less or equal to 6KB
                if((*needed1024Blocks <= 6) && (j==0 || j==7)) continue;

                if(!(*currentBlock8kb & (1 << j)))  // for detecting continuity
                {
                    count1024B++;
                }
                else    // reset if occupied
                {
                    count1024B = 0;
                }

                if(count1024B == *needed1024Blocks) // found space
                {
                    found = 1;
                    (*inUse1024)++;
                    int8_t k, startIndex = j - *needed1024Blocks + 1;
                    for(k = j; k >= startIndex; k--)
                    {
                        // mark occupied
                        *currentBlock8kb |= (1 << k);
                        // depending on which block, return the index
                    }
                    *allocatedIndex = (i == 0) ? (8 + startIndex) : (24 + startIndex);
                    return found;
                }
            }
        }
        // space not found for 6KB or less needed, check with edge sub-regions
        if(!found && (*needed1024Blocks <= 6))
        {
            for(i = 0; i < 2; i++)  // 2 8KB blocks
            {
                uint8_t* currentBlock8kb = (i == 0) ? block1InUse8KB : block2InUse8KB;
                uint8_t j, count1024B = 0;
                for(j = 0; j < 8; j++)  // 8 sub-regions in one 8KB block
                {
                    if(!(*currentBlock8kb & (1 << j)))  // for detecting continuity
                    {
                        count1024B++;
                    }
                    else    // reset if occupied
                    {
                        count1024B = 0;
                    }

                    if(count1024B == *needed1024Blocks) // found space
                    {
                        (*inUse1024)++;
                        found = 1;
                        int8_t k, startIndex = j - *needed1024Blocks + 1;
                        for(k = j; k >= startIndex; k--)
                        {
                            // mark occupied
                            *currentBlock8kb |= (1 << k);
                            // depending on which block, return the index
                        }
                        *allocatedIndex = (i == 0) ? (8 + startIndex) : (24 + startIndex);
                        return found;
                    }
                }
            }
        }
    }
    else if(*needed512Blocks != 0) // 512B multiple
    {
        for(i = 0; i < 3; i++)  // 2 8KB blocks
        {
            // iterate through 4KB blocks
            uint8_t* currentBlock4kb = (i == 0) ? block1InUse4KB :
                                        (i == 1) ? block2InUse4KB : block3InUse4KB;
            uint8_t j, count512B = 0;
            for(j = 0; j < 8; j++)  // 8 sub-regions in one 4KB block
            {
                // saving edge sub-regions if less or equal to 3KB
                if((*needed512Blocks <= 6) && (j==0) && (i==2)) continue;
                if((*needed512Blocks <= 6) && (j==0 || j==7) && (i==1)) continue;
                if((*needed512Blocks <= 6) && (j==7) && (i==0)) continue;

                if(!(*currentBlock4kb & (1 << j)))  // for detecting continuity
                {
                    count512B++;
                }
                else    // reset if occupied
                {
                    count512B = 0;
                }

                if(count512B == *needed512Blocks) // found space
                {
                    found = 1;
                    (*inUse512)++;
                    int8_t k, startIndex = j - *needed512Blocks + 1;
                    for(k = j; k >= startIndex; k--)
                    {
                        // mark occupied
                        *currentBlock4kb |= (1 << k);
                        // depending on which block, return the index
                    }
                    *allocatedIndex = (i*16) + startIndex;
                    return found;
                }
            }
        }
        // space not found for 3KB or less needed, check with edge sub-regions
        if(!found && (*needed1024Blocks <= 6))
        {
            for(i = 0; i < 3; i++)  // 2 8KB blocks
            {
                // iterate through 4KB blocks
                uint8_t* currentBlock4kb = (i == 0) ? block1InUse4KB :
                                            (i == 1) ? block2InUse4KB : block3InUse4KB;
                uint8_t j, count512B = 0;
                for(j = 0; j < 8; j++)  // 8 sub-regions in one 4KB block
                {
                    if(!(*currentBlock4kb & (1 << j)))  // for detecting continuity
                    {
                        count512B++;
                    }
                    else    // reset if occupied
                    {
                        count512B = 0;
                    }

                    if(count512B == *needed512Blocks) // found space
                    {
                        found = 1;
                        (*inUse512)++;
                        int8_t k, startIndex = j - *needed512Blocks + 1;
                        for(k = j; k >= startIndex; k--)
                        {
                            // mark occupied
                            *currentBlock4kb |= (1 << k);
                            // depending on which block, return the index
                        }
                        *allocatedIndex = (i*16) + startIndex;
                        return found;
                    }
                }
            }
        }
    }
    return found;
}

void* malloc_from_heap(uint32_t sizeInBytes)
{
    uint8_t* inUse1536 = (uint8_t*)(&subregionUseData) + 7; // 63:56 of inUse chances
    uint8_t* inUse1024 = (uint8_t*)(&subregionUseData) + 6; // 55:48 of inUse blocks
    uint8_t* inUse512  = (uint8_t*)(&subregionUseData) + 5; // 47:40 of inUse blocks

    bool ok = 0;
    uint8_t needed1024Blocks, needed512Blocks;
    calculateBlockRequired(sizeInBytes, &needed1024Blocks, &needed512Blocks);

    uint8_t totalSpaceNeeded = needed1024Blocks*2 + needed512Blocks; // Space respect to 512B block
    uint8_t block1024Avaliable = MAX_1024B_BLOCK - (*inUse1024);
    uint8_t block512Avaliable = MAX_512B_BLOCK - (*inUse512);

    //
    if((totalSpaceNeeded <= (block1024Avaliable*2 + block512Avaliable)) && totalSpaceNeeded > 16)
    {
        return NULL;    // all blocks used
    }

    if((needed1024Blocks > block1024Avaliable) && (totalSpaceNeeded <= block512Avaliable))
    {
        needed512Blocks = totalSpaceNeeded;     // not enough 1024B, assign multiple 512B
        needed1024Blocks = 0;                   // transfered to 512B block
    }
    else if((needed512Blocks > block512Avaliable) && ((needed1024Blocks + 1) <= block1024Avaliable))
    {
        needed512Blocks = 0;                // transfered to 1024B block
        needed1024Blocks += 1;              // Calculating fnc returns max 1 512B block
    }

    void* addrPtr;
    uint8_t allocatedIndex = 0;
    if((needed1024Blocks == 1) && (needed512Blocks == 1)) // 1536B needed, special case
    {
        if(*inUse1536 < MAX_1536B_BLOCK)
        {
            ok = findConsecutiveSpace(&allocatedIndex, &needed1024Blocks, &needed512Blocks);
        }
        if(!ok)     // space not found
        {
            needed1024Blocks = 0;
            needed512Blocks = 3;    // on unavailability of edge regions, allocate multiple 512B
            ok = findConsecutiveSpace(&allocatedIndex, &needed1024Blocks, &needed512Blocks);
        }
        if(!ok)     // space not found
        {
            needed1024Blocks = 2;   // allocate multiple 1024B, waste space
            needed512Blocks = 0;
            ok = findConsecutiveSpace(&allocatedIndex, &needed1024Blocks, &needed512Blocks);
        }
    }
    else
    {
        if(needed1024Blocks != 0)       // multiple of 1024B
        {
            ok = findConsecutiveSpace(&allocatedIndex, &needed1024Blocks, &needed512Blocks);
            if(!ok)     // unavailability on 8KB block
            {
                needed512Blocks = totalSpaceNeeded;    // allocate multiples of 512B
                needed1024Blocks = 0;
                ok = findConsecutiveSpace(&allocatedIndex, &needed1024Blocks, &needed512Blocks);
            }
        }
        if(needed512Blocks != 0)        // multiple of 512B
        {
            ok = findConsecutiveSpace(&allocatedIndex, &needed1024Blocks, &needed512Blocks);
            if(!ok)     // unavailability on 4KB block
            {
                needed512Blocks = 0;
                needed1024Blocks = (totalSpaceNeeded % 2) ? (totalSpaceNeeded/2) + 1 :
                                    totalSpaceNeeded/2;  // allocate multiples of 1024B
                ok = findConsecutiveSpace(&allocatedIndex, &needed1024Blocks, &needed512Blocks);
            }
        }
    }

    if(ok)
    {
        uint32_t regionAddr, subregionAddr;
        getMallocAddr(&regionAddr, &subregionAddr, allocatedIndex);
        addrPtr = (void*)(regionAddr + subregionAddr);
        return addrPtr;
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
