#ifndef BIOS_H
#define BIOS_H

/* USB BIOS Flashback file header */
const unsigned char UBF_FILE_HEADER[] =                  {'\x8B','\xA6','\x3C','\x4A','\x23',
                                                          '\x77','\xFB','\x48','\x80','\x3D',
                                                          '\x57','\x8C','\xC1','\xFE','\xC4',
                                                          '\x4D'};
#define UBF_FILE_HEADER_SIZE 0x800

/* BOOTEFI marker */
const unsigned char BOOTEFI_HEADER[] =                   {'$','B','O','O','T','E','F','I','$'};
#define BOOTEFI_MOTHERBOARD_NAME_OFFSET 14
#define BOOTEFI_MOTHERBOARD_NAME_LENGTH 60

/* ME */
static const unsigned char ME_HEADER[] =                 {'\x20','\x20','\x80','\x0F','\x40',
                                                          '\x00','\x00','\x10','\x00','\x00',
                                                          '\x00','\x00','\x00','\x00','\x00',
                                                          '\x00'};

/* GbE */
const unsigned char GBE_HEADER[] =                       {'\xFF','\xFF','\xFF','\xFF','\xFF',
                                                          '\xFF','\xFF','\xFF','\xC3','\x10'};
#define GBE_MAC_OFFSET (-12)
#define GBE_MAC_LENGTH 6
static const unsigned char GBE_MAC_STUB[] =              {'\x88','\x88','\x88','\x88','\x87',
                                                          '\x88'};
/* SLIC */
const unsigned char MSOA_MODULE_HEADER[] =               {'\xB9','\x2A','\x90','\xA1','\x94',
                                                          '\x53','\xF2','\x45','\x85','\x7A',
                                                          '\x12','\x82','\x42','\x13','\xEE',
                                                          '\xFB'};
const unsigned char SLIC_PUBKEY_HEADER[] =               {'\xFB','\xEB','\xFF','\xCD','\xDC',
                                                          '\x17','\xBC','\x46','\x9B','\x75',
                                                          '\x59','\xB8','\x61','\x92','\x09',
                                                          '\x13','\x78','\x02','\x02','\x40',
                                                          '\x6E','\x01','\x00','\xF8','\x56',
                                                          '\x01','\x00','\x19'};
#define SLIC_PUBKEY_LENGTH 366
const unsigned char SLIC_MARKER_HEADER[] =               {'\x58','\x44','\x63','\x15','\xA4',
                                                          '\xE8','\x6D','\x43','\xAC','\x2F',
                                                          '\x57','\xE3','\x3E','\x53','\x4C',
                                                          '\xCF','\x75','\x4E','\x02','\x40',
                                                          '\x38','\x00','\x00','\xF8','\x20',
                                                          '\x00','\x00','\x19'};
#define SLIC_MARKER_LENGTH 56
#define SLIC_FREE_SPACE_LENGTH 3096

/* FD44 */
const unsigned char FD44_MODULE_HEADER[] =               {'\x0B','\x82','\x44','\xFD','\xAB',
                                                          '\xF1','\xC0','\x41','\xAE','\x4E',
                                                          '\x0C','\x55','\x55','\x6E','\xB9',
                                                          '\xBD'};
#define FD44_MODULE_HEADER_BSA_OFFSET 28
const unsigned char FD44_MODULE_HEADER_BSA[] =           {'B', 'S', 'A', '_'};
#define FD44_MODULE_HEADER_LENGTH 36

#define FD44_MODULE_LENGTH 552

/* ASUSBKP */
const unsigned char ASUSBKP_HEADER[] =                  {'A','S','U','S','B','K','P','$'};
const unsigned char ASUSBKP_PUBKEY_HEADER[] =           {'S','2','L','P','R','\x01','\x00','\x00'};
const unsigned char ASUSBKP_MARKER_HEADER[] =           {'K','E','Y','S','\x1C','\x00','\x00','\x00'};

#endif /* BIOS_H */
