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
#include "gpio.h"
#include "nvic.h"
#include "tm4c123gh6pm.h"

#define GREEN_LED_MASK 8
#define RED_LED_MASK 2

#define MAX_CHARS 80
#define MAX_FIELDS 5

// Design specific data
#define HEAP_ADDRESS 0x20001000     // base address for tasks space 28KB
#define MAX_1536B_BLOCK 3
#define MAX_1024B_BLOCK 16
#define MAX_512B_BLOCK 24
#define MAX_SUBREGION 40
#define PSP_ADDRESS 0x20000500

// data from UI
typedef struct _USER_DATA
{
    char buffer[MAX_CHARS+1];
    uint8_t fieldCount;
    uint8_t fieldPosition[MAX_FIELDS];
    char fieldType[MAX_FIELDS];
} USER_DATA;

// data of allocated task
typedef struct _MALLOC_DATA
{
    bool inUse;
    uint16_t pid;
    uint32_t size;
    void* heapAddr;
} MALLOC_DATA;

//***************************************************************************************************************
// 63:56 - Sum of 1536B blocks used of 4-MAX
// 55:48 - Sum of 1024B blocks used of 16-MAX
// 47:40 - Sum of 512B blocks used of 24-MAX
//  39:0 - each bit (0 - not in use, 1 - in use) represents a respected sub-region (0 being region at base addr)
// **************************************************************************************************************
uint64_t subregionUseData = 0;
uint8_t activePid = 0;

MALLOC_DATA allocatedData[14] = {0};

void shell(void);
void mallocTesting1(void);
void mallocTesting2(void);
void mallocTesting3(void);
void freeTesting(void);

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
    // taking out alpha check for user friendly, if user wants num input as string
        // because input does not have to be in decimals
    if(fieldNumber < data->fieldCount)
    {
        return &(data->buffer[data->fieldPosition[fieldNumber]]);
    }
    return '\0';
}

int32_t getFieldInteger(USER_DATA* data, uint8_t fieldNumber)   // string to base 10
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

    //selectPinDigitalInput(PORTC, 4);    // ?
    selectPinDigitalInput(PORTC, 5);    // drives blue LED
    selectPinDigitalInput(PORTC, 6);    // drives green LED
    selectPinDigitalInput(PORTC, 7);    // drives yellow LED
    selectPinDigitalInput(PORTD, 6);    // drives orange LED


    enablePinPullup(PORTC, 5);
    enablePinPullup(PORTC, 6);
    enablePinPullup(PORTC, 7);
    enablePinPullup(PORTD, 6);

    // enable bus, usage, and memory management fault faults
    NVIC_SYS_HND_CTRL_R |= NVIC_SYS_HND_CTRL_BUS | NVIC_SYS_HND_CTRL_USAGE | NVIC_SYS_HND_CTRL_MEM;
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

uint64_t createNoSramAccessMask()
{
    // each byte presents a region and each bit a sub-region...0xFF disables sub-region
        // lsb presents lowest sub-region
    return 0xFFFFFFFFFFFFFFFF;
}

void applySramAccessMask(uint64_t srdBitMask)
{
    uint8_t i, j=0;
    // 0 and 1 region used for flash and peripherals, respectively
        // 2 is OS Kernel....3-7 follows 4KB -> 8KB -> 4KB -> 4KB -> 8KB
    for(i = 2; i < 8; i++)      // 6 iterations
    {
        uint8_t regionSrdMask = (uint8_t)(srdBitMask >> (j*8));
        NVIC_MPU_NUMBER_R = i;
        // disable region before updating
        NVIC_MPU_ATTR_R &= ~NVIC_MPU_ATTR_ENABLE;
        // clear and assign srd mask to each sub-regions bit
        NVIC_MPU_ATTR_R &= 0xFFFF00FF;
        NVIC_MPU_ATTR_R |= regionSrdMask << 8;
        // enable region after updating
        NVIC_MPU_ATTR_R |= NVIC_MPU_ATTR_ENABLE;
        ++j;
    }
}

uint8_t calculateIndex(uint32_t* baseAddrValue, uint16_t* subregionSize)
{
    uint32_t region = *baseAddrValue & 0xFFFFF000;
    uint16_t subregion = *baseAddrValue & 0x00000FFF;
    uint8_t index = 0;

    switch (region)
    {
        case 0x20000000: // Region 1 : 4KB
            *subregionSize = 512;
            index = 0 + (subregion / *subregionSize);
            break;
        case 0x20001000: // Region 1 : 4KB
            *subregionSize = 512;
            index = 8 + (subregion / *subregionSize);
            break;
        case 0x20002000: // Region 2 : 8KB
            *subregionSize = 1024;
            index = 16 + (subregion / *subregionSize);
            break;
        case 0x20003000: // Region 2 : 8KB
            *subregionSize = 1024;
            index = 20 + (subregion / *subregionSize);
            break;
        case 0x20004000: // Region 3 : 4KB
            *subregionSize = 512;
            index = 24 + (subregion / *subregionSize);
            break;
        case 0x20005000: // Region 4 : 4KB
            *subregionSize = 512;
            index = 32 + (subregion / *subregionSize);
            break;
        case 0x20006000: // Region 5 : 8KB
            *subregionSize = 1024;
            index = 40 + (subregion / *subregionSize);
            break;
        case 0x20007000: // Region 5 : 8KB
            *subregionSize = 1024;
            index = 44 + (subregion / *subregionSize);
            break;
        case 0x20008000: // access to whole SRAM
            index = 48;
        default:
            break;
    }
    return index;
}

void addSramAccessWindow(uint64_t *srdBitMask, uint32_t *baseAdd, uint32_t sizeInBytes)
{
    uint32_t baseAddrValue = (uint32_t)(baseAdd);
    uint16_t subregionSize;
    uint8_t startIndex, endIndex;

    startIndex = calculateIndex(&baseAddrValue, &subregionSize);
    baseAddrValue += sizeInBytes;
    if(baseAddrValue > 0x20008000)  // out of SRAM region
    {
        return;
    }
    endIndex = calculateIndex(&baseAddrValue, &subregionSize);

    (*srdBitMask) &= ~((((uint64_t)1 << (endIndex - startIndex)) - 1) << startIndex);
}

bool checkOwnership(uint32_t* baseAddr, uint32_t* sizeInBytes, bool isCallFromKill)     // add pid parameter later
{
    uint8_t i;
    if(isCallFromKill)
    {
        for(i = 0; i < 14; i++)
        {
            if(!allocatedData[i].inUse && (activePid == allocatedData[i].pid))
            {
                allocatedData[i].inUse = 0;     // mark available
                *sizeInBytes = allocatedData[i].size;
                return 1;   // found match
            }
        }
    }
    else
    {
        for(i = 0; i < 14; i++)
        {
            uint32_t heapAddrValue = (uint32_t)(allocatedData[i].heapAddr);
            uint32_t baseAddrValue = (uint32_t)(baseAddr);
            if(allocatedData[i].inUse && (baseAddrValue == heapAddrValue) && (activePid == allocatedData[i].pid))
            {
                allocatedData[i].inUse = 0;     // mark available
                *sizeInBytes = allocatedData[i].size;
                return 1;   // found match
            }
        }
    }
    return 0;
}

void freeFromHeap(uint32_t* baseAdd, uint32_t sizeInBytes)
{
    uint8_t* inUse1536 = (uint8_t*)(&subregionUseData) + 7; // 63:56 of inUse chances
    uint8_t* inUse1024 = (uint8_t*)(&subregionUseData) + 6; // 55:48 of inUse blocks
    uint8_t* inUse512  = (uint8_t*)(&subregionUseData) + 5; // 47:40 of inUse blocks

    uint32_t baseAddrValue = (uint32_t)(baseAdd);
    uint16_t startSubregionSize, endSubregionSize;
    uint8_t startIndex = calculateIndex(&baseAddrValue, &startSubregionSize);
    startIndex -= 8;    // heap don't include OS kernel
    baseAddrValue += sizeInBytes-1;
    // need to the correct sub-region size by sending in region address
    uint8_t endIndex = calculateIndex(&baseAddrValue, &endSubregionSize);
    endIndex -= 8 - 1;      // need to include for bit shifts
    subregionUseData &= ~((((uint64_t)1 << (endIndex - startIndex)) - 1) << startIndex);

    // change inUse blocks
    if((startSubregionSize == 512 && endSubregionSize == 1024) || (startSubregionSize == 1024 && endSubregionSize == 512))
    {
        *inUse512 -= 1;
        *inUse1024 -= 1;
        *inUse1536 -= 1;
    }
    else
    {
        *(startSubregionSize == 512 ? inUse512 : inUse1024) -= sizeInBytes / startSubregionSize;
    }
}

void addAllocation(uint16_t pid, uint32_t size, void* heapAddr)
{
    uint8_t i;
    for(i = 0; i < 14; i++)
    {
        if(!allocatedData[i].inUse)
        {
            allocatedData[i].inUse = 1;
            allocatedData[i].pid = pid;
            allocatedData[i].size = size;
            allocatedData[i].heapAddr = heapAddr;
            return;
        }
    }
}

void getMallocAddr(uint16_t* subregion, uint16_t* region, uint8_t allocatedIndex)
{
    *region = ((allocatedIndex / 8) * 0x1000);
    if (allocatedIndex >= 16)
    {
        *region += 0x1000;  // skips 0x20003000 by adding
    }

    // sub-region address based on the block
    if (allocatedIndex < 8 || (allocatedIndex >= 16 && allocatedIndex < 32))
    {
        *subregion = (allocatedIndex % 8) * 512;
    }
    else
    {
        *subregion = (allocatedIndex % 8) * 1024;
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

/* Function runs First Fit Algorithm to find space in specified block type, however space optimized */
/* returns allocatedIndex and true if space found, takes in needed 512B and 1024B blocks */
bool findConsecutiveSpace(uint8_t* allocatedIndex, uint8_t* needed1024Blocks, uint8_t* needed512Blocks)
{
    bool found = 0;
    uint8_t* inUse1536 = (uint8_t*)(&subregionUseData) + 7; // 63:56 of inUse chances
    uint8_t* inUse1024 = (uint8_t*)(&subregionUseData) + 6; // 55:48 of inUse blocks
    uint8_t* inUse512  = (uint8_t*)(&subregionUseData) + 5; // 47:40 of inUse blocks
    uint8_t* block2InUse8KB = (uint8_t*)(&subregionUseData) + 4; //   |  8 KB  |   ' 0x20008000
    uint8_t* block3InUse4KB = (uint8_t*)(&subregionUseData) + 3; //   |  4 KB  |   '
    uint8_t* block2InUse4KB = (uint8_t*)(&subregionUseData) + 2; //   |  4 KB  |   '
    uint8_t* block1InUse8KB = (uint8_t*)(&subregionUseData) + 1; //   |  8 KB  |   '
    uint8_t* block1InUse4KB = (uint8_t*)(&subregionUseData);     //   |  4 KB  |   v 0x20001000

    uint8_t i;
    if((*needed1024Blocks == 1) && (*needed512Blocks == 1)) // 1536B multiple, special case
    {
        for(i = 1; i < 4; i++)
        {
            // region on edge of multiple of 8 and 4KB 8KB block alternates, so check one up and down
            bool lowerRegionEdge = (bool) ((i == 1 || i == 2) ? (subregionUseData & (1 << ((i*8) - 1))) : (subregionUseData & ((uint64_t)1 << 31)));
            bool upperRegionEdge = (bool) ((i == 1 || i == 2) ? (subregionUseData & (1 << (i*8))) : (subregionUseData & ((uint64_t)1 << 32)));
            if(!lowerRegionEdge && !upperRegionEdge)
            {
                // space found, return allocatedIndex and found
                found = 1;
                (*inUse512)++;
                (*inUse1024)++;
                (*inUse1536)++;
                subregionUseData |= (i == 1 || i == 2) ? (3 << ((i*8) - 1)) : ((uint64_t)3 << 31);     // marks upper and lower as 'in use'
                *allocatedIndex = (i == 1 || i == 2) ? ((i*8)-1) : (31);  // assignment of lowerRegion in terms of index
                return found;
            }
        }
        return found;
    }
    else if(*needed1024Blocks != 0) // 1024B multiple
    {
        uint8_t tryWithoutEdge; // for preserving edges for 1536
        for(tryWithoutEdge = 0; tryWithoutEdge < 2; tryWithoutEdge++)
        {
            // if checked once for space greater than 6 which is with edge sub-regions so there no space
            if((tryWithoutEdge == 1) && (*needed512Blocks > 6))
            {
                return found;
            }
            for(i = 0; i < 2; i++)  // 2 8KB blocks
            {
                // only save edge sub-region in 8KB if 4KB edge is not used, shifting hard-coded by design
                bool lower4kbRegionEdge = (bool)((i == 0) ? (subregionUseData & (1 << 7)) : (subregionUseData & ((uint64_t)1 << 31)));
                bool upper4kbRegionEdge = (bool)((i == 0) ? (subregionUseData & (1 << 16)) : 0);    // second 8KB no upper edge

                uint8_t* currentBlock8kb = (i == 0) ? block1InUse8KB : block2InUse8KB;
                uint8_t j, count1024B = 0;
                for(j = 0; j < 8; j++)  // 8 sub-regions in one 8KB block
                {
                    // saving edge sub-regions if less or equal to 6KB
                        // when tryWithoutEdge = 1 space not found for 6KB or less needed, check with edge sub-regions (if not executes)
                    if(!tryWithoutEdge && (*needed1024Blocks <= 6))
                    {
                        if((j==0) && !lower4kbRegionEdge) continue;
                        else if((j==7) && !upper4kbRegionEdge) continue;
                    }

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
                        int8_t k, startIndex = j - *needed1024Blocks + 1;
                        for(k = j; k >= startIndex; k--)
                        {
                            (*inUse1024)++;
                            // mark occupied
                            *currentBlock8kb |= (1 << k);
                        }
                        // depending on which block, return the index
                        *allocatedIndex = (i == 0) ? (8 + startIndex) : (32 + startIndex);
                        return found;
                    }
                }
            }
        }
    }
    else if(*needed512Blocks != 0) // 512B multiple
    {
        uint8_t tryWithoutEdge; // for preserving edges for 1536
        for(tryWithoutEdge = 0; tryWithoutEdge < 2; tryWithoutEdge++)
        {
            // if checked once for space greater than 6 which is with edge sub-regions so there no space
            if((tryWithoutEdge == 1) && (*needed512Blocks > 6))
            {
                return found;
            }
            for(i = 0; i < 3; i++)  // 3 4KB blocks
            {
                // only save edge sub-region in 4KB if 8KB edge is not used, shifting hard-coded by design
                bool lower8kbRegionEdge = (bool) (subregionUseData & (1 << 15));
                bool upper8kbRegionEdge = (bool)((i == 0) ? (subregionUseData & (1 << 8)) : (subregionUseData & ((uint64_t)1 << 32)));

                // iterate through 4KB blocks
                uint8_t* currentBlock4kb = (i == 0) ? block1InUse4KB :
                                            (i == 1) ? block2InUse4KB : block3InUse4KB;
                uint8_t j, count512B = 0;
                for(j = 0; j < 8; j++)  // 8 sub-regions in one 4KB block
                {
                    // saving edge sub-regions if less or equal to 3KB, design specific
                        // when tryWithoutEdge = 1 space not found for 3KB or less needed, check with edge sub-regions
                    if(!tryWithoutEdge && (*needed512Blocks <= 6))
                    {
                        if((j==7) && (i==2) && !upper8kbRegionEdge) continue;
                        else if((j==0) && (i==1) && !lower8kbRegionEdge) continue;
                        else if((j==7) && (i==0) && !upper8kbRegionEdge) continue;
                    }

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
                        int8_t k, startIndex = j - *needed512Blocks + 1;
                        for(k = j; k >= startIndex; k--)
                        {
                            (*inUse512)++;
                            // mark occupied
                            *currentBlock4kb |= (1 << k);
                        }
                        // depending on which block, return the index
                        *allocatedIndex = (i == 0 || i == 1) ? ((i*16) + startIndex) : (24 + startIndex);
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

    // max allocation limit to 8KB
    if((totalSpaceNeeded > (block1024Avaliable*2 + block512Avaliable)) || totalSpaceNeeded > 16)
    {
        return NULL;    // all blocks used
    }

    // confirms block availability, not the consecutiveness so many if's in next block
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
        else if(needed512Blocks != 0)        // multiple of 512B
        {
            ok = findConsecutiveSpace(&allocatedIndex, &needed1024Blocks, &needed512Blocks);
            if(!ok)     // unavailability on 4KB block
            {
                needed512Blocks = 0;
                needed1024Blocks = (totalSpaceNeeded % 2) ? (totalSpaceNeeded / 2) + 1 :
                                    (totalSpaceNeeded / 2);  // allocate multiples of 1024B
                ok = findConsecutiveSpace(&allocatedIndex, &needed1024Blocks, &needed512Blocks);
            }
        }
    }

    if(ok)
    {
        uint16_t subregionAddr, regionAddr;
        getMallocAddr(&subregionAddr, &regionAddr, allocatedIndex);
        addrPtr = (void*)(HEAP_ADDRESS + regionAddr + subregionAddr);
        addAllocation(activePid, totalSpaceNeeded * 512, addrPtr);
        return addrPtr;
    }
    else    // sizeInBytes = 0
    {
        return NULL;
    }
}



void allowFlashAccess()
{
    // set region number 0
    NVIC_MPU_NUMBER_R = 0x0;
    // set region base address (N=log2(Size)) and let it use MPUNUMBER (0<<4)
        // region 0 : 0x00000000 - 0x00040000 , 18 = log2(256KB)
    NVIC_MPU_BASE_R = (0x00 << 18) | (0 << 4) | (0 << 0);
    // set region to allow processor to fetch in exception, +r+w+x privilege and +r-w+x user,
        // (tex-s-c-b) see pg.130, all sub-regions enable, size encoding pg.92 (N-1), enable the region
    NVIC_MPU_ATTR_R = (0 << 28) | (0b010 << 24) | (0b000 << 19) | (0 << 18) | (1 << 17) | (0 << 16) |
                        (0x00 << 8) | (0x11 << 1) | NVIC_MPU_ATTR_ENABLE;
}

void allowPeripheralAccess()
{
    // set region number (0 - 7)
    NVIC_MPU_NUMBER_R = 0x1;
    // set region base address (N=log2(Size)) and let it use MPUNUMBER (0<<4)
        // region 0 : 0x40000000 - 0x5FFFFFFF , 28 = log2(512MB)
    NVIC_MPU_BASE_R = (0x4 << 28) | (0 << 4) | (0 << 0);
    // set region to NOT allow processor to fetch in exception, +r+w-x both user and privilege,
        // (tex-s-c-b) see pg.130, all sub-regions enable, size encoding pg.92 (N-1), enable the region
    NVIC_MPU_ATTR_R = (1 << 28) | (0b011 << 24) | (0b000 << 19) | (1 << 18) | (0 << 17) | (1 << 16) |
                        (0x00 << 8) | (0x1B << 1) | NVIC_MPU_ATTR_ENABLE;
}

void steupSramAccess()
{
    // 4KB OS Kernel
    // set region number (0 - 7)
    NVIC_MPU_NUMBER_R = 0x2;
    // set region base address (N=log2(Size)) and let it use MPUNUMBER (0<<4)
        // region 0 : 0x20000000 - 0x20000FFF , 12 = log2(4KB)
    NVIC_MPU_BASE_R = (0x20000 << 12) | (0 << 4) | (0 << 0);
    // set region to allow processor to fetch in exception, -r-w user and +r+w privilege,
        // (tex-s-c-b) see pg.130, all sub-regions enable, size encoding pg.92 (N-1), enable the region
    NVIC_MPU_ATTR_R = (0 << 28) | (0b011 << 24) | (0b000 << 19) | (1 << 18) | (1 << 17) | (0 << 16) |
                        (0xFF << 8) | (0xB << 1) | NVIC_MPU_ATTR_ENABLE;

    // Heap for threads
    // set region number (0 - 7)
    NVIC_MPU_NUMBER_R = 0x3;
    // set region base address (N=log2(Size)) and let it use MPUNUMBER (0<<4)
        // region 0 : 0x20001000 - 0x20001FFF , 12 = log2(4KB)
    NVIC_MPU_BASE_R = (0x20001 << 12) | (0 << 4) | (0 << 0);
    // set region to allow processor to fetch in exception, -r-w user and +r+w privilege,
        // (tex-s-c-b) see pg.130, all sub-regions disable, size encoding pg.92 (N-1), enable the region
    NVIC_MPU_ATTR_R = (0 << 28) | (0b011 << 24) | (0b000 << 19) | (1 << 18) | (1 << 17) | (0 << 16) |
                        (0xFF << 8) | (0xB << 1) | NVIC_MPU_ATTR_ENABLE;

    // set region number (0 - 7)
    NVIC_MPU_NUMBER_R = 0x4;
    // set region base address (N=log2(Size)) and let it use MPUNUMBER (0<<4)
        // region 0 : 0x20002000 - 0x20003FFF , 13 = log2(8KB)
    NVIC_MPU_BASE_R = (0x10001 << 13) | (0 << 4) | (0 << 0);
    // set region to allow processor to fetch in exception, -r-w user and +r+w privilege,
        // (tex-s-c-b) see pg.130, all sub-regions disable, size encoding pg.92 (N-1), enable the region
    NVIC_MPU_ATTR_R = (0 << 28) | (0b011 << 24) | (0b000 << 19) | (1 << 18) | (1 << 17) | (0 << 16) |
                        (0xFF << 8) | (0xC << 1) | NVIC_MPU_ATTR_ENABLE;

    // set region number (0 - 7)
    NVIC_MPU_NUMBER_R = 0x5;
    // set region base address (N=log2(Size)) and let it use MPUNUMBER (0<<4)
        // region 0 : 0x20004000 - 0x20004FFF , 12 = log2(4KB)
    NVIC_MPU_BASE_R = (0x20004 << 12) | (0 << 4) | (0 << 0);
    // set region to allow processor to fetch in exception, -r-w user and +r+w privilege,
        // (tex-s-c-b) see pg.130, all sub-regions disable, size encoding pg.92 (N-1), enable the region
    NVIC_MPU_ATTR_R = (0 << 28) | (0b011 << 24) | (0b000 << 19) | (1 << 18) | (1 << 17) | (0 << 16) |
                        (0xFF << 8) | (0xB << 1) | NVIC_MPU_ATTR_ENABLE;

    // set region number (0 - 7)
    NVIC_MPU_NUMBER_R = 0x6;
    // set region base address (N=log2(Size)) and let it use MPUNUMBER (0<<4)
        // region 0 : 0x20005000 - 0x20005FFF , 12 = log2(4KB)
    NVIC_MPU_BASE_R = (0x20005 << 12) | (0 << 4) | (0 << 0);
    // set region to allow processor to fetch in exception, -r-w user and +r+w privilege,
        // (tex-s-c-b) see pg.130, all sub-regions disable, size encoding pg.92 (N-1), enable the region
    NVIC_MPU_ATTR_R = (0 << 28) | (0b011 << 24) | (0b000 << 19) | (1 << 18) | (1 << 17) | (0 << 16) |
                        (0xFF << 8) | (0xB << 1) | NVIC_MPU_ATTR_ENABLE;

    // set region number (0 - 7)
    NVIC_MPU_NUMBER_R = 0x7;
    // set region base address (N=log2(Size)) and let it use MPUNUMBER (0<<4)
        // region 0 : 0x20006000 - 0x20007FFF , 13 = log2(8KB)
    NVIC_MPU_BASE_R = (0x10003 << 13) | (0 << 4) | (0 << 0);
    // set region to allow processor to fetch in exception, -r-w user and +r+w privilege,
        // (tex-s-c-b) see pg.130, all sub-regions disable, size encoding pg.92 (N-1), enable the region
    NVIC_MPU_ATTR_R = (0 << 28) | (0b011 << 24) | (0b000 << 19) | (1 << 18) | (1 << 17) | (0 << 16) |
                        (0xFF << 8) | (0xC << 1) | NVIC_MPU_ATTR_ENABLE;
}

void sramRestrictedTest()
{
    uint32_t* p = malloc_from_heap(1024);

    // enable MPU background region, only privilege access
    NVIC_MPU_CTRL_R |= NVIC_MPU_CTRL_PRIVDEFEN;
    allowFlashAccess();
    allowPeripheralAccess();
    steupSramAccess();
    // enable MPU
    NVIC_MPU_CTRL_R |= NVIC_MPU_CTRL_ENABLE;

    uint64_t srdBitMask = createNoSramAccessMask();
    addSramAccessWindow(&srdBitMask, p, 1024);
    applySramAccessMask(srdBitMask);

    // have to enable OS kernel because variable p location is in PSP which is in OS
    NVIC_MPU_ATTR_R &= ~NVIC_MPU_ATTR_ENABLE;
    NVIC_MPU_NUMBER_R = 0x2;
    NVIC_MPU_ATTR_R &= 0xFFFF00FF;
    NVIC_MPU_ATTR_R |= NVIC_MPU_ATTR_ENABLE;

    // write under privilege mode
    *p = 5;
    // switch to user mode
    goUserMode();
    // write in user
    p++;
    *p = 10;
    // write outside of access region
    p += 0x400 - 4; // -4 because incremented by 4 bytes previously
    *p = 10;
}

void sramAllAccessTest()
{
    // enable MPU background region, only privilege access
    NVIC_MPU_CTRL_R |= NVIC_MPU_CTRL_PRIVDEFEN;
    allowFlashAccess();
    allowPeripheralAccess();
    steupSramAccess();
    // enable MPU
    NVIC_MPU_CTRL_R |= NVIC_MPU_CTRL_ENABLE;

    uint64_t srdBitMask = createNoSramAccessMask();
    uint32_t addr = 0x20000000;
    uint32_t* addrPtr = (uint32_t *)addr;
    addSramAccessWindow(&srdBitMask, addrPtr, 32768);
    applySramAccessMask(srdBitMask);

    // read from flash under privilege
    uint32_t* dataPtr = (uint32_t*)(0x00030000);
    uint32_t data = *(dataPtr);
    *(dataPtr) = 5;
    // switch to unprivileged mode
    goUserMode();
    // access to SRAM regions
    dataPtr = (uint32_t*)(0x20007C00);
    *(dataPtr) = 0xCC;
    // access word write to OS under USER
    togglePinValue(PORTF, 3);   // testing peripheral access
    dataPtr = (uint32_t*)(0x00030000); // flash
    uint32_t dataCopy = *(dataPtr);
    *(dataPtr) = 0xFF;      // give error user not allowed have write to flash
}

/**
 * mini_project.c
 */
int main(void)
{
    // initialize hardware
    initHw();
    uint32_t* pspPtr = (uint32_t*)PSP_ADDRESS;
    setPsp(pspPtr);
    // set ASP bit in CONTROL register
    goThreadMode();
//    sramAllAccessTest();
//    sramRestrictedTest();
//    mallocTesting1();
//    mallocTesting2();
//    mallocTesting3();
    freeTesting();
    shell();
    return 0;
}

void shell(void)
{
    char c;
    bool end;
    uint8_t count = 0;
    USER_DATA data;

    bool pc5, pc6, pc7, pd6, pd7;

    while(true)
    {
        pc5 = getPinValue(PORTC, 5);
        pc6 = getPinValue(PORTC, 6);
        pc7 = getPinValue(PORTC, 7);
        pd6 = getPinValue(PORTD, 6);
        pd7 = getPinValue(PORTD, 7);

        if(!pc5)
        {
            // set PF2 (blue led) to 1 (on)
            setPinValue(PORTF, 2, 1);
            // trigger pendSv fault
            triggerPendSv();
            pc5 = 0;
        }
        if(!pc6)
        {
            // set PE0 (green led) to 1 (on)
            setPinValue(PORTE, 0, 1);
            // trigger Memory Protection Unit Fault (MPU fault)
            triggerMpuFault();
        }
        if(!pc7)
        {
            // set PA4 (yellow led) to 1 (on)
            setPinValue(PORTA, 4, 1);
            // trigger hard fault
            triggerHardFault();
        }
        if(!pd6)
        {
            // set PA3 (orange led) to 1 (on)
            setPinValue(PORTA, 3, 1);
            // trigger usage fault
            triggerUsageFault();
        }
        if(!pd7)
        {
            // set A2 (red led) to 1 (on)
            setPinValue(PORTA, 2, 1);
            // trigger bus fault
            triggerBusFault();
        }
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
                else if(isCommand(&data, "free", 1))
                {
                    uint32_t* addr = (uint32_t*)hexStringToUint32(getFieldString(&data, 1));
                    uint32_t size;
                    bool ok = 0;
                    ok = checkOwnership(addr, &size, 0);
                    if(ok)
                    {
                        freeFromHeap(addr, size);
                    }
                    else
                    {
                        putsUart0("You THIEF!\n");
                    }
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
                            //
                        }
                    }
                }
            }
        }
        yeild();
    }
}

void mallocTesting1()
{
    uint32_t size = 512;
    uint32_t addr = (uint32_t) malloc_from_heap(size);      // 0
    char addrStr[10];
    uint32ToHexString(&addr, addrStr);
    putsUart0(addrStr);
    putcUart0('\n');
    addr = (uint32_t) malloc_from_heap(size);       // 1
    uint32ToHexString(&addr, addrStr);
    putsUart0(addrStr);
    putcUart0('\n');
    addr = (uint32_t) malloc_from_heap(size);       // 2
    uint32ToHexString(&addr, addrStr);
    putsUart0(addrStr);
    putcUart0('\n');
    addr = (uint32_t) malloc_from_heap(size);       // 3
    uint32ToHexString(&addr, addrStr);
    putsUart0(addrStr);
    putcUart0('\n');
    addr = (uint32_t) malloc_from_heap(size);       // 4
    uint32ToHexString(&addr, addrStr);
    putsUart0(addrStr);
    putcUart0('\n');
    addr = (uint32_t) malloc_from_heap(size);       // 5
    uint32ToHexString(&addr, addrStr);
    putsUart0(addrStr);
    putcUart0('\n');
    addr = (uint32_t) malloc_from_heap(size);       // 6
    uint32ToHexString(&addr, addrStr);
    putsUart0(addrStr);
    putcUart0('\n');
    addr = (uint32_t) malloc_from_heap(size);       // 17, on different block and skips 1 due to preservation of 1536
    uint32ToHexString(&addr, addrStr);
    putsUart0(addrStr);
    putcUart0('\n');
    addr = (uint32_t) malloc_from_heap(size);       // 18
    uint32ToHexString(&addr, addrStr);
    putsUart0(addrStr);
    putcUart0('\n');
    addr = (uint32_t) malloc_from_heap(size);       //  19
    uint32ToHexString(&addr, addrStr);
    putsUart0(addrStr);
    putcUart0('\n');
    addr = (uint32_t) malloc_from_heap(size);       // 20
    uint32ToHexString(&addr, addrStr);
    putsUart0(addrStr);
    putcUart0('\n');
    addr = (uint32_t) malloc_from_heap(size);       // 21
    uint32ToHexString(&addr, addrStr);
    putsUart0(addrStr);
    putcUart0('\n');
    addr = (uint32_t) malloc_from_heap(size);       // 22
    uint32ToHexString(&addr, addrStr);
    putsUart0(addrStr);
    putcUart0('\n');
    addr = (uint32_t) malloc_from_heap(size);       // 23
    uint32ToHexString(&addr, addrStr);
    putsUart0(addrStr);
    putcUart0('\n');
    addr = (uint32_t) malloc_from_heap(size);       // 24, on different block base
    uint32ToHexString(&addr, addrStr);
    putsUart0(addrStr);
    putcUart0('\n');
    addr = (uint32_t) malloc_from_heap(size);       // 25
    uint32ToHexString(&addr, addrStr);
    putsUart0(addrStr);
    putcUart0('\n');
    addr = (uint32_t) malloc_from_heap(size);       // 26
    uint32ToHexString(&addr, addrStr);
    putsUart0(addrStr);
    putcUart0('\n');
    addr = (uint32_t) malloc_from_heap(size);       // 27
    uint32ToHexString(&addr, addrStr);
    putsUart0(addrStr);
    putcUart0('\n');
    addr = (uint32_t) malloc_from_heap(size);       // 28
    uint32ToHexString(&addr, addrStr);
    putsUart0(addrStr);
    putcUart0('\n');
    addr = (uint32_t) malloc_from_heap(size);       // 29
    uint32ToHexString(&addr, addrStr);
    putsUart0(addrStr);
    putcUart0('\n');
    addr = (uint32_t) malloc_from_heap(size);       // 30
    uint32ToHexString(&addr, addrStr);
    putsUart0(addrStr);
    putcUart0('\n');
    addr = (uint32_t) malloc_from_heap(size);       // skips 31 preservation...goes to 7
    uint32ToHexString(&addr, addrStr);
    putsUart0(addrStr);
    putcUart0('\n');
    addr = (uint32_t) malloc_from_heap(size);       // 16
    uint32ToHexString(&addr, addrStr);
    putsUart0(addrStr);
    putcUart0('\n');
    addr = (uint32_t) malloc_from_heap(size);       // 31 index
    uint32ToHexString(&addr, addrStr);
    putsUart0(addrStr);
    putcUart0('\n');
    addr = (uint32_t) malloc_from_heap(size);       // 8, not 512 use 1024
    uint32ToHexString(&addr, addrStr);
    putsUart0(addrStr);
    putcUart0('\n');

    size = 1024;
    addr = (uint32_t) malloc_from_heap(size);
    uint32ToHexString(&addr, addrStr);
    putsUart0(addrStr);
    putcUart0('\n');

    size = 1500;
    addr = (uint32_t) malloc_from_heap(size);
    uint32ToHexString(&addr, addrStr);
    putsUart0(addrStr);
    putcUart0('\n');
}

void mallocTesting2()
{
    uint32_t size = 1500;
    uint32_t addr = (uint32_t) malloc_from_heap(size);      // 7
    char addrStr[10];
    uint32ToHexString(&addr, addrStr);
    putsUart0(addrStr);
    putcUart0('\n');

    addr = (uint32_t) malloc_from_heap(size);       // 15
    uint32ToHexString(&addr, addrStr);
    putsUart0(addrStr);
    putcUart0('\n');

    addr = (uint32_t) malloc_from_heap(size);       // 31
    uint32ToHexString(&addr, addrStr);
    putsUart0(addrStr);
    putcUart0('\n');

    addr = (uint32_t) malloc_from_heap(size);       // 0
    uint32ToHexString(&addr, addrStr);
    putsUart0(addrStr);
    putcUart0('\n');

    addr = (uint32_t) malloc_from_heap(size);       // 4
    uint32ToHexString(&addr, addrStr);
    putsUart0(addrStr);
    putcUart0('\n');

    size = 8000;
    addr = (uint32_t) malloc_from_heap(size);       // NULL
    uint32ToHexString(&addr, addrStr);
    putsUart0(addrStr);
    putcUart0('\n');

    size = 6000;
    addr = (uint32_t) malloc_from_heap(size);       // 9
    uint32ToHexString(&addr, addrStr);
    putsUart0(addrStr);
    putcUart0('\n');
}

void mallocTesting3()
{
    uint32_t size = 8000;
    uint32_t addr = (uint32_t) malloc_from_heap(size);      // 8
    char addrStr[10];
    uint32ToHexString(&addr, addrStr);
    putsUart0(addrStr);
    putcUart0('\n');

    addr = (uint32_t) malloc_from_heap(size);       // 32
    uint32ToHexString(&addr, addrStr);
    putsUart0(addrStr);
    putcUart0('\n');

    addr = (uint32_t) malloc_from_heap(size);       // NULL
    uint32ToHexString(&addr, addrStr);
    putsUart0(addrStr);
    putcUart0('\n');

    size = 4000;

    addr = (uint32_t) malloc_from_heap(size);       // 0
    uint32ToHexString(&addr, addrStr);
    putsUart0(addrStr);
    putcUart0('\n');

    addr = (uint32_t) malloc_from_heap(size);       // 16
    uint32ToHexString(&addr, addrStr);
    putsUart0(addrStr);
    putcUart0('\n');

    addr = (uint32_t) malloc_from_heap(size);       // 24
    uint32ToHexString(&addr, addrStr);
    putsUart0(addrStr);
    putcUart0('\n');

    size = 1;
    addr = (uint32_t) malloc_from_heap(size);       // NULL
    uint32ToHexString(&addr, addrStr);
    putsUart0(addrStr);
    putcUart0('\n');

//    activePid++;
}

void freeTesting()
{
    uint32_t size = 2000;
    uint32_t addr1 = (uint32_t) malloc_from_heap(size);      // 9
    char addrStr[10];
    uint32ToHexString(&addr1, addrStr);
    putsUart0(addrStr);
    putcUart0('\n');

    size = 3000;
    uint32_t addr2 = (uint32_t) malloc_from_heap(size);       // 11
    uint32ToHexString(&addr2, addrStr);
    putsUart0(addrStr);
    putcUart0('\n');

    freeFromHeap((uint32_t*)addr1, 2048);

    size = 1000;
    addr1 = (uint32_t) malloc_from_heap(size);       // 9
    uint32ToHexString(&addr1, addrStr);
    putsUart0(addrStr);
    putcUart0('\n');

    size = 1200;
    addr2 = (uint32_t) malloc_from_heap(size);       // 7
    uint32ToHexString(&addr2, addrStr);
    putsUart0(addrStr);
    putcUart0('\n');

    freeFromHeap((uint32_t*)addr2, 1536);

    addr2 = (uint32_t) malloc_from_heap(size);       // 7
    uint32ToHexString(&addr2, addrStr);
    putsUart0(addrStr);
    putcUart0('\n');
}
