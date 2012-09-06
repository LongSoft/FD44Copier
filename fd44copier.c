#include <stdio.h>
#include <malloc.h>
#include <memory.h>
#include "bios.h"

#define ERR_OK                  0;
#define ERR_ARGS                1;
#define ERR_INPUT_FILE          2;
#define ERR_OUTPUT_FILE         3;
#define ERR_MEMORY              4;
#define ERR_MODULE_NOT_FOUND    5;
#define ERR_EMPTY_MODULE        6;
#define ERR_DIFFERENT_BOARD     7;
#define ERR_NO_GBE              8;

/* memmem implementation using Boyer-Moore-Horspool algorithm */
unsigned char* memmem(unsigned char* string, long slen, const unsigned char* pattern, long plen)
{
    long int scan = 0;
    long int bad_char_skip[256];
    long int last;

    if (plen <= 0 || !string || !pattern)
        return NULL;

    for (scan = 0; scan <= 255; scan++)
        bad_char_skip[scan] = plen;

    last = plen - 1;

    for (scan = 0; scan < last; scan++)
        bad_char_skip[pattern[scan]] = last - scan;

    while (slen >= plen)
    {
        for (scan = last; string[scan] == pattern[scan]; scan--)
            if (scan == 0)
                return string;

        slen     -= bad_char_skip[string[last]];
        string   += bad_char_skip[string[last]];
    }

    return NULL;
}

int main(int argc, char* argv[])
{
    FILE* file;                                                     /* file pointer to work with input and output file */
    unsigned char* buffer;                                          /* buffer to read input and output file into */
    long int filesize;                                              /* size of opened file */
    long int read;                                                  /* read bytes counter */
    unsigned char* ubf;                                             /* UBF signature */
    unsigned char* bootefi;                                         /* bootefi signature */
    unsigned char* gbe;                                             /* GbE header */
    unsigned char* fd44;                                            /* module in search */
    unsigned char* module;                                          /* found module */
    long int rest;                                                  /* size of the rest of buffer in search*/
    char hasUbf;                                                    /* flag that output file has UBF header */
    char hasGbe;                                                    /* flag that input file has GbE region */
    char isEmpty;                                                   /* flag that module in input file is empty */

    unsigned char motherboardName[BOOTEFI_MOTHERBOARD_NAME_LENGTH]; /* motherboard name storage */
    unsigned char gbeMac[GBE_MAC_LENGTH];                           /* GbE MAC storage */
    unsigned char fd44Module[MODULE_LENGTH - MODULE_HEADER_LENGTH]; /* module storage */

    if(argc < 3)
    {
        printf("FD44Copier v0.2b\nThis program copies MAC address and FD44 block\nfrom one BIOS image file to another.\n\nUsage: FD44Copier INFILE OUTFILE\n");
        return ERR_ARGS;
    }

    /* Open input file */
    file = fopen(argv[1], "rb");
    if (!file)
    {
        fprintf(stderr, "Can't open input file.\n");
        return ERR_INPUT_FILE;
    }

    /* Determine file size */
    fseek(file, 0, SEEK_END);
    filesize = ftell(file);
    fseek(file, 0, SEEK_SET);

    /* Allocating memory for buffer */
    buffer = malloc(filesize);
    if (!buffer)
    {
        fprintf(stderr, "Can't allocate memory for input buffer.\n");
        return ERR_MEMORY;
    }

    /* Read whole file to buffer */
    read = fread((void*)buffer, sizeof(char), filesize, file);
    if (read != filesize)
    {
        fprintf(stderr, "Can't read input file.\n");
        return ERR_INPUT_FILE;
    }

    /* Search for bootefi signature */
    bootefi = memmem(buffer, filesize, BOOTEFI_HEADER, sizeof(BOOTEFI_HEADER));
    if (!bootefi)
    {
        fprintf(stderr, "ASUS BIOS file signature not found in input file.\n");
        return ERR_INPUT_FILE;
    }

    /* Store motherboard name */
    memcpy(motherboardName, bootefi + BOOTEFI_MOTHERBOARD_NAME_OFFSET, sizeof(motherboardName));

    /* Search for GbE and store MAC if found */
    hasGbe = 0;
    gbe = memmem(buffer, filesize, GBE_HEADER, sizeof(GBE_HEADER));
    if (gbe)
    {
        hasGbe = 1;
        /* Check if first GbE is a stub */
        if (!memcmp(gbe + GBE_MAC_OFFSET, GBE_MAC_STUB, sizeof(GBE_MAC_STUB)))
        {
            unsigned char* gbe2;
            rest = filesize - (gbe - buffer) - sizeof(GBE_HEADER);
            gbe2 = memmem(gbe + sizeof(GBE_HEADER), rest, GBE_HEADER, sizeof(GBE_HEADER));
            /* Check if second GbE is not a stub */
            if (memcmp(gbe2 + GBE_MAC_OFFSET, GBE_MAC_STUB, sizeof(GBE_MAC_STUB)))
                gbe = gbe2;
        }

        memcpy(gbeMac, gbe + GBE_MAC_OFFSET, GBE_MAC_LENGTH);
    }

    /* Search for module header */
    fd44 = memmem(buffer, filesize, MODULE_HEADER, sizeof(MODULE_HEADER));
    if (!fd44)
    {
        fprintf(stderr, "Module not found in input file.\n");
        return ERR_MODULE_NOT_FOUND;
    }

    /* Look for nonempty module */
    rest = filesize - (fd44 - buffer);
    isEmpty = 1;
    while(isEmpty && fd44)
    {
        unsigned int pos;

        if (memcmp(fd44 + MODULE_HEADER_BSA_OFFSET, MODULE_HEADER_BSA, sizeof(MODULE_HEADER_BSA)))
        {
            fd44 = memmem(fd44, rest, MODULE_HEADER, sizeof(MODULE_HEADER));
            rest = filesize - (fd44 - buffer);
            continue;
        }

        module = fd44 + MODULE_HEADER_LENGTH;
        for (pos = 0; pos < MODULE_LENGTH; pos++)
        {
            if (*module != (unsigned char)'\xFF')
            {
                isEmpty = 0;
                break;
            }
        }
    }

    if (isEmpty)
    {
        fprintf(stderr, "Module is empty in input file. Nothing to do.\n");
        return ERR_EMPTY_MODULE;
    }

    /* Store module contents */
    memcpy(fd44Module, module, sizeof(fd44Module));
    free(buffer);
    fclose(file);

    /* Open output file */
    file = fopen(argv[2], "r+b");
    if (!file)
    {
        fprintf(stderr, "Can't open output file.\n");
        return ERR_OUTPUT_FILE;
    }

    /* Determine file size */
    fseek(file, 0, SEEK_END);
    filesize = ftell(file);
    fseek(file, 0, SEEK_SET);

    /* Allocating memory for buffer */
    buffer = malloc(filesize);
    if (!buffer)
    {
        fprintf(stderr, "Can't allocate memory for output buffer.\n");
        return ERR_MEMORY;
    }

    /* Read whole file to buffer */
    read = fread((void*)buffer, sizeof(char), filesize, file);
    if (read != filesize)
    {
        fprintf(stderr, "Can't read output file.\n");
        return ERR_OUTPUT_FILE;
    }

    /* Search for UBF signature, if found - remove UBF header */
    hasUbf = 0;
    ubf = memmem(buffer, filesize, UBF_FILE_HEADER, sizeof(UBF_FILE_HEADER));
    if (ubf)
    {
        hasUbf = 1;
        buffer += UBF_FILE_HEADER_SIZE;
        filesize -= UBF_FILE_HEADER_SIZE;
    }

    /* Search for bootefi signature */
    bootefi = memmem(buffer, filesize, BOOTEFI_HEADER, sizeof(BOOTEFI_HEADER));
    if (!bootefi)
    {
        fprintf(stderr, "ASUS BIOS file signature not found in output file.\n");
        return ERR_OUTPUT_FILE;
    }

    /* Checking motherboard name */
    if (memcmp(motherboardName, bootefi + BOOTEFI_MOTHERBOARD_NAME_OFFSET, strlen((const char*)motherboardName)))
    {
        fprintf(stderr, "Motherboard name in output file is different from name in input file.\n");
        return ERR_DIFFERENT_BOARD;
    }

    /* If input file had GbE block, search for it in output file and replace it */
    if (hasGbe)
    {
        /* First GbE block */
        gbe = memmem(buffer, filesize, GBE_HEADER, sizeof(GBE_HEADER));
        if (!gbe)
        {
            fprintf(stderr, "GbE region not found in output file. Please use BIOS file from asus.com as output file.\n");
            return ERR_NO_GBE;
        }
        memcpy(gbe + GBE_MAC_OFFSET, gbeMac, sizeof(gbeMac));

        /* Second GbE block */
        rest = filesize - (gbe - buffer) - sizeof(GBE_HEADER);
        gbe = memmem(gbe + sizeof(GBE_HEADER), rest, GBE_HEADER, sizeof(GBE_HEADER));
        if(gbe)
            memcpy(gbe + GBE_MAC_OFFSET, gbeMac, sizeof(gbeMac));
    }

    /* Search for module header */
    fd44 = memmem(buffer, filesize, MODULE_HEADER, sizeof(MODULE_HEADER));
    if (!fd44)
    {
        fprintf(stderr, "Module not found in output file.\n");
        return ERR_MODULE_NOT_FOUND;
    }

    /* Replace all BSA_ modules */
    rest = filesize - (fd44 - buffer);
    while(fd44)
    {
        if (!memcmp(fd44 + MODULE_HEADER_BSA_OFFSET, MODULE_HEADER_BSA, sizeof(MODULE_HEADER_BSA)))
        {
            module = fd44 + MODULE_HEADER_LENGTH;
            memcpy(module, fd44Module, sizeof(fd44Module));
        }
        fd44 = memmem(fd44 + 1, rest, MODULE_HEADER, sizeof(MODULE_HEADER));
        rest = filesize - (fd44 - buffer);
    }

    /* Reopen file to resize it */
    fclose(file);
    file = fopen(argv[2], "wb");

    /* Write buffer to output file */
    read = fwrite(buffer, sizeof(char), filesize, file);
    if (read != filesize)
    {
        fprintf(stderr, "Can't write output file.\n");
        return ERR_OUTPUT_FILE;
    }

    if (hasUbf)
        buffer -= UBF_FILE_HEADER_SIZE;
    free(buffer);
    fclose(file);

    return ERR_OK;
}

