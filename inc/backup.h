/**
 * @file   backup.h
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @brief  declaration of various structures and functions that manage
 *         backups (partition tables and disk areas).
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

#ifndef _INC_BACKUP_H_
#define _INC_BACKUP_H_

#include <part-y.h>

#ifdef __cplusplus
extern "C" {
#endif

// all values in backups are stored as Big Endian (network order) values

typedef struct _backup_header               backup_header, * backup_header_ptr;
typedef struct _backup_record               backup_record, * backup_record_ptr;

#define BACKUP_VERSION      0x00010000            ///< version 1.0

struct _backup_header
{
  uint8_t                   signature[16];        ///< contains "PART-Y-BACK-FILE"
  uint32_t                  version;              ///< hi 16bits = major, lo 16bits = minor
  uint32_t                  first_record_ofs;     ///< offset in file of first record (always 512 or 0x200)
  uint64_t                  device_sectors;       ///< number of 512-byte-sectors of the device this backup was made of
  uint64_t                  num_records;          ///< number of backup records

  backup_record_ptr         head;                 ///< ONLY IN-MEMORY: head of all records
  backup_record_ptr         tail;                 ///< ONLY IN-MEMORY: tail of all records
};

struct _backup_record
{
  uint64_t                  start_lba;            ///< start LBA (start sector)
  uint64_t                  num_lbas;             ///< number of LBAs (number of sectors)

  backup_record_ptr         prev;                 ///< ONLY IN-MEMORY: previous backup record or NULL (head)
  backup_record_ptr         next;                 ///< ONLY IN-MEMORY: next backup record or NULL (tail)
};

/**********************************************************************************************//**
 * @fn  backup_header_ptr bootstrap_backup(uint64_t device_sectors);
 *
 * @brief Creates a new backup header and thus bootstraps a new backup
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param device_sectors  number of sectors (entire device)
 *
 * @returns NULL on error or a newly allocated backup header structure (on success).
 **************************************************************************************************/

backup_header_ptr bootstrap_backup(uint64_t device_sectors);

/**********************************************************************************************//**
 * @fn  bool add_backup_record(backup_header_ptr bhp, uint64_t start_lba, uint64_t num_sectors);
 *
 * @brief Adds a backup record
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param bhp         pointer to the backup header
 * @param start_lba   start sector (LBA) of the next backup record (chunk)
 * @param num_sectors number of 512-byte-sectors in this record
 *
 * @returns true on success, false otherwise.
 **************************************************************************************************/

bool add_backup_record(backup_header_ptr bhp, uint64_t start_lba, uint64_t num_sectors);

/**********************************************************************************************//**
 * @fn  void free_backup_structure(backup_header_ptr bhp);
 *
 * @brief Free a backup structure in memory
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param bhp pointer to the backup header; the header and all of its linked records are freed.
 **************************************************************************************************/

void free_backup_structure(backup_header_ptr bhp);

/**********************************************************************************************//**
 * @fn  bool create_backup_file(disk_ptr dp, backup_header_ptr bhp, DISK_HANDLE h, const char* backup_file, const char *message);
 *
 * @brief Creates a backup file
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param dp          pointer to the disk.
 * @param bhp         pointer to the backup header.
 * @param h           disk handle opened for at least 'reading'.
 * @param backup_file pointer to the fully-qualified, zero-terminated backup file name.
 * @param message     NULL or a message string (progress is shown)
 *
 * @returns true if the backup could be created, false otherwise (error).
 **************************************************************************************************/

bool create_backup_file(disk_ptr dp, backup_header_ptr bhp, DISK_HANDLE h, const char* backup_file, const char *message);

/**********************************************************************************************//**
 * @fn  bool check_backup_file(disk_ptr dp, DISK_HANDLE h, const char* backup_file, const char* message);
 *
 * @brief Performs a read-back of a backup file and checks if it matches the disk (handle h).
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param dp          pointer to the disk.
 * @param h           disk handle opened for a least 'reading' or INVALID_DISK_HANDLE if the caller
 *                    just wants to check the SHA3_512 embedded in the backup file.
 * @param backup_file pointer to the fully-qualified, zero-terminated backup file name.
 * @param message     NULL or a message string (progress is shown)
 *
 * @returns true if the backup file matches the included LBAs of the physical disk, false on error.
 **************************************************************************************************/

bool check_backup_file(disk_ptr dp, DISK_HANDLE h, const char* backup_file, const char* message);

/**********************************************************************************************//**
 * @fn  bool restore_backup_file(disk_ptr dp, DISK_HANDLE h, const char* backup_file);
 *
 * @brief Restores a previously created backup file to the identical device (number of device sectors MUST match)
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param dp          pointer to the disk
 * @param h           disk handle opened for 'writing'
 * @param backup_file pointer to the fully-qualified, zero-terminated backup file name
 * @param message     NULL or a message string (progress is shown)
 *
 * @returns true if the backup was successfully restored to the disk, false on error.
 **************************************************************************************************/

bool restore_backup_file(disk_ptr dp, DISK_HANDLE h, const char* backup_file, const char *message);

#ifdef __cplusplus
}
#endif

#endif // _INC_BACKUP_H_
