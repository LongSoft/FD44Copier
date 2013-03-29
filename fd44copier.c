#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "bios.h"

/* Return codes */
#define ERR_OK                      0
#define ERR_EMPTY_FD44_MODULE       1
#define ERR_ARGS                    2
#define ERR_INPUT_FILE              3
#define ERR_OUTPUT_FILE             4
#define ERR_MEMORY                  5
#define ERR_NO_FD44_MODULE          6
#define ERR_DIFFERENT_BOARD         7
#define ERR_NO_GBE                  8
#define ERR_NO_SLIC                 9

/* Implementation of GNU memmem function using Boyer-Moore-Horspool algorithm
*  Returns pointer to the beginning of found pattern of NULL if not found */
uint8_t* find_pattern(uint8_t* begin, uint8_t* end, const uint8_t* pattern, uint32_t plen)
{
    uint32_t scan = 0;
    uint32_t bad_char_skip[256];
    uint32_t last;
    uint32_t slen;

    if (plen == 0 || !begin || !pattern || !end || end <= begin)
        return NULL;

    slen = end - begin;

    for (scan = 0; scan <= 255; scan++)
        bad_char_skip[scan] = plen;

    last = plen - 1;

    for (scan = 0; scan < last; scan++)
        bad_char_skip[pattern[scan]] = last - scan;

    while (slen >= plen)
    {
        for (scan = last; begin[scan] == pattern[scan]; scan--)
            if (scan == 0)
                return begin;

        slen     -= bad_char_skip[begin[last]];
        begin   += bad_char_skip[begin[last]];
    }

    return NULL;
}

/* Finds free space between begin and end to insert new module.
 * Returns aligned pointer to empty space or NULL if it can't be found. */
uint8_t* find_free_space(uint8_t* begin, uint8_t* end, uint32_t space_length)
{
    uint8_t* current;
    uint32_t size;
    uint32_t allignment;

    current = end;
    
    // Skipping 0xFF bytes from end
    while (*current-- == 0xFF);
    current++;

    // Error if begin passed, all bytes are 0xFF, which is incorrect
    if (current < begin)
        return NULL;

    // Alligning pounter to 8
    size = current - begin;
    if (size % 8)
        allignment = 8 - size % 8;
    else
        allignment = 0;
    
    if (size + allignment < space_length)
        return NULL;

    return current + allignment;
}

/* Calculates 2's complement 8-bit checksum of data from data[0] to data[length-1] and stores it to *checksum
 * Returns 1 on success and 0 on failure */
int calculate_checksum(uint8_t* data, uint32_t length, uint8_t* checksum)
{
    uint8_t counter;

    if (!data || !length || !checksum)
        return 0;
    counter = 0;
    while (length--)
        counter += data[length];
    *checksum = ~counter + 1;
    return 1;
}

/* Converts SIZE field of MODULE_HEADER (3 bytes in reversed order) to uint32_t.
 * Returns 1 on success or 0 on error */
int size2int(uint8_t* module_size, uint32_t* size)
{
    if (!module_size || !size)
        return 0;

    *size = (module_size[2] << 16) + 
            (module_size[1] << 8) + 
             module_size[0];
    return 1;
}

/* Entry point */
int main(int argc, char* argv[])
{
    FILE* file;                                                                 /* file pointer to work with input and output files */
    char* inputfile;                                                            /* path to input file*/
    char* outputfile;                                                           /* path to output file */
    uint8_t* buffer;                                                      /* buffer to read input and output file */
    uint8_t* end;                                                         /* pointer to the end of buffer */
    uint32_t filesize;                                                          /* size of opened file */
    uint32_t read;                                                              /* read bytes counter */
    uint8_t* bootefi;                                                     /* BOOTEFI header */
    uint8_t* capsuleHeader;                                               /* Capsule header */
    int8_t hasCapsuleHeader;                                                      /* flag that output file has capsule header */
    int8_t hasGbe;                                                                /* flag that input file has GbE region */
    int8_t hasSLIC;                                                               /* flag that input file has SLIC pubkey and marker */
    int8_t isModuleEmpty;                                                         /* flag that FD44 module is empty in input file */
    int8_t defaultOptions;                                                        /* flag that program is ran with default options */
    int8_t copyModule;                                                            /* flag that FD44 module copying is requested */
    int8_t copyGbe;                                                               /* flag that GbE MAC copying is requested */
    int8_t copySLIC;                                                              /* flag that SLIC copying is requested */
    int8_t skipMotherboardNameCheck;                                              /* flag that motherboard name in output file doesn't need to be checked */
    uint8_t motherboardName[BOOTEFI_MOTHERBOARD_NAME_LENGTH];             /* motherboard name storage */
    uint8_t gbeMac[GBE_MAC_LENGTH];                                       /* GbE MAC storage */
    uint8_t slicPubkey[SLIC_PUBKEY_LENGTH                                 /* SLIC----*/
                             - sizeof(SLIC_PUBKEY_HEADER)                       /* pubkey--*/
                             - sizeof(SLIC_PUBKEY_PART1)];                      /* storage */
    uint8_t slicMarker[SLIC_MARKER_LENGTH                                 /* SLIC----*/
                             - sizeof(SLIC_MARKER_HEADER)                       /* marker--*/
                             - sizeof(SLIC_MARKER_PART1)];                      /* storage */
    uint8_t* fd44Module;                                                  /* FD44 module storage, will be allocated later */
    uint32_t fd44ModuleSize;                                                /* Size of FD44 module */
    

    if (argc < 3 || (argv[1][0] == '-' && argc < 4))
    {
        printf("FD44Copier v0.6.9\nThis program copies GbE MAC address, FD44 module data,\n"\
               "SLIC pubkey and marker from one BIOS image file to another.\n\n"
               "Usage: FD44Copier <-OPTIONS> INFILE OUTFILE\n\n"
               "Options: m - copy module data.\n"
               "         g - copy GbE MAC address.\n"
               "         s - copy SLIC pubkey and marker.\n"
               "         n - do not check that both BIOS files are for same motherboard.\n"
               "         <none> - copy all available data and check for same motherboard in both BIOS files.\n\n");
        return ERR_ARGS;
    }

    /* Checking for options presence and setting options */
    if (argv[1][0] == '-')
    {
        /* Setting supplied options */
        copyModule = (strchr(argv[1], 'm') != NULL);
        copyGbe =    (strchr(argv[1], 'g') != NULL);
        copySLIC =   (strchr(argv[1], 's') != NULL);
        skipMotherboardNameCheck =
                     (strchr(argv[1], 'n') != NULL);
        defaultOptions = 0;
        inputfile = argv[2];
        outputfile = argv[3];
    }
    else
    {
        /* Setting default options */
        defaultOptions =            1;
        copyModule =                1;
        copyGbe =                   1;
        copySLIC =                  1;
        skipMotherboardNameCheck =  0;
        inputfile =  argv[1];
        outputfile = argv[2];
    }

     /* Opening input file */
    file = fopen(inputfile, "rb");
    if (!file)
    {
        perror("Can't open input file.\n");
        return ERR_INPUT_FILE;
    }

    /* Determining file size */
    fseek(file, 0, SEEK_END);
    filesize = ftell(file);
    fseek(file, 0, SEEK_SET);

    /* Allocating memory for buffer */
    buffer = (uint8_t*)malloc(filesize);
    if (!buffer)
    {
        printf("Can't allocate memory for input file.\n");
        return ERR_MEMORY;
    }
    end = buffer + filesize;

    /* Reading whole file to buffer */
    read = fread((void*)buffer, sizeof(char), filesize, file);
    if (read != filesize)
    {
        perror("Can't read input file.\n");
        return ERR_INPUT_FILE;
    }

    /* Searching for bootefi signature */
    bootefi = find_pattern(buffer, end, BOOTEFI_HEADER, sizeof(BOOTEFI_HEADER));
    if (!bootefi)
    {
        printf("ASUS BIOS file signature not found in input file.\n");
        return ERR_INPUT_FILE;
    }
    
    /* Storing motherboard name */
    if (!skipMotherboardNameCheck && !memcpy(motherboardName, bootefi + BOOTEFI_MOTHERBOARD_NAME_OFFSET, sizeof(motherboardName)))
    {
        printf("Memcpy failed.\nMotherboard name can't be stored.\n");
        return ERR_MEMORY;
    }

    /* Searching for GbE and storing MAC address if it is found */
    if (copyGbe)
    {
        uint8_t* gbe = find_pattern(buffer, end, GBE_HEADER, sizeof(GBE_HEADER));
        hasGbe = 0;
        if (gbe)
        {
            hasGbe = 1;
            /* Checking if first GbE is a stub */
            if (!memcmp(gbe + GBE_MAC_OFFSET, GBE_MAC_STUB, sizeof(GBE_MAC_STUB)))
            {
                uint8_t* gbe2;
                gbe2 = find_pattern(gbe + sizeof(GBE_HEADER), end, GBE_HEADER, sizeof(GBE_HEADER));
                /* Checking if second GbE is not a stub */
                if(gbe2 && memcmp(gbe2 + GBE_MAC_OFFSET, GBE_MAC_STUB, sizeof(GBE_MAC_STUB)))
                    gbe = gbe2;
            }

            if (!memcpy(gbeMac, gbe + GBE_MAC_OFFSET, GBE_MAC_LENGTH))
            {
                printf("Memcpy failed.\nGbE MAC can't be copied.\n");
                return ERR_MEMORY;
            }
        }

        if (!defaultOptions && !hasGbe)
        {
            printf("GbE region not found in input file, but required by -g option.\n");
            return ERR_NO_GBE;
        }
    }

    /* Searching for SLIC pubkey and marker and storing them if found*/
    if (copySLIC)
    {
        uint8_t* slic_pubkey = find_pattern(buffer, end, SLIC_PUBKEY_HEADER, sizeof(SLIC_PUBKEY_HEADER));
        uint8_t* slic_marker = find_pattern(buffer, end, SLIC_MARKER_HEADER, sizeof(SLIC_MARKER_HEADER));
        hasSLIC = 0;
        if (slic_pubkey && slic_marker)
        {
            slic_pubkey += sizeof(SLIC_PUBKEY_HEADER) + sizeof(SLIC_PUBKEY_PART1);
            slic_marker += sizeof(SLIC_MARKER_HEADER) + sizeof(SLIC_MARKER_PART1);
            if (!memcpy(slicPubkey, slic_pubkey, sizeof(slicPubkey)))
            {
                printf("Memcpy failed.\nSLIC pubkey can't be copied.\n");
                return ERR_MEMORY;
            }
            if (!memcpy(slicMarker, slic_marker, sizeof(slicMarker)))
            {
                printf("Memcpy failed.\nSLIC marker can't be copied.\n");
                return ERR_MEMORY;
            }
            hasSLIC = 1;
        }
        else /* If SLIC headers not found, searching for SLIC pubkey and marker in ASUSBKP module */
        {
            uint8_t* asusbkp = find_pattern(buffer, end, ASUSBKP_HEADER, sizeof(ASUSBKP_HEADER));
            if (asusbkp)
            {
                slic_pubkey = find_pattern(asusbkp, end, ASUSBKP_PUBKEY_HEADER, sizeof(ASUSBKP_PUBKEY_HEADER));
                slic_marker = find_pattern(asusbkp, end, ASUSBKP_MARKER_HEADER, sizeof(ASUSBKP_MARKER_HEADER));
                if (slic_pubkey && slic_marker)
                {
                    slic_pubkey += sizeof(ASUSBKP_PUBKEY_HEADER);
                    slic_marker += sizeof(ASUSBKP_MARKER_HEADER);
                    if (!memcpy(slicPubkey, slic_pubkey, sizeof(slicPubkey)))
                    {
                        printf("Memcpy failed\nSLIC pubkey can't be copied.\n");
                        return ERR_MEMORY;
                    }
                    if (!memcpy(slicMarker, slic_marker, sizeof(slicMarker)))
                    {
                        printf("Memcpy failed\nSLIC marker can't be copied.\n");
                        return ERR_MEMORY;
                    }
                    hasSLIC = 1;
                }
            }
        }

        if (!defaultOptions && !hasSLIC)
        {
            printf("SLIC pubkey and marker not found in input file, but required by -s option.\n");
            return ERR_NO_SLIC;
        }
    }

    /* Searching for FD44 module header */
    if (copyModule)
    {
        uint8_t* module;
        uint8_t* fd44 = find_pattern(buffer, end, FD44_MODULE_HEADER, sizeof(FD44_MODULE_HEADER));
        isModuleEmpty = 1;
        if (!fd44)
        {
            printf("FD44 module not found in input file.\n");
            return ERR_NO_FD44_MODULE;
        }

        /* Looking for non-empty module */
        while(isModuleEmpty && fd44)
        {

            /* Getting module size */
            size2int(fd44 + FD44_MODULE_SIZE_OFFSET, &fd44ModuleSize);
            
            /* Checking that module has BSA signature */
            if (!memcmp(fd44 + FD44_MODULE_HEADER_BSA_OFFSET, FD44_MODULE_HEADER_BSA, sizeof(FD44_MODULE_HEADER_BSA)))
            {
                uint32_t pos;
                /* Looking for non-FF byte starting from the beginning of data */
                module = fd44 + FD44_MODULE_HEADER_LENGTH;
                for (pos = 0; pos < fd44ModuleSize - FD44_MODULE_HEADER_LENGTH; pos++)
                {
                    /* If found - this module is not empty */
                    if (module[pos] != 0xFF)
                    {
                        isModuleEmpty = 0;
                        break;
                    }
                }
            }

            /* Finding next module */
            fd44 = find_pattern(fd44 + FD44_MODULE_HEADER_LENGTH, end, FD44_MODULE_HEADER, sizeof(FD44_MODULE_HEADER));
        }

        /* Checking if all modules are empty */
        if (isModuleEmpty)
        {
            printf("FD44 modules are empty in input file. Data restoration required.\nUse FD44Editor to restore your data.\n");
        }
        else /* Storing module contents */       
        {
            /* No need to store module header */
            fd44ModuleSize -= FD44_MODULE_HEADER_LENGTH;
        
            /* No need to store FF bytes */
            while (module[--fd44ModuleSize] == 0xFF)
                ;
            fd44ModuleSize++;

            /* Allocating memory for module storage */
            fd44Module = (uint8_t*)malloc(fd44ModuleSize);
            if (!fd44Module)
            {
                printf("Can't allocate memory for FD44 module.\nFD44 module can't be copied.\n");
                return ERR_MEMORY;
            }

            /* Storing module contents */
            if (!memcpy(fd44Module, module, fd44ModuleSize))
            {
                printf("Memcpy failed.\nFD44 module can't be copied.\n");
                return ERR_MEMORY;
            }
        }
    }

    /* Closing input file */
    free(buffer);
    fclose(file);
    
    /* Opening output file */
    file = fopen(outputfile, "r+b");
    if (!file)
    {
        perror("Can't open output file.\n");
        return ERR_OUTPUT_FILE;
    }

    /* Determining file size */
    fseek(file, 0, SEEK_END);
    filesize = ftell(file);
    fseek(file, 0, SEEK_SET);

    /* Allocating memory for buffer */
    buffer = (uint8_t*)malloc(filesize);
    if (!buffer)
    {
        printf("Can't allocate memory for output file.\n");
        return ERR_MEMORY;
    }
    
    /* Reading whole file to buffer */
    read = fread((void*)buffer, sizeof(char), filesize, file);
    if (read != filesize)
    {
        perror("Can't read output file.\n");
        return ERR_OUTPUT_FILE;
    }

    /* Searching for capsule file signature, if found - remove capsule file header */
    hasCapsuleHeader = 0;
    capsuleHeader = find_pattern(buffer, buffer + sizeof(CAPSULE_FILE_HEADER), CAPSULE_FILE_HEADER, sizeof(CAPSULE_FILE_HEADER));
    if (capsuleHeader)
    {
        hasCapsuleHeader = 1;
        buffer += CAPSULE_FILE_HEADER_SIZE;
        filesize -= CAPSULE_FILE_HEADER_SIZE;
    }
    end = buffer + filesize;

    /* Searching for bootefi signature */
    bootefi = find_pattern(buffer, end, BOOTEFI_HEADER, sizeof(BOOTEFI_HEADER));
    if (!bootefi)
    {
        printf("ASUS BIOS file signature not found in output file.\n");
        return ERR_OUTPUT_FILE;
    }

    /* Checking motherboard name */
    if (!skipMotherboardNameCheck && memcmp(motherboardName, bootefi + BOOTEFI_MOTHERBOARD_NAME_OFFSET, strlen((const char*)motherboardName)))
    {
        printf("Motherboard name in output file differs from motherboard name in input file.\n");
        return ERR_DIFFERENT_BOARD;
    }

    /* If input file had GbE block, searching for it in output file and replacing it */
    if (copyGbe && hasGbe)
    {
        /* First GbE block */
        uint8_t* gbe = find_pattern(buffer, end, GBE_HEADER, sizeof(GBE_HEADER));
        if (!gbe)
        {
            printf("GbE region not found in output file.\n");
            return ERR_NO_GBE;
        }
        if (!memcpy(gbe + GBE_MAC_OFFSET, gbeMac, sizeof(gbeMac)))
        {
            printf("Memcpy failed.\nGbE MAC can't be copied.\n");
            return ERR_MEMORY;
        }

        /* Second GbE block */
        gbe = find_pattern(gbe + sizeof(GBE_HEADER), end, GBE_HEADER, sizeof(GBE_HEADER));
        
        if (gbe && !memcpy(gbe + GBE_MAC_OFFSET, gbeMac, sizeof(gbeMac)))
        {
            printf("Memcpy failed.\nGbE MAC can't be copied.\n");
            return ERR_MEMORY;
        }
        
        printf("GbE MAC address copied.\n");
    }

    /* Searching for EFI volume containing MSOA module and add SLIC pubkey and marker modules if found */
    if (copySLIC && hasSLIC)
    {
        uint8_t* efi_volume_begin;
        uint8_t* efi_volume_end;
        uint8_t* msoa_module;
        uint8_t* pubkey_module;
        uint8_t* marker_module;
        uint8_t  data_checksum;
        
        do
        {
            /* Searching for existing SLIC modules */
            pubkey_module = find_pattern(buffer, end, SLIC_PUBKEY_HEADER, sizeof(SLIC_PUBKEY_HEADER));
            marker_module = find_pattern(buffer, end, SLIC_MARKER_HEADER, sizeof(SLIC_MARKER_HEADER));
            if (pubkey_module ||  marker_module)
            {
                printf("SLIC pubkey or marker found in output file.\nSLIC table copy is not needed.\n");
                break;
            }

            /* Searching for second EFI Volume to instert SLIC */
            efi_volume_begin = find_pattern(buffer, end, EFI_VOLUME_HEADER, sizeof(EFI_VOLUME_HEADER));
            if (!efi_volume_begin)
            {
                printf("First EFI volume not found in output file. The file is possibly corrupted. SLIC table can't be inserted.");
                break;
            }
            efi_volume_end = efi_volume_begin + *(uint32_t*)(efi_volume_begin + sizeof(EFI_VOLUME_HEADER));
            efi_volume_begin = find_pattern(efi_volume_end, end, EFI_VOLUME_HEADER, sizeof(EFI_VOLUME_HEADER));
            if (!efi_volume_begin)
            {
                printf("Second EFI volume not found in output file. The file is possibly corrupted. SLIC table can't be inserted.");
                break;
            }
            efi_volume_end = efi_volume_begin + *(uint32_t*)(efi_volume_begin + sizeof(EFI_VOLUME_HEADER)) - 16; 
            
            /* Searching for DummyMSOA or MSOA module */
            msoa_module = find_pattern(efi_volume_begin, efi_volume_end, DUMMY_MSOA_MODULE_HEADER, sizeof(DUMMY_MSOA_MODULE_HEADER));
            if (!msoa_module)
            {
                msoa_module = find_pattern(efi_volume_begin, efi_volume_end, MSOA_MODULE_HEADER, sizeof(MSOA_MODULE_HEADER));
                if (!msoa_module)
                {
                    printf("DummyMSOA and MSOA module not found in first EFI volume.\nSLIC table can't be copied.\n");
                    break;
                }
            }

            /* Searching for free space at the end of EFI volume with MSOA module to insert pubkey module */
            pubkey_module = find_free_space(efi_volume_begin, efi_volume_end, SLIC_PUBKEY_LENGTH + SLIC_MARKER_LENGTH);
            if (!pubkey_module)
            {
                printf("Not enough free space to insert SLIC modules.\nSLIC table can't be copied.\n");
                break;
            }
            
            /* Writing pubkey header */
            if (!memcpy(pubkey_module, SLIC_PUBKEY_HEADER, sizeof(SLIC_PUBKEY_HEADER)))
            {
                printf("Memcpy failed.\nSLIC table can't be copied.\n");
                return ERR_MEMORY;
            }
            /* Writing pubkey first part */
            if (!memcpy(pubkey_module + sizeof(SLIC_PUBKEY_HEADER), SLIC_PUBKEY_PART1, sizeof(SLIC_PUBKEY_PART1)))
            {
                printf("Memcpy failed.\nSLIC table can't be copied.\n");
                return ERR_MEMORY;
            }
            /* Writing pubkey */
            if (!memcpy(pubkey_module + sizeof(SLIC_PUBKEY_HEADER) + sizeof(SLIC_PUBKEY_PART1), slicPubkey, sizeof(slicPubkey)))
            {
                printf("Memcpy failed.\nSLIC table can't be copied.\n");
                return ERR_MEMORY;
            }
            /* Calculating pubkey module data checksum */
            if (!calculate_checksum(pubkey_module + MODULE_DATA_CHECKSUM_START, SLIC_PUBKEY_LENGTH - MODULE_DATA_CHECKSUM_START, &data_checksum))
            {
                printf("Pubkey module checksum calculation failed.\nSLIC table can't be copied.\n");
                return ERR_MEMORY;
            }
            /* Writing pubkey module data checksum */
            pubkey_module[MODULE_DATA_CHECKSUM_OFFSET] = data_checksum;

            /* Searching for free space to insert marker module */
            marker_module = find_free_space(pubkey_module, efi_volume_end, SLIC_MARKER_LENGTH);
            if (!marker_module)
            {
                printf("Not enough free space to insert marker module.\nSLIC table can't be copied.\n");
                return ERR_MEMORY;
            }

            /* Writing marker header*/
            if (!memcpy(marker_module, SLIC_MARKER_HEADER, sizeof(SLIC_MARKER_HEADER)))
            {
                printf("Memcpy failed.\nSLIC table can't be copied.\n");
                return ERR_MEMORY;
            }
            /* Writing marker first part*/
            if (!memcpy(marker_module + sizeof(SLIC_MARKER_HEADER), SLIC_MARKER_PART1, sizeof(SLIC_MARKER_PART1)))
            {
                printf("Memcpy failed.\nSLIC table can't be copied.\n");
                return ERR_MEMORY;
            }
            /* Writing marker */
            if (!memcpy(marker_module + sizeof(SLIC_MARKER_HEADER) + sizeof(SLIC_MARKER_PART1), slicMarker, sizeof(slicMarker)))
            {
                printf("Memcpy failed.\nSLIC table can't be copied.\n");
                return ERR_MEMORY;
            }
            /* Calculating pubkey module data checksum */
            if (!calculate_checksum(marker_module + MODULE_DATA_CHECKSUM_START, SLIC_MARKER_LENGTH - MODULE_DATA_CHECKSUM_START, &data_checksum))
            {
                printf("Marker module checksum calculation failed.\nSLIC table can't be copied.\n");
                return ERR_MEMORY;
            }
            /* Writing marker module data checksum */
            marker_module[MODULE_DATA_CHECKSUM_OFFSET] = data_checksum;

            printf("SLIC pubkey and marker copied.\n");
        } while (0); /* Used for break */
    }

    /* Searching for module header */
    if (copyModule)
    {
        char isCopied;
        uint32_t currentModuleSize;
        uint8_t* module;
        uint8_t* fd44 = find_pattern(buffer, end, FD44_MODULE_HEADER, sizeof(FD44_MODULE_HEADER));
        if (!fd44)
        {
            printf("FD44 module not found in output file.\n");
            return ERR_NO_FD44_MODULE;
        }

        if (!isModuleEmpty)
        {
            /* Copying data to all BSA_ modules */
            isCopied = 0;
            while (fd44)
            {
                /* Getting module size */
                size2int(fd44 + FD44_MODULE_SIZE_OFFSET, &currentModuleSize);
                if (!memcmp(fd44 + FD44_MODULE_HEADER_BSA_OFFSET, FD44_MODULE_HEADER_BSA, sizeof(FD44_MODULE_HEADER_BSA)))
                {
                    module = fd44 + FD44_MODULE_HEADER_LENGTH;
                    /* Checking that there is enough space in module to insert data */
                    if (currentModuleSize - FD44_MODULE_HEADER_LENGTH < fd44ModuleSize)
                    {
                        printf("FD44 module at %08X is too small.\n", module - buffer);
                        fd44 = find_pattern(fd44 + currentModuleSize, end, FD44_MODULE_HEADER, sizeof(FD44_MODULE_HEADER));
                        break;
                    }
                    /* Copying module data*/
                    if (!memcpy(module, fd44Module, fd44ModuleSize))
                    {
                        printf("Memcpy failed.\nFD44 module can't be copied.\n");
                        return ERR_MEMORY;
                    }
                    isCopied = 1;
                }
            
                fd44 = find_pattern(fd44 + currentModuleSize, end, FD44_MODULE_HEADER, sizeof(FD44_MODULE_HEADER));
            }
                
            /* Checking if there is at least one non-empty module after copying */
            if(isCopied)
                printf("FD44 module copied.\n");
            else
            {
                printf("FD44 module can't be copied.\n");
                return ERR_NO_FD44_MODULE;
            }
        }
    }

    /* Reopening file to resize it */
    fclose(file);
    file = fopen(outputfile, "wb");

    /* Writing buffer to output file */
    read = fwrite(buffer, sizeof(char), filesize, file);
    if (read != filesize)
    {
        perror("Can't write output file.\n");
        return ERR_OUTPUT_FILE;
    }

    /* Cleaning */
    if (hasCapsuleHeader)
    {
        printf("Capsule file header removed.\n");
        buffer -= CAPSULE_FILE_HEADER_SIZE;
    }
    free(buffer);
    if (copyModule && !isModuleEmpty)
        free(fd44Module);
    fclose(file);

    if (isModuleEmpty)
        return ERR_EMPTY_FD44_MODULE;

    return ERR_OK;
}
