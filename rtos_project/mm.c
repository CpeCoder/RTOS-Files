// Memory manager functions
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
#include "mm.h"

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------
void calculateBlockRequired(uint32_t requestedSize, uint8_t* blockCount1024, uint8_t* blockCount512);
bool findConsecutiveSpace(uint8_t* allocatedIndex, uint8_t* needed1024Blocks, uint8_t* needed512Blocks);
void getMallocAddr(uint16_t* subregion, uint16_t* region, uint8_t allocatedIndex);
uint8_t calculateIndex(uint32_t* baseAddrValue, uint16_t* subregionSize);
void addAllocation(uint32_t size, void* heapAddr, void* pid);

// Design specific data
#define HEAP_ADDRESS 0x20001000     // base address for tasks space 28KB
#define TOTAL_SPACE 28672
#define MAX_1536B_BLOCK 3
#define MAX_1024B_BLOCK 16
#define MAX_512B_BLOCK 24
#define MAX_ALLOCATION 14

//***************************************************************************************************************
// 63:56 - Sum of 1536B blocks used of 4-MAX
// 55:48 - Sum of 1024B blocks used of 16-MAX
// 47:40 - Sum of 512B blocks used of 24-MAX
//  39:0 - each bit (0 - not in use, 1 - in use) represents a respected sub-region (0 being region at base addr)
// **************************************************************************************************************
uint64_t subregionUseData = 0;
uint16_t usedSpace = 0;

// REQUIRED: add your malloc code here and update the SRD bits for the current thread
void * mallocFromHeap(uint32_t size_in_bytes)
{
    uint8_t* inUse1536 = (uint8_t*)(&subregionUseData) + 7; // 63:56 of inUse chances
    uint8_t* inUse1024 = (uint8_t*)(&subregionUseData) + 6; // 55:48 of inUse blocks
    uint8_t* inUse512  = (uint8_t*)(&subregionUseData) + 5; // 47:40 of inUse blocks

    bool ok = 0;
    uint8_t needed1024Blocks, needed512Blocks;
    calculateBlockRequired(size_in_bytes, &needed1024Blocks, &needed512Blocks);

    uint8_t totalSpaceNeeded = needed1024Blocks*2 + needed512Blocks; // Space respect to 512B block
    uint8_t block1024Avaliable = MAX_1024B_BLOCK - (*inUse1024);
    uint8_t block512Avaliable = MAX_512B_BLOCK - (*inUse512);

    // max allocation limit to 8KB
    if((totalSpaceNeeded > (block1024Avaliable*2 + block512Avaliable)) || totalSpaceNeeded > 16)
        return 0;    // all blocks used

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
            ok = findConsecutiveSpace(&allocatedIndex, &needed1024Blocks, &needed512Blocks);
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
        addAllocation(totalSpaceNeeded * 512, (void*)((uint32_t)addrPtr+size_in_bytes), (void*)0x20008000);   // store stack top
        return addrPtr;
    }
    else    // sizeInBytes = 0
        return 0;
}

// REQUIRED: add your free code here and update the SRD bits for the current thread
void freeToHeap(void *pMemory)
{
    uint8_t* inUse1536 = (uint8_t*)(&subregionUseData) + 7; // 63:56 of inUse chances
    uint8_t* inUse1024 = (uint8_t*)(&subregionUseData) + 6; // 55:48 of inUse blocks
    uint8_t* inUse512  = (uint8_t*)(&subregionUseData) + 5; // 47:40 of inUse blocks

    uint8_t i;
    for(i = 0; i < MAX_ALLOCATION; i++)
    {
        if (allocatedData[i].heapAddr == pMemory)
            break;
    }
    uint32_t sizeInBytes = allocatedData[i].size;
    uint32_t baseAddrValue = (uint32_t)pMemory - sizeInBytes;
    uint16_t startSubregionSize, endSubregionSize;
    uint8_t startIndex = calculateIndex(&baseAddrValue, &startSubregionSize);
    startIndex -= 8;    // heap don't include OS kernel
    baseAddrValue += sizeInBytes - 1;
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
        *(startSubregionSize == 512 ? inUse512 : inUse1024) -= sizeInBytes / startSubregionSize;
    usedSpace -= sizeInBytes;
}

// REQUIRED: include your solution from the mini project
void allowFlashAccess(void)
{
    // set region number 0
    NVIC_MPU_NUMBER_R = 0x0;
    // set region base address (N=log2(Size)) and let it use MPUNUMBER (0<<4)
        // region 0 : 0x00000000 - 0x00040000 , 18 = log2(256KB)
    NVIC_MPU_BASE_R = (0x00 << 18) | (0 << 4) | (0 << 0);
    // set region to allow processor to fetch in exception, +r+w+x privilege and +r+w+x user,
        // (tex-s-c-b) see pg.130, all sub-regions enable, size encoding pg.92 (N-1), enable the region
    NVIC_MPU_ATTR_R = (0 << 28) | (0b011 << 24) | (0b000 << 19) | (0 << 18) | (1 << 17) | (0 << 16) |
                        (0x00 << 8) | (0x11 << 1) | NVIC_MPU_ATTR_ENABLE;
}

void allowPeripheralAccess(void)
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

void setupSramAccess(void)
{
/*    // 4KB OS Kernel, takes background region config (+r+w+x privilege, -r-w-x user)
    // set region number (0 - 7)
    NVIC_MPU_NUMBER_R = 0x2;
    // set region base address (N=log2(Size)) and let it use MPUNUMBER (0<<4)
        // region 0 : 0x20000000 - 0x20000FFF , 12 = log2(4KB)
    NVIC_MPU_BASE_R = (0x20000 << 12) | (0 << 4) | (0 << 0);
    // set region to allow processor to fetch in exception, +r+w user,
        // (tex-s-c-b) see pg.130, all sub-regions enable, size encoding pg.92 (N-1), enable the region
    NVIC_MPU_ATTR_R = (0 << 28) | (0b011 << 24) | (0b000 << 19) | (1 << 18) | (1 << 17) | (0 << 16) |
                        (0xFF << 8) | (0xB << 1) | NVIC_MPU_ATTR_ENABLE;*/

    // Heap for threads
    // set region number (0 - 7)
    NVIC_MPU_NUMBER_R = 0x3;
    // set region base address (N=log2(Size)) and let it use MPUNUMBER (0<<4)
        // region 0 : 0x20001000 - 0x20001FFF , 12 = log2(4KB)
    NVIC_MPU_BASE_R = (0x20001 << 12) | (0 << 4) | (0 << 0);
    // set region to allow processor to fetch in exception, +r+w user,
        // (tex-s-c-b) see pg.130, all sub-regions disable, size encoding pg.92 (N-1), enable the region
    NVIC_MPU_ATTR_R = (0 << 28) | (0b011 << 24) | (0b000 << 19) | (1 << 18) | (1 << 17) | (0 << 16) |
                        (0xFF << 8) | (0xB << 1) | NVIC_MPU_ATTR_ENABLE;

    // set region number (0 - 7)
    NVIC_MPU_NUMBER_R = 0x4;
    // set region base address (N=log2(Size)) and let it use MPUNUMBER (0<<4)
        // region 0 : 0x20002000 - 0x20003FFF , 13 = log2(8KB)
    NVIC_MPU_BASE_R = (0x10001 << 13) | (0 << 4) | (0 << 0);
    // set region to allow processor to fetch in exception, +r+w user,
        // (tex-s-c-b) see pg.130, all sub-regions disable, size encoding pg.92 (N-1), enable the region
    NVIC_MPU_ATTR_R = (0 << 28) | (0b011 << 24) | (0b000 << 19) | (1 << 18) | (1 << 17) | (0 << 16) |
                        (0xFF << 8) | (0xC << 1) | NVIC_MPU_ATTR_ENABLE;

    // set region number (0 - 7)
    NVIC_MPU_NUMBER_R = 0x5;
    // set region base address (N=log2(Size)) and let it use MPUNUMBER (0<<4)
        // region 0 : 0x20004000 - 0x20004FFF , 12 = log2(4KB)
    NVIC_MPU_BASE_R = (0x20004 << 12) | (0 << 4) | (0 << 0);
    // set region to allow processor to fetch in exception, +r+w user,
        // (tex-s-c-b) see pg.130, all sub-regions disable, size encoding pg.92 (N-1), enable the region
    NVIC_MPU_ATTR_R = (0 << 28) | (0b011 << 24) | (0b000 << 19) | (1 << 18) | (1 << 17) | (0 << 16) |
                        (0xFF << 8) | (0xB << 1) | NVIC_MPU_ATTR_ENABLE;

    // set region number (0 - 7)
    NVIC_MPU_NUMBER_R = 0x6;
    // set region base address (N=log2(Size)) and let it use MPUNUMBER (0<<4)
        // region 0 : 0x20005000 - 0x20005FFF , 12 = log2(4KB)
    NVIC_MPU_BASE_R = (0x20005 << 12) | (0 << 4) | (0 << 0);
    // set region to allow processor to fetch in exception, +r+w user,
        // (tex-s-c-b) see pg.130, all sub-regions disable, size encoding pg.92 (N-1), enable the region
    NVIC_MPU_ATTR_R = (0 << 28) | (0b011 << 24) | (0b000 << 19) | (1 << 18) | (1 << 17) | (0 << 16) |
                        (0xFF << 8) | (0xB << 1) | NVIC_MPU_ATTR_ENABLE;

    // set region number (0 - 7)
    NVIC_MPU_NUMBER_R = 0x7;
    // set region base address (N=log2(Size)) and let it use MPUNUMBER (0<<4)
        // region 0 : 0x20006000 - 0x20007FFF , 13 = log2(8KB)
    NVIC_MPU_BASE_R = (0x10003 << 13) | (0 << 4) | (0 << 0);
    // set region to allow processor to fetch in exception, +r+w user,
        // (tex-s-c-b) see pg.130, all sub-regions disable, size encoding pg.92 (N-1), enable the region
    NVIC_MPU_ATTR_R = (0 << 28) | (0b011 << 24) | (0b000 << 19) | (1 << 18) | (1 << 17) | (0 << 16) |
                        (0x7F << 8) | (0xC << 1) | NVIC_MPU_ATTR_ENABLE;
}

uint64_t createNoSramAccessMask(void)
{
    // each byte presents a region and each bit a sub-region...0xFF disables sub-region
        // lsb presents lowest sub-region
    return 0xFFFFFFFFFFFFFFFF;
}

void addSramAccessWindow(uint64_t *srdBitMask, uint32_t *baseAdd, uint32_t size_in_bytes)
{
    uint32_t baseAddrValue = (uint32_t)(baseAdd);
    uint16_t subregionSize;
    uint8_t startIndex, endIndex;

    startIndex = calculateIndex(&baseAddrValue, &subregionSize);
    baseAddrValue += size_in_bytes;
    if(baseAddrValue > 0x20008000)
        return; // out of SRAM region
    endIndex = calculateIndex(&baseAddrValue, &subregionSize);
    if (size_in_bytes % 512) endIndex++;    // on not multiple of 512 add an additional index

    (*srdBitMask) &= ~((((uint64_t)1 << (endIndex - startIndex)) - 1) << startIndex);
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
            *blockCount1024 += 1; // if remains more than 512, allocate one more 1024B block
        else
        {
            if(*blockCount1024 > 1)
                *blockCount1024 += 1;
            else
                *blockCount512 = 1; // allocate 512B block for the remainder
        }
    }
}

void getMallocAddr(uint16_t* subregion, uint16_t* region, uint8_t allocatedIndex)
{
    *region = ((allocatedIndex / 8) * 0x1000);
    if (allocatedIndex >= 16)
        *region += 0x1000;  // skips 0x20003000 by adding

    // sub-region address based on the block
    if (allocatedIndex < 8 || (allocatedIndex >= 16 && allocatedIndex < 32))
        *subregion = (allocatedIndex % 8) * 512;
    else
        *subregion = (allocatedIndex % 8) * 1024;
}

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
                return found;

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
                        count1024B++;
                    else    // reset if occupied
                        count1024B = 0;

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
            uint8_t irr = 8;    // 8 sub-regions in one 4KB block
            // if checked once for space greater than 10 which is with edge sub-regions so there no space
            if ((tryWithoutEdge == 1) && (*needed512Blocks > 10))
                return found;
            else if ((*needed512Blocks > 8))
                irr = 16;  // allow allocation over other 4K region

            for(i = 0; i < 3; i++)  // 3 4KB blocks
            {
                // only save edge sub-region in 4KB if 8KB edge is not used, shifting hard-coded by design
                bool lower8kbRegionEdge = (bool) (subregionUseData & (1 << 15));
                bool upper8kbRegionEdge = (bool)((i == 0) ? (subregionUseData & (1 << 8)) : (subregionUseData & ((uint64_t)1 << 32)));

                // iterate through 4KB blocks
                uint16_t* currentBlock4kb = (uint16_t*)((i == 0) ? block1InUse4KB :
                                            (i == 1) ? block2InUse4KB : block3InUse4KB);
                uint8_t j, count512B = 0;
                for(j = 0; j < irr; j++)
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
                        count512B++;
                    else    // reset if occupied
                        count512B = 0;

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

uint8_t calculateIndex(uint32_t* baseAddrValue, uint16_t* subregionSize)
{
    uint32_t region = *baseAddrValue & 0xFFFFF000;
    uint16_t subregion = *baseAddrValue & 0x00000FFF;
    uint8_t index = 0;

    switch (region)
    {
        case 0x20000000: // Region 0 : 4KB
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

void addAllocation(uint32_t size, void* heapAddr, void* pid)
{
    uint8_t i;
    for(i = 0; i < 12; i++)
    {
        if(!allocatedData[i].inUse)
        {
            allocatedData[i].inUse = 1;
            allocatedData[i].fnPid = pid;
            allocatedData[i].size = size;
            allocatedData[i].heapAddr = heapAddr;
            usedSpace += size;
            return;
        }
    }
}

uint32_t getFreeSpace()
{
    return (TOTAL_SPACE - usedSpace);
}
