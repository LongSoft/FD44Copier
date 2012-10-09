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
#define ERR_NO_SLIC                 9

/* findpattern - implementation of GNU memmem function using Boyer-Moore-Horspool algorithm */
unsigned char* findpattern(unsigned char* string, size_t slen, const unsigned char* pattern, size_t plen)
{
    size_t scan = 0;
    size_t bad_char_skip[256];
    size_t last;

    if (plen == 0 || !string || !pattern)
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

/* findfreespace - finds free space between begin and end to insert new module. Returns alligned pointer to empty space or NULL if it can't be found. */
unsigned char* findfreespace(unsigned char* begin, unsigned char* end, size_t space_length)
{
    size_t pos;
    size_t free_bytes;

    free_bytes = 0;
    for(pos = 0; pos < (size_t)(end - begin); pos++)
    {
        if(*(begin+pos) == (unsigned char)'\xFF')
            free_bytes++;
        else
            free_bytes = 0;
        if(free_bytes == space_length)
        {
            pos -= free_bytes; /* back at the beginning of free space */
            pos += 8 - pos%8; /* allign to 8 */
            return begin + pos;
        }
    }
    return NULL;
}

int main(int argc, char* argv[])
{
    FILE* file;                                                                                             /* file pointer to work with input and output file */
    char* inputfile;                                                                                        /* path to input file*/
    char* outputfile;                                                                                       /* path to output file */
    unsigned char* buffer;                                                                                  /* buffer to read input and output file into */
    size_t filesize;                                                                                        /* size of opened file */
    size_t read;                                                                                            /* read bytes counter */
    unsigned char* ubf;                                                                                     /* UBF signature */
    unsigned char* bootefi;                                                                                 /* bootefi signature */
    unsigned char* gbe;                                                                                     /* GbE header */
    unsigned char* asusbkp;                                                                                 /* ASUSBKP header */
    unsigned char* slic_pubkey;                                                                             /* SLIC pubkey header */
    unsigned char* slic_marker;                                                                             /* SLIC marker header */
    unsigned char* fd44;                                                                                    /* module in search */
    unsigned char* module;                                                                                  /* found module */
    size_t rest;                                                                                            /* size of the rest of buffer in search*/
    char hasUbf;                                                                                            /* flag that output file has UBF header */
    char hasGbe;                                                                                            /* flag that input file has GbE region */
    char hasSLIC;                                                                                           /* flag that input file has SLIC pubkey and marker */
    char defaultOptions;                                                                                    /* flag that programm is runned with default options */
    char copyModule;                                                                                        /* flag that module copying is requested */
    char copyGbe;                                                                                           /* flag that GbE MAC copying is requested */
    char copySLIC;                                                                                          /* flag that SLIC copying is requested */
    char skipMotherboardNameCheck;                                                                          /* flag that motherboard name in output file don't need to be checked */ 
    char isEmpty;                                                                                           /* flag that FD44 module in input file is empty */
    unsigned char motherboardName[BOOTEFI_MOTHERBOARD_NAME_LENGTH];                                         /* motherboard name storage */
    unsigned char gbeMac[GBE_MAC_LENGTH];                                                                   /* GbE MAC storage */
    unsigned char slicPubkey[SLIC_PUBKEY_LENGTH - sizeof(SLIC_PUBKEY_HEADER) - sizeof(SLIC_PUBKEY_PART1)];  /* SLIC pubkey storage */
    unsigned char slicMarker[SLIC_MARKER_LENGTH - sizeof(SLIC_MARKER_HEADER) - sizeof(SLIC_MARKER_PART1)];  /* SLIC marker storage */
    unsigned char fd44Module[FD44_MODULE_LENGTH - FD44_MODULE_HEADER_LENGTH];                               /* module storage */

    if(argc < 3 || (argv[1][0] == '-' && argc < 4))
    {
        printf("FD44Copier v0.5.2\nThis program copies GbE MAC address, FD44 module, SLIC pubkey and marker from one BIOS image file to another\n\n"
            "Usage: FD44Copier <-OPTIONS> INFILE OUTFILE\n\n"
            "Options: m - copy module data\n"
            "         g - copy GbE MAC address\n"
            "         s - copy SLIC pubkey and marker\n"
            "         n - do not check that both BIOS files are for same motherboard\n"
            "         <none> - copy all available data and check for same motherboard in both BIOS files\n\n");
        return ERR_ARGS;
    }

    /* Setting default options */
    defaultOptions =            1;
    copyModule =                1;
    copyGbe =                   1;
    copySLIC =                  1;
    skipMotherboardNameCheck =  0;

    /* Checking for options presence and setting options*/
    if(argv[1][0] == '-')
    {
        size_t optlen = strlen(argv[1]);
        if(!findpattern((unsigned char*)argv[1], optlen, "m", 1))
            copyModule = 0;
        if(!findpattern((unsigned char*)argv[1], optlen, "g", 1))
            copyGbe = 0;
        if(!findpattern((unsigned char*)argv[1], optlen, "s", 1))
            copySLIC = 0;
        if(findpattern((unsigned char*)argv[1], optlen, "n", 1))
            skipMotherboardNameCheck = 1;
        inputfile = argv[2];
        outputfile = argv[3];
        defaultOptions = 0;
    }
    else
    {
        inputfile = argv[1];
        outputfile = argv[2];
    }

     /* Opening input file */
    file = fopen(inputfile, "rb");
    if (!file)
    {
        fprintf(stderr, "Can't open input file\n");
        return ERR_INPUT_FILE;
    }

    /* Determining file size */
    fseek(file, 0, SEEK_END);
    filesize = ftell(file);
    fseek(file, 0, SEEK_SET);

    /* Allocating memory for buffer */
    buffer = (unsigned char*)malloc(filesize);
    if (!buffer)
    {
        fprintf(stderr, "Can't allocate memory for input buffer\n");
        return ERR_MEMORY;
    }

    /* Reading whole file to buffer */
    read = fread((void*)buffer, sizeof(char), filesize, file);
    if (read != filesize)
    {
        fprintf(stderr, "Can't read input file\n");
        return ERR_INPUT_FILE;
    }

    /* Searching for bootefi signature */
    bootefi = findpattern(buffer, filesize, BOOTEFI_HEADER, sizeof(BOOTEFI_HEADER));
    if (!bootefi)
    {
        fprintf(stderr, "ASUS BIOS file signature not found in input file\n");
        return ERR_INPUT_FILE;
    }
    
    /* Storing motherboard name */
    if(!skipMotherboardNameCheck)
        memcpy(motherboardName, bootefi + BOOTEFI_MOTHERBOARD_NAME_OFFSET, sizeof(motherboardName));

    /* Searching for GbE and storing MAC address if it is found */
    if(copyGbe)
    {
        hasGbe = 0;
        gbe = findpattern(buffer, filesize, GBE_HEADER, sizeof(GBE_HEADER));
        if(gbe)
        {
            hasGbe = 1;
            /* Checking if first GbE is a stub */
            if(!memcmp(gbe + GBE_MAC_OFFSET, GBE_MAC_STUB, sizeof(GBE_MAC_STUB)))
            {
                unsigned char* gbe2;
                rest = filesize - (gbe - buffer) - sizeof(GBE_HEADER);
                gbe2 = findpattern(gbe + sizeof(GBE_HEADER), rest, GBE_HEADER, sizeof(GBE_HEADER));
                /* Checking if second GbE is not a stub */
                if(gbe2 && memcmp(gbe2 + GBE_MAC_OFFSET, GBE_MAC_STUB, sizeof(GBE_MAC_STUB)))
                    gbe = gbe2;
            }

            if(!memcpy(gbeMac, gbe + GBE_MAC_OFFSET, GBE_MAC_LENGTH))
            {
                fprintf(stderr, "Memcpy failed.\nGbE MAC can't be copied\n");       
                return ERR_MEMORY;
            }
        }

        if(!defaultOptions && !hasGbe)
        {
            fprintf(stderr, "GbE region not found in input file, but required by -g option\nNothing to do\n");
            return ERR_NO_GBE;
        }
    }

    /* Searching for SLIC pubkey and marker and storing them if they are found*/
    if(copySLIC)
    {
        hasSLIC = 0;
        slic_pubkey = findpattern(buffer, filesize, SLIC_PUBKEY_HEADER, sizeof(SLIC_PUBKEY_HEADER));
        slic_marker = findpattern(buffer, filesize, SLIC_MARKER_HEADER, sizeof(SLIC_MARKER_HEADER));
        if(slic_pubkey && slic_marker) 
        {
            slic_pubkey += sizeof(SLIC_PUBKEY_HEADER) + sizeof(SLIC_PUBKEY_PART1);
            slic_marker += sizeof(SLIC_MARKER_HEADER) + sizeof(SLIC_MARKER_PART1);
            if(!memcpy(slicPubkey, slic_pubkey, sizeof(slicPubkey)))
            {
                fprintf(stderr, "Memcpy failed\nSLIC table can't be copied\n");       
                return ERR_MEMORY;
            }
            if(!memcpy(slicMarker, slic_marker, sizeof(slicMarker)))
            {
                fprintf(stderr, "Memcpy failed\nSLIC table can't be copied\n");       
                return ERR_MEMORY;
            }
            hasSLIC = 1;
        }
        else /* If SLIC headers not found, seaching for SLIC pubkey and marker in ASUSBKP module */
        {
            asusbkp = findpattern(buffer, filesize, ASUSBKP_HEADER, sizeof(ASUSBKP_HEADER));
            if(asusbkp)
            {
                rest = filesize - (asusbkp - buffer);
                slic_pubkey = findpattern(asusbkp, rest, ASUSBKP_PUBKEY_HEADER, sizeof(ASUSBKP_PUBKEY_HEADER));
                slic_marker = findpattern(asusbkp, rest, ASUSBKP_MARKER_HEADER, sizeof(ASUSBKP_MARKER_HEADER));
                if(slic_pubkey && slic_marker)
                {
                    slic_pubkey += sizeof(ASUSBKP_PUBKEY_HEADER);
                    slic_marker += sizeof(ASUSBKP_MARKER_HEADER);
                    if(!memcpy(slicPubkey, slic_pubkey, sizeof(slicPubkey)))
                    {
                        fprintf(stderr, "Memcpy failed\nSLIC table can't be copied\n");       
                        return ERR_MEMORY;
                    }
                    if(!memcpy(slicMarker, slic_marker, sizeof(slicMarker)))
                    {
                        fprintf(stderr, "Memcpy failed\nSLIC table can't be copied\n");       
                        return ERR_MEMORY;
                    }
                    hasSLIC = 1;
                }
            }
        }

        if(!defaultOptions && !hasSLIC)
        {
            fprintf(stderr, "SLIC pubkey and marker not found in input file, but required by -s option\nNothing to do\n");
            return ERR_NO_SLIC;
        }
    }

    /* Searching for module header */
    if(copyModule)
    {
        fd44 = findpattern(buffer, filesize, FD44_MODULE_HEADER, sizeof(FD44_MODULE_HEADER));
        if (!fd44)
        {
            fprintf(stderr, "FD44 module not found in input file\n");
            return ERR_MODULE_NOT_FOUND;
        }

        /* Looking for nonempty module */
        rest = filesize - (fd44 - buffer);
        isEmpty = 1;
        while(isEmpty && fd44)
        {
            if (!memcmp(fd44 + FD44_MODULE_HEADER_BSA_OFFSET, FD44_MODULE_HEADER_BSA, sizeof(FD44_MODULE_HEADER_BSA)))
            {
                unsigned int pos;
                module = fd44 + FD44_MODULE_HEADER_LENGTH;
                for (pos = 0; pos < FD44_MODULE_LENGTH; pos++)
                {
                    if (*module != (unsigned char)'\xFF')
                    {
                        isEmpty = 0;
                        break;
                    }
                }
            }

            fd44 = findpattern(fd44 + FD44_MODULE_HEADER_LENGTH, rest, FD44_MODULE_HEADER, sizeof(FD44_MODULE_HEADER));
            rest = filesize - (fd44 - buffer);
        }

        if (isEmpty)
        {
            fprintf(stderr, "FD44 module is empty in input file\nNothing to do\n");
            return ERR_EMPTY_FD44_MODULE;
        }

        /* Storing module contents */
        if(!memcpy(fd44Module, module, sizeof(fd44Module)))
        {
            fprintf(stderr, "Memcpy failed\nFD44 module can't be copied\n");       
            return ERR_MEMORY;
        }
    }

    /* Closing input file */
    free(buffer);
    fclose(file);
    
    /* Opening output file */
    file = fopen(outputfile, "r+b");
    if (!file)
    {
        fprintf(stderr, "Can't open output file\n");
        return ERR_OUTPUT_FILE;
    }

    /* Determining file size */
    fseek(file, 0, SEEK_END);
    filesize = ftell(file);
    fseek(file, 0, SEEK_SET);

    /* Allocating memory for buffer */
    buffer = (unsigned char*)malloc(filesize);
    if (!buffer)
    {
        fprintf(stderr, "Can't allocate memory for output buffer\n");
        return ERR_MEMORY;
    }

    /* Reading whole file to buffer */
    read = fread((void*)buffer, sizeof(char), filesize, file);
    if (read != filesize)
    {
        fprintf(stderr, "Can't read output file\n");
        return ERR_OUTPUT_FILE;
    }

    /* Searching for UBF signature, if found - remove UBF header */
    hasUbf = 0;
    ubf = findpattern(buffer, filesize, UBF_FILE_HEADER, sizeof(UBF_FILE_HEADER));
    if (ubf)
    {
        hasUbf = 1;
        buffer += UBF_FILE_HEADER_SIZE;
        filesize -= UBF_FILE_HEADER_SIZE;
        printf("UBF header removed\n");
    }

    /* Searching for bootefi signature */
    bootefi = findpattern(buffer, filesize, BOOTEFI_HEADER, sizeof(BOOTEFI_HEADER));
    if (!bootefi)
    {
        fprintf(stderr, "ASUS BIOS file signature not found in output file\n");
        return ERR_OUTPUT_FILE;
    }

    /* Checking motherboard name */
    if (!skipMotherboardNameCheck && memcmp(motherboardName, bootefi + BOOTEFI_MOTHERBOARD_NAME_OFFSET, strlen((const char*)motherboardName)))
    {
        fprintf(stderr, "Motherboard name in output file differs from name in input file\nNothing to do\n");
        return ERR_DIFFERENT_BOARD;
    }

    /* If input file had GbE block, searching for it in output file and replacing it */
    if (copyGbe && hasGbe)
    {
        /* First GbE block */
        gbe = findpattern(buffer, filesize, GBE_HEADER, sizeof(GBE_HEADER));
        if (!gbe)
        {
            fprintf(stderr, "GbE region not found in output file\nPlease use BIOS file from asus.com as output file\n");
            return ERR_NO_GBE;
        }
        if(!memcpy(gbe + GBE_MAC_OFFSET, gbeMac, sizeof(gbeMac)))
        {
            fprintf(stderr, "Memcpy failed\nGbE MAC can't be copied\n");       
            return ERR_MEMORY;
        }

        /* Second GbE block */
        rest = filesize - (gbe - buffer) - sizeof(GBE_HEADER);
        gbe = findpattern(gbe + sizeof(GBE_HEADER), rest, GBE_HEADER, sizeof(GBE_HEADER));
        
        if(gbe && !memcpy(gbe + GBE_MAC_OFFSET, gbeMac, sizeof(gbeMac)))
        {
            fprintf(stderr, "Memcpy failed\nGbE MAC can't be copied\n");    
            return ERR_MEMORY;
        }
        
        printf("GbE MAC address copied\n");
    }

    /* Searching for MSOA-containing module and add SLIC pubkey and marker if found */
    if(copySLIC && hasSLIC)
    {
        unsigned char* msoa_module;
        unsigned char* pubkey_module;
        unsigned char* marker_module;

        do
        {
            pubkey_module = findpattern(buffer, filesize, SLIC_PUBKEY_HEADER, sizeof(SLIC_PUBKEY_HEADER));
            marker_module = findpattern(buffer, filesize, SLIC_MARKER_HEADER, sizeof(SLIC_MARKER_HEADER));
            if(pubkey_module ||  marker_module)
            {
                fprintf(stderr, "SLIC pubkey or marker found in output file\nSLIC table copy is not needed\n");
                break;
            }

            msoa_module = findpattern(buffer, filesize, MSOA_MODULE_HEADER, sizeof(MSOA_MODULE_HEADER));
            if(!msoa_module)
            {
                fprintf(stderr, "MSOA module not found in output file\nSLIC table can't be copied\n");
                break;
            }

            pubkey_module = findfreespace(msoa_module, buffer + filesize - 1, SLIC_FREE_SPACE_LENGTH);
            if(!pubkey_module)
            {
                fprintf(stderr, "Not enough free space to insert pubkey module\nSLIC table can't be copied\n");       
                break;
            }
            
            /* Writing pubkey header*/
            if(!memcpy(pubkey_module, SLIC_PUBKEY_HEADER, sizeof(SLIC_PUBKEY_HEADER)))
            {
                fprintf(stderr, "Memcpy failed\nSLIC table can't be copied\n");       
                return ERR_MEMORY;
            }
            /* Writing pubkey first part */
            if(!memcpy(pubkey_module + sizeof(SLIC_PUBKEY_HEADER), SLIC_PUBKEY_PART1, sizeof(SLIC_PUBKEY_PART1)))
            {
                fprintf(stderr, "Memcpy failed\nSLIC table can't be copied\n");       
                return ERR_MEMORY;
            }
            /* Writing pubkey*/ 
             if(!memcpy(pubkey_module + sizeof(SLIC_PUBKEY_HEADER) + sizeof(SLIC_PUBKEY_PART1), slicPubkey, sizeof(slicPubkey)))
            {
                fprintf(stderr, "Memcpy failed\nSLIC table can't be copied\n");       
                return ERR_MEMORY;
            }

            marker_module = findfreespace(pubkey_module, buffer + filesize - 1, SLIC_MARKER_LENGTH + 8);
            if(!marker_module)
            {
                fprintf(stderr, "Not enough free space to insert marker module\nSLIC table can't be copied\n");       
                return ERR_MEMORY;
            }

            /* Writing marker header*/
            if(!memcpy(marker_module, SLIC_MARKER_HEADER, sizeof(SLIC_MARKER_HEADER)))
            {
                fprintf(stderr, "Memcpy failed\nSLIC table can't be copied\n");       
                return ERR_MEMORY;
            }
            /* Writing marker first part*/
            if(!memcpy(marker_module + sizeof(SLIC_MARKER_HEADER), SLIC_MARKER_PART1, sizeof(SLIC_MARKER_PART1)))
            {
                fprintf(stderr, "Memcpy failed\nSLIC table can't be copied\n");       
                return ERR_MEMORY;
            }
            /* Writing marker */
            if(!memcpy(marker_module + sizeof(SLIC_MARKER_HEADER) + sizeof(SLIC_MARKER_PART1), slicMarker, sizeof(slicMarker)))
            {
                fprintf(stderr, "Memcpy failed\nSLIC table can't be copied\n");       
                return ERR_MEMORY;
            }
            
            printf("SLIC pubkey and marker copied\n");
        } while (0);
    }

    /* Searching for module header */
    if(copyModule)
    {
        fd44 = findpattern(buffer, filesize, FD44_MODULE_HEADER, sizeof(FD44_MODULE_HEADER));
        if (!fd44)
        {
            fprintf(stderr, "FD44 module not found in output file\n");
            return ERR_MODULE_NOT_FOUND;
        }

        /* Replacing all BSA_ modules */
        rest = filesize - (fd44 - buffer) - FD44_MODULE_HEADER_LENGTH - FD44_MODULE_LENGTH;
        while(fd44)
        {
            if (!memcmp(fd44 + FD44_MODULE_HEADER_BSA_OFFSET, FD44_MODULE_HEADER_BSA, sizeof(FD44_MODULE_HEADER_BSA)))
            {
                module = fd44 + FD44_MODULE_HEADER_LENGTH;
                if(!memcpy(module, fd44Module, sizeof(fd44Module)))
                {
                    fprintf(stderr, "Memcpy failed\nFD44 module can't be copied\n");       
                    return ERR_MEMORY;
                }
            }
            fd44 = findpattern(fd44 + FD44_MODULE_LENGTH + FD44_MODULE_HEADER_LENGTH, rest, FD44_MODULE_HEADER, sizeof(FD44_MODULE_HEADER));
            rest = filesize - (fd44 - buffer) - FD44_MODULE_HEADER_LENGTH - FD44_MODULE_LENGTH;
        }
        printf("FD44 module copied\n");
    }

    /* Reopening file to resize it */
    fclose(file);
    file = fopen(outputfile, "wb");

    /* Writing buffer to output file */
    read = fwrite(buffer, sizeof(char), filesize, file);
    if (read != filesize)
    {
        fprintf(stderr, "Can't write output file\n");
        return ERR_OUTPUT_FILE;
    }

    if (hasUbf)
        buffer -= UBF_FILE_HEADER_SIZE;
    free(buffer);
    fclose(file);

    return ERR_OK;
}
