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
#include <stdbool.h>
#include "tm4c123gh6pm.h"
#include "shell.h"
#include "c_fnc.h"
#include "uart0.h"
#include "kernel.h"
#include "asm_src.h"

// REQUIRED: Add header files here for your strings functions, ...
// data from UI
#define MAX_CHARS   80
#define MAX_FIELDS  5
#define MAX_PS_DATA 15

typedef struct _USER_DATA
{
    char    buffer[MAX_CHARS+1];
    uint8_t fieldCount;
    uint8_t fieldPosition[MAX_FIELDS];
    char    fieldType[MAX_FIELDS];
} USER_DATA;

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------
void pkill(char str[]);
void pidof(char name[]);
void sched(bool prio_on);
void preempt(bool on);
void pi(bool on);
void kill(uint32_t pid);
void ipcs(void);
void ps(PS_DATA* psInfo);
bool isCommand(USER_DATA* data, const char strCommand[], uint8_t minArguments);
int32_t getFieldInteger(USER_DATA* data, uint8_t fieldNumber);
char* getFieldString(USER_DATA* data, uint8_t fieldNumber);
void parseFields(USER_DATA* data);
void resetTask(char taskName[]);

// REQUIRED: add processing for the shell commands through the UART here
void shell(void)
{
    char c;
    bool end;
    uint8_t count = 0;
    USER_DATA data;
    PS_DATA psInfo[MAX_PS_DATA];

    putsUart0("\nuser@rtos:~$ ");
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
                    __asm(" SVC #16");
                }
                else if(isCommand(&data, "ps", 0))
                {
                    ps(psInfo);
                }
                else if(isCommand(&data, "ipcs", 0))
                {
                    ipcs();
                }
                else if(isCommand(&data, "kill", 1))
                {
                    uint32_t pid = hexStringToUint32(getFieldString(&data, 1));
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
                else if(isCommand(&data, "meminfo", 0))
                {
                    __asm(" SVC #15");
                }
                else
                {
                    resetTask(getFieldString(&data, 0));
                }
                putsUart0("\nuser@rtos:~$ ");
            }
        }
        else
            yield();
    }
}

void resetTask(char taskName[])
{
    __asm(" SVC #18");
}

void pkill(char str[])
{
    __asm(" SVC #10");
    putsUart0((char*)str);
    putsUart0(" killed");
    putcUart0('\n');
}

void pidof(char name[])
{
    __asm(" SVC #11");
    uint32_t pid = reg0();
    char pidStr[20];
    putsUart0((char*)name);
    putsUart0(" pid value: ");
    putsUart0(uint32ToHexString(&pid, pidStr));
    putsUart0("\n\n");
}

void sched(bool prio_on)
{
    __asm(" SVC #12");
    if(prio_on)
    {
        putsUart0("prio on");
        putcUart0('\n');
    }
    else
    {
        putsUart0("prio off");
        putcUart0('\n');
    }
}

void preempt(bool on)
{
    __asm(" SVC #13");
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
    __asm(" SVC #14");
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
    __asm(" SVC #9");
    char pidStr[20];
    numToStr(pid, pidStr);
    putsUart0(pidStr);
    putsUart0(" killed\n");
}

void ipcs(void)
{
    __asm(" SVC #8");
    //putsUart0("IPCS called");
    putcUart0('\n');
}

void ps(PS_DATA* psInfo)
{
    __asm(" SVC #20");
    putsUart0("\n  TASK   \tCPU %   \tMEMORY   \tSTATE      \t\tMUTEX   \tSEMAPHORE\n");
    putsUart0("----------------------------------------------------------------------------------------------------\n");
    char data[NAME_SIZE];
    uint8_t i;
    for (i = 0; i < MAX_PS_DATA; i++)
    {
        if (psInfo[i].isData == true)
        {
            putsUart0(psInfo[i].taskName); putsUart0("    \t"); putsUart0(numToStr(psInfo[i].cpuPercent / 100, data));
            putcUart0('.'); putsUart0(numToStr(psInfo[i].cpuPercent % 100, data)); putsUart0("      \t");
            putsUart0(numToStr(psInfo[i].memory, data)); putsUart0("B    \t"); putsUart0(psInfo[i].state); putsUart0(" \t");
            putsUart0(psInfo[i].mutex); putsUart0("    \t"); putsUart0(psInfo[i].semaphore); putcUart0('\n');
        }
        else
            break;
    }
}

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
