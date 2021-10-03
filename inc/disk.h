/**
 * @file   disk.h
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @brief  declaration of various structures and functions that manage
 *         harddisk/SSD drives on MS Windows or Linux, respectively.
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

#ifndef _INC_DISK_H_
#define _INC_DISK_H_

#include <part-y.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SECTOR_MEM_ALIGN                4096

#define SECTOR_SIZE                     512                         ///< one logical sector is always 512 for us
#define SECTOR_SIZE_MASK                (SECTOR_SIZE-1)
#define SECTOR_SHIFT                    9                           ///< 2^9 = 512
#define SECTOR_SHIFT_MEGABYTE           11                          ///< one megabyte is 2^20 so (20-9) = 11

#define DISK_FLAG_READ_ACCESS_ERROR     0x00000001                  ///< there was a (read) access error, so do not use this drive at all.
#define DISK_FLAG_WRITE_ACCESS_ERROR    0x00000002                  ///< there was a (write) access error, so do not use this drive for write ops.
#define DISK_FLAG_NOT_DEVICE_BUT_FILE   0x00000004                  ///< if this is an image file and not a physical disk device

#define DISK_FLAG_HAS_MBR               0x00000008                  ///< MBR partition table could be successfully read and parsed
#define DISK_FLAG_MBR_IS_PROTECTIVE     0x00000010                  ///< only set if both MBR and GPT present AND MBR only contains one 0xEE entry
#define DISK_FLAG_HAS_GPT               0x00000020                  ///< GPT partition table could be successfully read and parsed

typedef struct _mbr_part_sector        *mbr_part_sector_ptr;        ///< forward definition

typedef struct _disk                    disk, * disk_ptr;
typedef struct _sector                  sector, * sector_ptr;
typedef struct _disk_map                disk_map, * disk_map_ptr;
typedef struct _gpt                    *gpt_ptr;                    ///< forward

struct _disk
{
  disk_ptr                              next;                       ///< next disk in list (NULL if this is tail)
  disk_ptr                              prev;                       ///< previous disk in list (NULL if this is head)

  char                                  device_file[256];           ///< on Windows: e.g. "\\.\PhysicalDrive0"; on Linux, e.g. "/dev/sda"
#ifdef _WINDOWS
  char                                  vendor[128];                ///< on Windows retrieved from physical device string (registry)
  char                                  product[128];               ///< on Windows retrieved from physical device string (registry)
  uint32_t                              device_no;                  ///< on Windows: 0, 1, 2, ...; UNUSED on Linux; does not apply to image files on Windows, too.
#endif // _WINDOWS

  uint64_t                              device_size;                ///< device size measured in BYTES
  uint64_t                              device_sectors;             ///< device size divided by 512 (BECAUSE THIS TOOL ONLY WORKS WITH 
                                                                    ///< DEVICES HAVING THE LOGICAL SECTOR SIZE OF 512 ALTHOUGH PHYSICAL 
                                                                    ///< SECTOR SIZE MAY BE 4096 OR WHATSOEVER)
  // thus, the 64bit LBA range of this disk is 0..(device_sectors-1)
  
  uint32_t                              flags;                      ///< some disk flags, see above
  uint32_t                              logical_sector_size;        ///< if this is != 512 = SECTOR_SIZE, then this tool refuses to use the disk
  uint32_t                              physical_sector_size;       ///< usually 512, can be 4096 (4K drive with 512 emulation aka '512e', though.

  mbr_part_sector_ptr                   mbr;                        ///< if != NULL, then MBR (plus optional extended partition tables) successfully scanned
  disk_map_ptr                          mbr_dmp;                    ///< the disk map according to MBR plus any extended partitions (logical drives)

#ifdef _WINDOWS
  PDRIVE_LAYOUT_INFORMATION_EX          win_drive_layout;           ///< pointer to windows drive layout information returned by DeviceIoControl
  DWORD                                 mbr_part_info_size;         ///< size in bytes of the following data
  uint8_t                              *mbr_partition_info;         ///< MBR partition information as returned by MS Windows
#endif
  gpt_ptr                               gpt1;                       ///< primary GPT (if any)
  gpt_ptr                               gpt2;                       ///< backup GPT (if any)
  bool                                  primary_gpt_exists;         ///< true if primary GPT exists
  bool                                  primary_gpt_corrupt;        ///< true if primary GPT could be successfully parsed
  bool                                  backup_gpt_exists;          ///< true if secondary=backup GPT exists
  bool                                  backup_gpt_corrupt;         ///< true if secondary=backup GPT could be successfully parsed
  disk_map_ptr                          gpt_dmp;                    ///< the disk map according to GPT
  bool                                  gpts_mismatch;              ///< true if both GPTs mismatch (which is bad -> corrupt GPT(s))
};

struct _sector
{
  disk_ptr                              dp;                         ///< back pointer to disk these sectors belong to

  sector_ptr                            next;                       ///< next list item (NULL if this is tail)
  sector_ptr                            prev;                       ///< prev list item (NULL if this is head)

  uint8_t                              *data;                       ///< pointer to sector data (aligned to physical sector size of device)
  void                                 *data_malloc_ptr;            ///< this is the original pointer returned by malloc()

  uint64_t                              lba;                        ///< start LBA (zero-based)
  uint32_t                              num_sectors;                ///< number of LBAs, i.e. data size is 512 * num_lbas
};

struct _disk_map
{
  uint8_t                       guid[16];         ///< only filled for GPTs (not MBRs)

  disk_map_ptr                  prev;
  disk_map_ptr                  next;

  uint64_t                      start_lba;
  uint64_t                      end_lba;

  char                          description[64];

  bool                          is_free;
};

/**********************************************************************************************//**
 * @fn  uint32_t disk_explore_all(disk_ptr* head, disk_ptr* tail);
 *
 * @brief Explore all disks in the system (Windows or Linux)
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param [in,out]  head  receives the head of the double linked list.
 * @param [in,out]  tail  receives the tail of the double linked list.
 *
 * @returns number of explored physical disk drives (0 on error, e.g. head, tail or BOTH were NULL).
 **************************************************************************************************/

uint32_t disk_explore_all(disk_ptr* head, disk_ptr* tail);

/**********************************************************************************************//**
 * @fn  void disk_free_list(disk_ptr head);
 *
 * @brief Frees an allocated double linked list with disks (from disk_explore_all or manually added)
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param head  the head of the double linked list.
 **************************************************************************************************/

void disk_free_list(disk_ptr head);

/**********************************************************************************************//**
 * @fn  DISK_HANDLE disk_open_device(const char* device_file, bool writeAccess);
 *
 * @brief opens a device file in read-only or read-write mode
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param device_file fully-qualified device file name
 * @param writeAccess false if read-only access desired, true for write-write
 *
 * @returns INVALID_DISK_HANDLE on error or a usable DISK_HANDLE (success)
 **************************************************************************************************/

DISK_HANDLE disk_open_device(const char* device_file, bool writeAccess);

/**********************************************************************************************//**
 * @fn  void disk_close_device(DISK_HANDLE h);
 *
 * @brief closes a device file
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param h DISK_HANDLE to be closed; if it is INVALID_DISK_HANDLE, then this is a no-op.
 **************************************************************************************************/

void disk_close_device(DISK_HANDLE h);

/**********************************************************************************************//**
 * @fn  uint64_t disk_get_size(const char* device_file, DISK_HANDLE h, uint32_t* logical_sector_size, uint32_t* physical_sector_size);
 *
 * @brief retrieve the size of a device file in sectors (512 byte units). Optionally retrieve
 *        the logical and physical sector sizes (for an image file, this is always 512,512).
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param           device_file           The device file.
 * @param           h                     handle to the already opened device file.
 * @param [in,out]  logical_sector_size   If non-null, size of the logical sector is returned here.
 * @param [in,out]  physical_sector_size  If non-null, size of the physical sector is returned here.
 *
 * @returns size of the device file in sectors (512 byte units).
 **************************************************************************************************/

uint64_t disk_get_size(const char* device_file, DISK_HANDLE h, uint32_t* logical_sector_size, uint32_t* physical_sector_size);

/**********************************************************************************************//**
 * @fn  bool disk_read(disk_ptr dp, DISK_HANDLE h, uint64_t fp, uint8_t* buffer, uint32_t size);
 *
 * @brief Reads from disk
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param           dp      pointer to disk structure (for flag updates)
 * @param           h       disk handle
 * @param           fp      file pointer (zero-based, must be divisible by 512)
 * @param [in,out]  buffer  pointer to buffer
 * @param           size    size to be read, must be divisible by 512
 *
 * @returns True if it succeeds, false if it fails.
 **************************************************************************************************/

bool disk_read(disk_ptr dp, DISK_HANDLE h, uint64_t fp, uint8_t* buffer, uint32_t size);

/**********************************************************************************************//**
 * @fn  bool disk_write(disk_ptr dp, DISK_HANDLE h, uint64_t fp, const uint8_t const* buffer, uint32_t size);
 *
 * @brief Disk write
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param dp      pointer to disk structure (for flag updates)
 * @param h       disk handle
 * @param fp      file pointer (zero-based, must be divisible by 512)
 * @param buffer  buffer containing the data to be written
 * @param size    size of the data to be written, must be divisible by 512
 *
 * @returns True if it succeeds, false if it fails.
 **************************************************************************************************/

bool disk_write(disk_ptr dp, DISK_HANDLE h, uint64_t fp, const uint8_t * buffer, uint32_t size);

/**********************************************************************************************//**
 * @fn  uint64_t disk_getFileSize(DISK_HANDLE h);
 *
 * @brief Retrieves the size of a file
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param h the already open file (handle)
 *
 * @returns the file size
 **************************************************************************************************/

uint64_t disk_getFileSize(DISK_HANDLE h);

/**********************************************************************************************//**
 * @fn  sector_ptr disk_read_sectors(disk_ptr dp, DISK_HANDLE h, sector_ptr* head, sector_ptr* tail, uint64_t lba, uint32_t num_sectors);
 *
 * @brief Reads one or more sectors, allocates a new sector list item, stores the data in it. Adds
 *        the sector list item (sorted) in the list (head, tail).
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param           dp          pointer to disk structure.
 * @param           h           handle to disk
 * @param [in,out]  head        pointer to head of sector list.
 * @param [in,out]  tail        pointer to tail of sector list.
 * @param           lba         start LBA (where to begin reading)
 * @param           num_sectors number of sectors to be read.
 *
 * @returns NULL on error or the pointer to the sector list item containing the requested
 *          sector(s). Also, the list (head,tail) will be updated with this sector structure item
 *          (list will be kept sorted).
 **************************************************************************************************/

sector_ptr disk_read_sectors(disk_ptr dp, DISK_HANDLE h, sector_ptr* head, sector_ptr* tail, uint64_t lba, uint32_t num_sectors);

/**********************************************************************************************//**
 * @fn  void disk_free_sector(sector_ptr sp);
 *
 * @brief Frees one disk sector structure
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param sp  pointer to the disk sector C structure to be freed.
 **************************************************************************************************/

void disk_free_sector(sector_ptr sp);

/**********************************************************************************************//**
 * @fn  void disk_free_sector_list(sector_ptr sp);
 *
 * @brief Frees this disk sector structure and walks down the double linked list freeding all
 *        subsequent sector structures
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param sp  pointer to the head of disk sector C structure(s) - double linked list.
 **************************************************************************************************/

void disk_free_sector_list(sector_ptr sp);

/**********************************************************************************************//**
 * @fn  void disk_scan_partitions(disk_ptr dp);
 *
 * @brief Performs a full partition scan (MBR and GPT) and collects all 512-byte sectors, which are
 *        part of either one of the partition table types (or both).
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param dp  pointer to the disk to be scanned.
 **************************************************************************************************/

bool disk_scan_partitions(disk_ptr dp);

/**********************************************************************************************//**
 * @fn  void disk_dump_info(disk_ptr dp);
 *
 * @brief Dumps disk information to stdout
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param dp  pointer to the disk C structure containing information about a HDD or SSD,
 *            respectively.
 **************************************************************************************************/

void disk_dump_info(disk_ptr dp);

/**********************************************************************************************//**
 * @fn  void _diskpart_volume::disk_dump_map(disk_map_ptr dmp);
 *
 * @brief Dumps a disk map to stdout. A disk map represents the full partition layout of a disk
 *        including any unused (free, unallocated) spaces between the partitions (if any).
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param dmp pointer to the disk map
 **************************************************************************************************/

void disk_dump_map(disk_map_ptr dmp);

typedef struct _cmdline_args *cmdline_args_ptr;

/**********************************************************************************************//**
 * @fn  disk_ptr disk_setup_device(cmdline_args_ptr cap, const char* device_file);
 *
 * @brief Performs a disk setup using a device name. A device file is just 0, 1, 2, ... on Windows 
 *        (which becomes \\.\PhysicalDriveX) or a block device, e.g. /dev/sda, /dev/nvmne0n1, ...
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param cap         pointer to cmdline args structure (which must contain the already scanned
 *                    device_file)
 * @param device_file zero-terminated device file name
 *
 * @returns the disk structure representing the device (either newly allocated or taken from
 *          the cmdline args structure - pre-scanned device)
 **************************************************************************************************/

disk_ptr disk_setup_device(cmdline_args_ptr cap, const char* device_file);

/**********************************************************************************************//**
 * @fn  disk_ptr disk_create_new(cmdline_args_ptr cap, const char* device_file, bool is_image_file);
 *
 * @brief Creates a new disk C structure for a device file
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param cap           cmdline arguments structure
 * @param device_file   pointer to the zero-terminated device name (see disk_setup_device above)
 * @param is_image_file true if the file is an image file, false if it is a real block device
 *
 * @returns NULL (error) or the newly allocated disk C structure describing the disk
 **************************************************************************************************/

disk_ptr disk_create_new(cmdline_args_ptr cap, const char* device_file, bool is_image_file);

#ifdef _WINDOWS
int truncate(const char* file_name, uint64_t filesize);

typedef struct _win_volume                win_volume, * win_volume_ptr;
typedef struct _diskpart_volume           diskpart_volume, * diskpart_volume_ptr;

struct _win_volume
{
  win_volume_ptr                          next;
  win_volume_ptr                          prev;
  char                                    volume_name[256];     ///< full volume name
  char                                    device_name[128];     ///< "\Device\HarddiskVolumeX"
  char                                    volume_guid[48];      ///< extracted {00000000-0000-0000-0000-000000000000} from "\\?\Volume{95bc2098-0000-0000-0000-902b5a000000}\"
  uint64_t                                start_lba;            ///< start LBA of partition; 0 if no drive letter available (only if drive_letter != 0x00)
  uint64_t                                num_lbas;             ///< number of LBAs in this volume = partition (because we only support single extent volumes)
  uint32_t                                disk_number;          ///< number of the \\.\PhysicalDriveX
  uint32_t                                volume_no;            ///< number "X" extracted from device_name (volume name)
  uint32_t                                num_extents;          ///< caution: if this is != 1, we DO NOT SUPPORT it (for the conversion MBR -> GPT)
  char                                    drive_letter;         ///< 0x00 if no drive letter or character C, D, E, ... for C:\, D:\, E:\ ...
};

struct _diskpart_volume
{
  diskpart_volume_ptr                     next;
  diskpart_volume_ptr                     prev;

  uint64_t                                start_lba;            ///< start sector (LBA) from Windows volume
  uint32_t                                volume_number;        ///< zero-based
  char                                    drive_letter;         ///< 0x00 if no drive letter
  uint32_t                                fs_type;              ///< FSYS_xxx constants
};

/**********************************************************************************************//**
 * @fn  win_volume_ptr disk_enumerate_windows_volumes(void);
 *
 * @brief (Windows only) Enumerates all Windows volumes
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @returns NULL (error) or the pointer to the double linked list containing the Windows volumes.
 **************************************************************************************************/

win_volume_ptr disk_enumerate_windows_volumes(void);

/**********************************************************************************************//**
 * @fn  void disk_free_windows_volume_list(win_volume_ptr item);
 *
 * @brief (Windows only) Frees the double linked list of Windows volumes
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param item  The item.
 **************************************************************************************************/

void disk_free_windows_volume_list(win_volume_ptr item);

/**********************************************************************************************//**
 * @fn  diskpart_volume_ptr disk_enumerate_diskpart_volumes(cmdline_args_ptr cap);
 *
 * @brief (Windows only) Execute the external diskpart.exe Windows tool to parse the delivered
 *        console output for diskpart volumes (which differ from Windows volumes).
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param cap command line arguments
 *
 * @returns NULL (error) or the double linked list containing all diskpart volumes
 **************************************************************************************************/

diskpart_volume_ptr disk_enumerate_diskpart_volumes(cmdline_args_ptr cap);

/**********************************************************************************************//**
 * @fn  void disk_free_diskpart_volume_list(diskpart_volume_ptr item);
 *
 * @brief (Windows only) Frees the diskpart volume list
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param item  the head of the list
 **************************************************************************************************/

void disk_free_diskpart_volume_list(diskpart_volume_ptr item);

/**********************************************************************************************//**
 * @fn  win_volume_ptr findWindowsVolumeByPartitionStartLBA(win_volume_ptr head, uint32_t disk_number, uint64_t start_lba);
 *
 * @brief (Windows only) Scans the list of Windows volumes and tries to locate a Windows volume
 *        from the disk number and the starting LBA
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param head        head of volume list.
 * @param disk_number zero-based disk number (e.g. 0 represents \\.\PhysicalDrive0)
 * @param start_lba   starting sector number of the volume partition (Linear Block Addressing);
 *                    volumes spanning over several partitions are not supported here.
 *
 * @returns The found windows volume or NULL on error (not found).
 **************************************************************************************************/

win_volume_ptr findWindowsVolumeByPartitionStartLBA(win_volume_ptr head, uint32_t disk_number, uint64_t start_lba);

/**********************************************************************************************//**
 * @fn  disk_map_ptr sort_and_complete_disk_map(disk_map_ptr dmp, uint64_t deviceSectorSize);
 *
 * @brief (Windows only) Sorts a disk map and fill partition gaps with free (unallocated) entries
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param dmp               The dmp.
 * @param deviceSectorSize  Size of the device sector.
 *
 * @returns The sorted and complete disk map.
 **************************************************************************************************/

disk_map_ptr sort_and_complete_disk_map(disk_map_ptr dmp, uint64_t deviceSectorSize);

#ifdef _WINDOWS

/**********************************************************************************************//**
 * @fn  bool disk_mbr_get_partition_guid(disk_ptr dp, uint64_t start_lba, uint64_t num_lbas, GUID* guid);
 *
 * @brief Retrieves the partition GUID of an MBR partition, WHICH MAY DIFFER from the GUID of the
 *        Windows volume. The Boot Configuration Data uses this partition GUID to identify a
 *        partition (together with the GUID of the disk, which is the MBR disk signature 32bit
 *        value at offset 0x1B8 with 12 zeros).
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 *
 * @param           dp        pointer to the disk structure.
 * @param           start_lba the start sector of the partition (a sector is always 512 bytes here,
 *                            4096 is not supported)
 * @param           num_lbas  number of sectors (512-byte sectors), size of the partition.
 * @param [in,out]  guid      receives the GUID if the partition was found on exit.
 *
 * @returns true on success, false otherwise.
 **************************************************************************************************/

bool disk_mbr_get_partition_guid(disk_ptr dp, uint64_t start_lba, uint64_t num_lbas, GUID* guid);

#endif

#endif // of Windows

#ifdef __cplusplus
}
#endif

#endif // _INC_DISK_H_
