/**
 * @file   partition.h
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @brief  declaration of various structures and functions that manage
 *         MBRs or GPTs, respectively.
 *
 * [MIT license]
 *
 * Copyright (c) 2021 Ingo A. Kubbilun
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef _INC_PARTITION_H_
#define _INC_PARTITION_H_

#include <part-y.h>
#include <disk.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MBR_IS_EXTENDED_PARTITION(_type)    ((0x05 == (_type)) || (0x0F == (_type)) || (0x85 == (_type)))

#define FSYS_UNKNOWN                    0x00000000
#define FSYS_WIN_FAT12                  0x00000001
#define FSYS_WIN_FAT16                  0x00000002
#define FSYS_WIN_FAT32                  0x00000003
#define FSYS_WIN_EXFAT                  0x00000004
#define FSYS_WIN_NTFS                   0x00000005
#define FSYS_LINUX_EXT2                 0x00000006
#define FSYS_LINUX_EXT3                 0x00000007
#define FSYS_LINUX_EXT4                 0x00000008

typedef struct _mbr_part_sector         mbr_part_sector, * mbr_part_sector_ptr;
typedef struct _mbr_entry               mbr_entry, * mbr_entry_ptr;

struct _mbr_entry
{
  uint32_t                  head_first;                   ///< C-H-S is evil now... (see start_sector = LBA below)
  uint32_t                  sector_first;
  uint32_t                  cylinder_first;

  uint32_t                  head_last;                    ///< C-H-S is evil now... (see start_sector = LBA below)
  uint32_t                  sector_last;
  uint32_t                  cylinder_last;

  uint64_t                  start_sector;                 ///< 32bit in partition tables; but because of relative extended partitions, can be more
  uint32_t                  num_sectors;

  uint32_t                  fs_type;                      ///< one of the FSYS_xxx flags

  uint8_t                   uuid[16];                     ///< UUID of Linux EXT2/3/4

  char                      type_desc[64];                ///< can be empty string (unknown) or partition type description

  uint8_t                   boot_flag;                    ///< 0x00 not active, 0x80 if active
  uint8_t                   part_type;
};

typedef struct _sector     *sector_ptr;                   ///< forward definition

struct _mbr_part_sector
{
  mbr_part_sector_ptr       next;                         ///< chaining of MBR with extended partition tables
  mbr_part_sector_ptr       prev;                         ///< chaining of MBR with extended partition tables

  sector_ptr                sp;                           ///< this is the sector pointer for this partition sector (it is the head of the double linked list if this is the MBR at LBA 0 of the disk)

  uint32_t                  disk_signature;               ///< only meaningful for the MBR at LBA 0

  uint8_t                   boot_sector_signature1;       ///< must be 0x55
  uint8_t                   boot_sector_signature2;       ///< must be 0xAA

  uint8_t                   ext_part_no;                  ///< 0xFF if no extended partition; or entry 0..3 (MBR), constantly 1 (extended part. table)

  mbr_entry                 part_table[4];                ///< only entries 0 and 1 filled for extended partition sector
};

#define GPT_ATTR_SYSTEM_PARTITION       ((uint64_t)0x0000000000000001)
#define GPT_ATTR_HIDE_EFI               ((uint64_t)0x0000000000000002)
#define GPT_ATTR_LEGACY_BIOS_BOOT       ((uint64_t)0x0000000000000004)    ///< is the 0x80 boot flag in the MBR
#define GPT_ATTR_READ_ONLY              ((uint64_t)0x1000000000000000)
#define GPT_ATTR_HIDDEN                 ((uint64_t)0x4000000000000000)
#define GPT_ATTR_DO_NOT_MOUNT           ((uint64_t)0x8000000000000000)

typedef struct _gpt_header              gpt_header, * gpt_header_ptr;
typedef struct _gpt_entry               gpt_entry, * gpt_entry_ptr;
typedef struct _gpt                     gpt, * gpt_ptr;

struct _gpt_entry
{
  uint8_t                   type_guid[16];                ///< type GUID in mixed endian
  uint8_t                   partition_guid[16];           ///< partition GUID in mixed endian
  uint8_t                   fs_uuid[16];                  ///< file system UUID (Linux EXT2, EXT3, EXT4) in big endian (raw memory)
  uint64_t                  part_start_lba;
  uint64_t                  part_end_lba;
  uint64_t                  attributes;
  uint32_t                  fs_type;                      ///< one of the FSYS_xxx flags (see peek file system function)
  uint16_t                  part_name[38];                ///< UTF-16 Little Endian -> Windows wchar_t (36 chars, 38 here because of terminating zero, plus one spare char)
  char                      part_name_utf8_oem[128];
};

struct _gpt_header
{
  sector_ptr                sp;                           ///< sector contains GPT header

  uint32_t                  revision;
  uint32_t                  header_size;                  ///< only the standard, 0x5C supported!
  uint32_t                  header_crc32;

  uint64_t                  current_lba;
  uint64_t                  backup_lba;

  uint64_t                  first_usable_lba;
  uint64_t                  last_usable_lba;

  uint8_t                   disk_guid[16];

  uint64_t                  starting_lba_part_entries;    ///< always 2 in primary copy

  uint32_t                  number_of_part_entries;
  uint32_t                  size_of_part_entry;           ///< only 0x80 = 128 supported
  uint32_t                  part_entries_crc32;

  bool                      header_corrupt;               ///< true if CRC32 of header mismatches
  bool                      entries_corrupt;              ///< true if CRC32 of entries mismatches
};

struct _gpt
{
  gpt_header                header;

  sector_ptr                sp;                           ///< 32 sectors containing the entries
  gpt_entry                 entries[128];
};

typedef struct _disk       *disk_ptr;                     ///< forward definition

/**********************************************************************************************//**
 * @fn  mbr_part_sector_ptr partition_scan_mbr(disk_ptr dp, DISK_HANDLE h);
 *
 * @brief Scans the MBR and all extended partitions found on the disk
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param dp  the disk pointer.
 * @param h   handle to the disk with read access.
 *
 * @returns NULL (error, no MBR or corrupt); != NULL, then double linked list with MBR and all
 *          evaluated extended partitions.
 **************************************************************************************************/

mbr_part_sector_ptr partition_scan_mbr(disk_ptr dp, DISK_HANDLE h);

/**********************************************************************************************//**
 * @fn  void partition_free_mbr_part_sector_list(mbr_part_sector_ptr item);
 *
 * @brief Frees an MBR partition sector list
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param item  The pointer to the MBR partition sector list (head)
 **************************************************************************************************/

void partition_free_mbr_part_sector_list(mbr_part_sector_ptr head);

/**********************************************************************************************//**
 * @fn  gpt_ptr partition_scan_gpt(disk_ptr dp, DISK_HANDLE h, uint64_t lba);
 *
 * @brief Scans either the primary GPT or the backup GPT of a drive, respectively.
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param dp  pointer to the disk
 * @param h   disk handle
 * @param lba lba (1 for the primary GPT or <n> for the backup GPT where <n> is the highest LBA minus one)
 *
 * @returns NULL on error or the pointer to a GPT structure on success
 **************************************************************************************************/

gpt_ptr partition_scan_gpt(disk_ptr dp, DISK_HANDLE h, uint64_t lba);

/**********************************************************************************************//**
 * @fn  bool partition_compare_gpts(gpt_ptr g1, gpt_ptr g2);
 *
 * @brief Compares two GPTs (a primary and a backup one) to check if they are equal.
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param g1  The first GPT (GUID Partition Table)
 * @param g2  The second GPT
 *
 * @returns true if they are equal, false if not.
 **************************************************************************************************/

bool partition_compare_gpts(gpt_ptr g1, gpt_ptr g2);

/**********************************************************************************************//**
 * @fn  void partition_free_gpt(gpt_ptr gptp);
 *
 * @brief Frees a GPT in memory
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param gptp  pointer to GPT
 **************************************************************************************************/

void partition_free_gpt(gpt_ptr gptp);

/**********************************************************************************************//**
 * @fn  disk_map_ptr partition_create_disk_map_mbr(disk_ptr dp);
 *
 * @brief Create a double linked list (disk map) starting from a MBR-style partition table.
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param dp  pointer to disk, which contains nested pointers to MBR-style partition table
 *
 * @returns NULL on error or the head of the disk map on success
 **************************************************************************************************/

disk_map_ptr partition_create_disk_map_mbr(disk_ptr dp);

/**********************************************************************************************//**
 * @fn  disk_map_ptr partition_create_disk_map_gpt(gpt_ptr g, gpt_ptr g2);
 *
 * @brief Creates a disk map (double linked list) from two GPTs (primary and backup)
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param g   primary GPT
 * @param g2  backup GPT
 *
 * @returns NULL on error or the head of the disk map on success
 **************************************************************************************************/

disk_map_ptr partition_create_disk_map_gpt(gpt_ptr g, gpt_ptr g2);

/**********************************************************************************************//**
 * @fn  bool partition_dump_mbr(disk_ptr dp);
 *
 * @brief Dumps an MBR-style partition table to stdout.
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param dp  pointer to the disk (containing pointer to MBR-style partition table)
 *
 * @returns true on success, false on error.
 **************************************************************************************************/

bool partition_dump_mbr(disk_ptr dp);

/**********************************************************************************************//**
 * @fn  bool partition_dump_gpt(disk_ptr dp);
 *
 * @brief Dumps a GUID Partition Table to stdout.
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param dp  pointer to the disk (containing pointers the the primary/backup GPTs)
 *
 * @returns true on success, false on error.
 **************************************************************************************************/

bool partition_dump_gpt(disk_ptr dp);

/**********************************************************************************************//**
 * @fn  bool partition_peek_fs_for_gpt(disk_ptr dp, DISK_HANDLE h);
 *
 * @brief Walks down all partitions of a GPT calling partition_peek_filesystem for all supported partition types
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param dp  pointer to disk
 * @param h   an opened disk handle to the disk (for reading/peeking file systems).
 *
 * @returns true on success, false on error.
 **************************************************************************************************/

bool partition_peek_fs_for_gpt(disk_ptr dp, DISK_HANDLE h);

/**********************************************************************************************//**
 * @fn  uint32_t partition_peek_filesystem(disk_ptr dp, DISK_HANDLE h, uint64_t lba_start, uint8_t* uuid);
 *
 * @brief Peeks a filesystem (inspects the first sectors of a file system partition) to see what is
 *        'in there'. Currently, only the Windows file systems (FAT12, FAT16, FAT32, exFAT, NTFS)
 *        and Linux ext2, ext3, ext4 are supported.
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param           dp        disk pointer
 * @param           h         open DISK_HANDLE
 * @param           lba_start The start LBA of the file system partition.
 * @param [in,out]  uuid      (optional) if the file system contains a UUID, it is returned here
 *                            (128 bits or 16 bytes). Is only returned for EXT2/EXT3/EXT4 in Big
 *                            Endian, i.e. just copied as is.
 *
 * @returns one of the FSYS_xxx constants (see above)
 **************************************************************************************************/

uint32_t partition_peek_filesystem(disk_ptr dp, DISK_HANDLE h, uint64_t lba_start, uint8_t* uuid);

/**********************************************************************************************//**
 * @fn  void setGPTPartitionName(uint16_t* p, char *p2, const char* name);
 *
 * @brief Copies up to 38 Little Endian UTF-16 letters to the GPT entry (name of partition)
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param [in,out]  p     pointer to buffer with 36 uint16_t's.
 * @param [in,out]  p2    target pointer receiving the ASCII string.
 * @param           name  pointer to the zero-terminated name; up to 35 characters are copied. THIS
 *                        MUST BE ZERO-TERMINATED according to UEFI spec. so only 35 characters plus
 *                        zero are allowed.
 **************************************************************************************************/

void setGPTPartitionName(uint16_t* p, char *p2, const char* name);

/**********************************************************************************************//**
 * @fn  bool gpt_get_guid_for_mbr_type(uint8_t part_type, uint8_t* guid, uint64_t* attributes);
 *
 * @brief Retrieves the GPT partition type GUID based on the MBR partition type plus GPT attributes
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param           part_type   MBR partition type byte
 * @param [in,out]  guid        receives the GUID on success
 * @param [in,out]  attributes  receives the GPT partition attributes on success
 *
 * @returns true on success, false otherwise
 **************************************************************************************************/

bool gpt_get_guid_for_mbr_type(uint8_t part_type, uint8_t* guid, uint64_t* attributes);

/**********************************************************************************************//**
 * @fn  bool partition_dump_temporary_gpt(gpt_ptr g);
 *
 * @brief Dumps a temporary (in-memory) GPT to stdout (for verbosity option)
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param g pointer to GPT
 *
 * @returns true on success, false on error
 **************************************************************************************************/

bool partition_dump_temporary_gpt(gpt_ptr g);

/**********************************************************************************************//**
 * @fn  void create_protective_mbr(uint64_t device_sectors, uint8_t* target);
 *
 * @brief Creates a protective MBR containing just one partition (type 0xEE) protecting the GPT that follows the MBR.
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param           device_sectors  number of 512-byte sectors on the disk.
 * @param [in,out]  target          pointer to a 512-byte area receiving the protective MBR
 **************************************************************************************************/

void create_protective_mbr(uint64_t device_sectors, uint8_t* target);

/**********************************************************************************************//**
 * @fn  void gpt_create_table(uint8_t* sector, gpt_ptr g, bool is_primary);
 *
 * @brief Creates a GPT consisting of 33 (1 for header, 32 for the entries) 512-byte sectors.
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param [in,out]  sector      pointer to the target area (at least 33 * 512 bytes space)
 * @param           g           pointer to GPT that will be stored in the sector area
 * @param           is_primary  true if it is the primary GPT, false if it is the backup GPT
 **************************************************************************************************/

void gpt_create_table(uint8_t* sector, gpt_ptr g, bool is_primary);

/**********************************************************************************************//**
 * @fn  uint64_t gpt_repair_table(uint8_t* sector, gpt_ptr g, bool is_primary);
 *
 * @brief Creates a GPT consisting of 33 (1 for header, 32 for the entries) 512-byte sectors using
 *        the other GPT (backup for primary or primary for backup, respectively).
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param [in,out]  sector      pointer to the target area (at least 33 * 512 bytes space)
 * @param           g           pointer to GPT that will be used as the template (repair source)
 * @param           is_primary  true if GPT is the primary GPT, false if it is the backup GPT.
 *
 * @returns the 64bit file pointer offset where the 33 512 byte sectors have to be written to the
 *          disk.
 **************************************************************************************************/

uint64_t gpt_repair_table(uint8_t* sector, gpt_ptr g, bool is_primary);

#ifdef __cplusplus
}
#endif

#endif // _INC_PARTITION_H_
