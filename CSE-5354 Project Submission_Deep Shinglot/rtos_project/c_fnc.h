// C functions
// Deep Shinglot

//-----------------------------------------------------------------------------
// Hardware Target
//-----------------------------------------------------------------------------

// Target uC:       TM4C123GH6PM
// System Clock:    40 MHz#ifndef FAULT_HANDLERS_H

#ifndef C_FNC_H
#define C_FNC_H

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

// Subroutines

bool strCmp(const char str1[], const char str2[]);
void strCpy(const char str1[], char str2[]);
char* numToStr(uint32_t num, char str[]);
char* uint32ToHexString(uint32_t* value, char *hexString);
uint32_t hexStringToUint32(char* hexstring);

#endif
