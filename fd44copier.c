#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bios.h"

#define ERR_OK                      0
#define ERR_ARGS                    1
#define ERR_INPUT_FILE              2
#define ERR_OUTPUT_FILE             3
#define ERR_MEMORY                  4
#define ERR_MODULE_NOT_FOUND        5
#define ERR_EMPTY_FD44_MODULE       6
#define ERR_DIFFERENT_BOARD         7
#define ERR_NO_GBE                  8

/* memmem - implementation of GNU memmem function using Boyer-Moore-Horspool algorithm */
unsigned char* memmem(unsigned char* string, size_t slen, const unsigned char* pattern, size_t plen)
{
    size_t scan = 0;
    size_t bad_char_skip[256];
    size_t last;

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

/* find_free_space - finds free space between begin and end to insert new module. Returns alligned pointer to empty space or NULL if it can't be found. */
unsigned char* find_free_space(unsigned char* begin, unsigned char* end, size_t space_length)
{
    size_t pos;
    for(pos = end - begin - 1; pos > 0; pos--)
    {
        if(*(begin+pos) != (unsigned char)'\xFF')
        {
            pos +=  8 - pos%8; /* allign to 8 */
            if(end - begin - pos >= space_length)
                return begin + pos;
            else
                return NULL;
        }
    }
    return NULL;
}

int main(int argc, char* argv[])
{
    FILE* file;                                                     /* file pointer to work with input and output file */
    unsigned char* buffer;                                          /* buffer to read input and output file into */
    size_t filesize;                                                /* size of opened file */
    size_t read;                                                    /* read bytes counter */
    unsigned char* ubf;                                             /* UBF signature */
    unsigned char* bootefi;                                         /* bootefi signature */
    unsigned char* gbe;                                             /* GbE header */
    unsigned char* slic_pubkey;                                     /* SLIC pubkey header */
    unsigned char* slic_marker;                                     /* SLIC marker header */
    unsigned char* fd44;                                            /* module in search */
    unsigned char* module;                                          /* found module */
    size_t rest;                                                    /* size of the rest of buffer in search*/
    char hasUbf;                                                    /* flag that output file has UBF header */
    char hasGbe;                                                    /* flag that input file has GbE region */
    char hasSLIC;                                                   /* flag that input file has SLIC pubkey and marker */
    char isEmpty;                                                   /* flag that module in input file is empty */

    unsigned char motherboardName[BOOTEFI_MOTHERBOARD_NAME_LENGTH]; /* motherboard name storage */
    unsigned char gbeMac[GBE_MAC_LENGTH];                           /* GbE MAC storage */
    unsigned char slicPubkey[SLIC_PUBKEY_LENGTH];                   /* SLIC pubkey storage */
    unsigned char slicMarker[SLIC_MARKER_LENGTH];                   /* SLIC marker storage */
    unsigned char fd44Module[FD44_MODULE_LENGTH - FD44_MODULE_HEADER_LENGTH]; /* module storage */

    if(argc < 3)
    {
        printf("FD44Copier v0.3.1b\nThis program copies GbE MAC address, FD44 module, SLIC pubkey and marker\nfrom one BIOS image file to another.\n\nUsage: FD44Copier INFILE OUTFILE\n");
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
    buffer = (unsigned char*)malloc(filesize);
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
            if (gbe2 && memcmp(gbe2 + GBE_MAC_OFFSET, GBE_MAC_STUB, sizeof(GBE_MAC_STUB)))
                gbe = gbe2;
        }

        memcpy(gbeMac, gbe + GBE_MAC_OFFSET, GBE_MAC_LENGTH);
    }

    /* Search for SLIC headers and store pubkey and marker if found*/
    hasSLIC = 0;
    slic_pubkey = memmem(buffer, filesize, SLIC_PUBKEY_HEADER, sizeof(SLIC_PUBKEY_HEADER));
    if(slic_pubkey)
    {
        slic_marker = memmem(buffer, filesize, SLIC_MARKER_HEADER, sizeof(SLIC_MARKER_HEADER));
        if(slic_marker)
        {
            hasSLIC = 1;
            memcpy(slicPubkey, slic_pubkey, sizeof(slicPubkey));
            memcpy(slicMarker, slic_marker, sizeof(slicMarker));
        }
    }

    /* Search for module header */
    fd44 = memmem(buffer, filesize, FD44_MODULE_HEADER, sizeof(FD44_MODULE_HEADER));
    if (!fd44)
    {
        fprintf(stderr, "FD44 module not found in input file.\n");
        return ERR_MODULE_NOT_FOUND;
    }

    /* Look for nonempty module */
    rest = filesize - (fd44 - buffer);
    isEmpty = 1;
    while(isEmpty && fd44)
    {
        unsigned int pos;

        if (memcmp(fd44 + FD44_MODULE_HEADER_BSA_OFFSET, FD44_MODULE_HEADER_BSA, sizeof(FD44_MODULE_HEADER_BSA)))
        {
            fd44 = memmem(fd44 + FD44_MODULE_HEADER_LENGTH, rest, FD44_MODULE_HEADER, sizeof(FD44_MODULE_HEADER));
            rest = filesize - (fd44 - buffer);
            continue;
        }

        module = fd44 + FD44_MODULE_HEADER_LENGTH;
        for (pos = 0; pos < FD44_MODULE_LENGTH; pos++)
        {
            if (*module != (unsigned char)'\xFF')
            {
                isEmpty = 0;
                break;
            }
        }
        
        fd44 = memmem(fd44 + FD44_MODULE_HEADER_LENGTH, rest, FD44_MODULE_HEADER, sizeof(FD44_MODULE_HEADER));
        rest = filesize - (fd44 - buffer);
    }

    if (isEmpty)
    {
        fprintf(stderr, "Fd44 module is empty in input file.\nNothing to do.\n");
        return ERR_EMPTY_FD44_MODULE;
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
    buffer = (unsigned char*)malloc(filesize);
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
        printf("UBF header removed.\n");
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
        fprintf(stderr, "Motherboard name in output file differs from name in input file.\n");
        return ERR_DIFFERENT_BOARD;
    }

    /* If input file had GbE block, search for it in output file and replace it */
    if (hasGbe)
    {
        /* First GbE block */
        gbe = memmem(buffer, filesize, GBE_HEADER, sizeof(GBE_HEADER));
        if (!gbe)
        {
            fprintf(stderr, "GbE region not found in output file.\nPlease use BIOS file from asus.com as output file.\n");
            return ERR_NO_GBE;
        }
        memcpy(gbe + GBE_MAC_OFFSET, gbeMac, sizeof(gbeMac));

        /* Second GbE block */
        rest = filesize - (gbe - buffer) - sizeof(GBE_HEADER);
        gbe = memmem(gbe + sizeof(GBE_HEADER), rest, GBE_HEADER, sizeof(GBE_HEADER));
        if(gbe)
            memcpy(gbe + GBE_MAC_OFFSET, gbeMac, sizeof(gbeMac));
        
        printf("GbE MAC address copied.\n");
    }

    /* Search for MSOA-containing module and add SLIC pubkey and marker if found */
    if(hasSLIC)
    {
        unsigned char* msoa_module;
        unsigned char* next_module;
        unsigned char* pubkey_module;
        unsigned char* marker_module;

        pubkey_module = memmem(buffer, filesize, SLIC_PUBKEY_HEADER, sizeof(SLIC_PUBKEY_HEADER));
        marker_module = memmem(buffer, filesize, SLIC_MARKER_HEADER, sizeof(SLIC_MARKER_HEADER));
        if(!pubkey_module && !marker_module)
        {
            msoa_module = memmem(buffer, filesize, MSOA_MODULE_HEADER, sizeof(MSOA_MODULE_HEADER));
            if(msoa_module)
            {
                rest = filesize - (msoa_module - buffer);
                next_module = memmem(msoa_module, rest, BIOS_VOLUME_HEADER, sizeof(BIOS_VOLUME_HEADER));
                if(!next_module)
                {
                    fprintf(stderr, "MSOA module is at the end of output file.\nSLIC table can't be transfered.\n");                       
                }
                else
                {
                    pubkey_module = find_free_space(msoa_module, next_module + BIOS_VOLUME_HEADER_OFFSET, SLIC_PUBKEY_LENGTH + SLIC_MARKER_LENGTH + 16);
                
                    if(pubkey_module)
                    {
                        memcpy(pubkey_module, slicPubkey, sizeof(slicPubkey));
                    
                        marker_module =  find_free_space(pubkey_module, next_module + BIOS_VOLUME_HEADER_OFFSET, SLIC_MARKER_LENGTH + 8);
                        if(marker_module)
                        {
                            memcpy(marker_module, slicMarker, sizeof(slicMarker));
                            printf("SLIC pubkey and marker copied.\n");
                        }
                        else
                        {
                            memset(pubkey_module, 0xFF, sizeof(slicPubkey));
                            fprintf(stderr, "Not enough free space to instert marker module.\nSLIC table can't be transfered.\n");
                        }
                    }
                    else
                    {
                        fprintf(stderr, "Not enough free space to instert pubkey module.\nSLIC table can't be transfered.\n");                       
                    }
                }
            }
            else
            {
                fprintf(stderr, "MSOA module not found in output file.\nSLIC table can't be transfered.\n");
            }
        }
        else
        {
            fprintf(stderr, "SLIC pubkey or marker is found in output file.\nSLIC table transfer in not needed.\n");
        }
    }

    /* Search for module header */
    fd44 = memmem(buffer, filesize, FD44_MODULE_HEADER, sizeof(FD44_MODULE_HEADER));
    if (!fd44)
    {
        fprintf(stderr, "FD44 module not found in output file.\n");
        return ERR_MODULE_NOT_FOUND;
    }

    /* Replace all BSA_ modules */
    rest = filesize - (fd44 - buffer) - FD44_MODULE_HEADER_LENGTH - FD44_MODULE_LENGTH;
    while(fd44)
    {
        if (!memcmp(fd44 + FD44_MODULE_HEADER_BSA_OFFSET, FD44_MODULE_HEADER_BSA, sizeof(FD44_MODULE_HEADER_BSA)))
        {
            module = fd44 + FD44_MODULE_HEADER_LENGTH;
            memcpy(module, fd44Module, sizeof(fd44Module));
        }
        fd44 = memmem(fd44 + FD44_MODULE_LENGTH + FD44_MODULE_HEADER_LENGTH, rest, FD44_MODULE_HEADER, sizeof(FD44_MODULE_HEADER));
        rest = filesize - (fd44 - buffer) - FD44_MODULE_HEADER_LENGTH - FD44_MODULE_LENGTH;
    }
    printf("FD44 module copied.\n");

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

