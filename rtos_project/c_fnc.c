// Deep Shinglot

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

bool strCmp(const char str1[], const char str2[])
{
    uint8_t i = 0;

    while(str1[i] || str2[i])
    {
        if(str1[i] != str2[i])
        {
            return false;
        }
        i++;
    }
    return true;
}

void strCpy(const char str1[], char str2[])
{
    uint8_t i = 0;

    while(str1[i] != '\0')
    {
        str2[i] = str1[i];
        i++;
    }
    str2[i] = '\0';
}

char* numToStr(uint32_t num, char str[])
{
    uint8_t i = 0;

    if (num == 0)
    {
        str[i++] = '0';
        str[i] = '\0';
        return str;
    }

    // Find the end of the string by counting digits
    uint32_t temp = num;
    while (temp != 0)
    {
        temp = temp / 10;
        i++;
    }

    str[i] = '\0';

    // Fill the string from the end
    while (num != 0)
    {
        i--;
        uint8_t digit = num % 10;
        str[i] = digit + '0';  // Convert digit to character using ASCII
        num = num / 10;
    }

    return str;
}

char* uint32ToHexString(uint32_t* value, char* hexString)
{
    char *hexDigits = "0123456789ABCDEF";
    int8_t i;
    // Process each nibble (4 bits) of the 32-bit number
    for (i = 7; i >= 0; i--)
    {
        // Extract and convert each nibble
        hexString[7 - i] = hexDigits[(*value >> (i * 4)) & 0xF];
    }
    // Null-terminate the string
    hexString[8] = '\0';
    return hexString;
}

uint32_t hexStringToUint32(char* hexString)
{
    uint32_t value = 0;
    uint8_t i;
    // Convert each character in the hex string to its corresponding uint32_t value
    for (i = 0; i < 8; i++)
    {
        char hexChar = hexString[i];
        uint32_t digitValue;

        if (hexChar >= '0' && hexChar <= '9')
        {
            digitValue = hexChar - '0';
        }
        else if (hexChar >= 'A' && hexChar <= 'F')
        {
            digitValue = hexChar - 'A' + 10;
        }
        else if (hexChar >= 'a' && hexChar <= 'f')
        {
            digitValue = hexChar - 'a' + 10;
        }
        value = (value << 4) | digitValue; // Shift left and add the new digit
    }

    return value;
}
