/**
 * @file   part-y.h
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @brief  declaration of various structures and functions that represent
 *         the part-y (say 'part-why' or just 'party') tool, which runs on
 *         MS Windows or Linux, respectively.
 *         This is yet-another partitioning / formatting tool, which supports
 *         MBRs and GPTs and implements the conversion of Windows 10 systems
 *         from an MBR-style partition table to GPTs including all the necessary
 *         BCD (Boot Configuration Data) and EFI-NVRAM variable stuff.
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

#ifndef _INC_PART_Y_H_
#define _INC_PART_Y_H_

#ifdef __cplusplus
extern "C" {
#endif

#define PROGRAM_VERSION               "0.4-alpha"
#define PROGRAM_INFO                  "party v" PROGRAM_VERSION ", build date " __DATE__
#define PROGRAM_AUTHOR                "Ingo A. Kubbilun (www.devcorn.de)"

#ifdef _LINUX
#define _GNU_SOURCE
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#ifdef _WINDOWS
#pragma warning (disable : 4996)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <winioctl.h>
#include <io.h>
#include <fcntl.h>
#include <winreg.h>
#include <objbase.h>
#define _WIN32_DCOM
#include <wbemidl.h>
#pragma comment(lib, "wbemuuid.lib")

#ifdef _DEBUG
#include <crtdbg.h>
#define CHECK_MEM() \
	do \
	{ \
		if ( !_CrtCheckMemory( ) ) \
		{ \
			_CrtDbgBreak( ); \
		} \
	} while( 0 )
#endif

#define FMT64 "I64"
#define likely(expr)    (expr)
#define unlikely(expr)  (expr)
#else
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <fstab.h>
#include <sys/stat.h>
#include <ctype.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/mount.h>
#define stricmp strcasecmp
#define FMT64 "l"
#define likely(expr)    (__builtin_expect(!!(expr), 1))
#define unlikely(expr)  (__builtin_expect(!!(expr), 0))

#endif

#ifdef _WINDOWS
  typedef HANDLE                          DISK_HANDLE;
#define INVALID_DISK_HANDLE             INVALID_HANDLE_VALUE
#else
  typedef int                             DISK_HANDLE;
#define INVALID_DISK_HANDLE             -1
#endif

#include <file.h>
#include <disk.h>
#include <partition.h>
#include <backup.h>
#include <sha3.h>
#include <bcd.h>

#define WINDOWS_BOOT_EFI_DIR    "\\Windows\\Boot\\EFI"

typedef struct _part_def        part_def, *part_def_ptr;
typedef struct _cmdline_args    cmdline_args, *cmdline_args_ptr;

struct _part_def
{
  uint64_t                      flags;      ///< GPT_ATTR_xxx constants (for MBR, only GPT_ATTR_LEGACY_BIOS_BOOT possible!)
  uint64_t                      size;
  uint32_t                      type;
  char                          label[40];  ///< limited to 36 characters (converted to UTF-16 LE), GPT-only!
};

#define CTRL_RESET              "\033[0;0;0m"
#define CTRL_RED                "\033[1;31m"
#define CTRL_GREEN              "\033[1;32m"
#define CTRL_YELLOW             "\033[1;33m"
#define CTRL_BLUE               "\033[1;34m"
#define CTRL_MAGENTA            "\033[1;35m"
#define CTRL_CYAN               "\033[1;36m"

#define READ_LITTLE_ENDIAN32(_buf,_ofs) ((((uint32_t)(_buf)[(_ofs)])<<0)|(((uint32_t)(_buf)[(_ofs)+1])<<8)|(((uint32_t)(_buf)[(_ofs)+2])<<16)|(((uint32_t)(_buf)[(_ofs)+3])<<24))
#define READ_BIG_ENDIAN32(_buf,_ofs)    ((((uint32_t)(_buf)[(_ofs)])<<24)|(((uint32_t)(_buf)[(_ofs)+1])<<16)|(((uint32_t)(_buf)[(_ofs)+2])<<8)|(((uint32_t)(_buf)[(_ofs)+3])<<0))
#define READ_LITTLE_ENDIAN64(_buf,_ofs) ((((uint64_t)(_buf)[(_ofs)])<<0)|(((uint64_t)(_buf)[(_ofs)+1])<<8)|(((uint64_t)(_buf)[(_ofs)+2])<<16)|(((uint64_t)(_buf)[(_ofs)+3])<<24)|(((uint64_t)(_buf)[(_ofs) + 4]) << 32) | (((uint64_t)(_buf)[(_ofs) + 5]) << 40) | (((uint64_t)(_buf)[(_ofs) + 6]) << 48) | (((uint64_t)(_buf)[(_ofs) + 7]) << 56) )
#define READ_BIG_ENDIAN64(_buf,_ofs)    ((((uint64_t)(_buf)[(_ofs)])<<56)|(((uint64_t)(_buf)[(_ofs)+1])<<48)|(((uint64_t)(_buf)[(_ofs)+2])<<40)|(((uint64_t)(_buf)[(_ofs)+3])<<32)|(((uint64_t)(_buf)[(_ofs) + 4]) << 24) | (((uint64_t)(_buf)[(_ofs) + 5]) << 16) | (((uint64_t)(_buf)[(_ofs) + 6]) << 8) | (((uint64_t)(_buf)[(_ofs) + 7]) << 0) )

#define WRITE_BIG_ENDIAN32(_buf,_ofs,_val)  do { _buf[(_ofs)+0] = (uint8_t)((_val) >> 24); _buf[(_ofs)+1] = (uint8_t)((_val) >> 16); _buf[(_ofs)+2] = (uint8_t)((_val) >> 8); _buf[(_ofs)+3] = (uint8_t)(_val); } while (0)
#define WRITE_BIG_ENDIAN64(_buf,_ofs,_val)  do { _buf[(_ofs)+0] = (uint8_t)((_val) >> 56); _buf[(_ofs)+1] = (uint8_t)((_val) >> 48); _buf[(_ofs)+2] = (uint8_t)((_val) >> 40); _buf[(_ofs)+3] = (uint8_t)((_val) >> 32); _buf[(_ofs)+4] = (uint8_t)((_val) >> 24);  _buf[(_ofs)+5] = (uint8_t)((_val) >> 16);  _buf[(_ofs)+6] = (uint8_t)((_val) >> 8);  _buf[(_ofs)+7] = (uint8_t)(_val); } while (0)

#define WRITE_LITTLE_ENDIAN32(_buf,_ofs,_val)  do { _buf[(_ofs)+3] = (uint8_t)((_val) >> 24); _buf[(_ofs)+2] = (uint8_t)((_val) >> 16); _buf[(_ofs)+1] = (uint8_t)((_val) >> 8); _buf[(_ofs)+0] = (uint8_t)(_val); } while (0)
#define WRITE_LITTLE_ENDIAN64(_buf,_ofs,_val)  do { _buf[(_ofs)+7] = (uint8_t)((_val) >> 56); _buf[(_ofs)+6] = (uint8_t)((_val) >> 48); _buf[(_ofs)+5] = (uint8_t)((_val) >> 40); _buf[(_ofs)+4] = (uint8_t)((_val) >> 32); _buf[(_ofs)+3] = (uint8_t)((_val) >> 24);  _buf[(_ofs)+2] = (uint8_t)((_val) >> 16);  _buf[(_ofs)+1] = (uint8_t)((_val) >> 8);  _buf[(_ofs)+0] = (uint8_t)(_val); } while (0)

#define COMMAND_VERSION         0x00000001
#define COMMAND_HELP            0x00000002
#define COMMAND_INFO            0x00000003
#define COMMAND_BACKUP          0x00000004
#define COMMAND_RESTORE         0x00000005
#define COMMAND_CREATE          0x00000006
#define COMMAND_CONVERT         0x00000007
#define COMMAND_PREPAREWIN10    0x00000008
#define COMMAND_CONVERTWIN10    0x00000009
#define COMMAND_WRITEPMBR       0x0000000A
#define COMMAND_REPAIRGPT       0x0000000B
#define COMMAND_FILL            0x0000000C
#define COMMAND_HEXDUMP         0x0000000D
#define COMMAND_ENUMDISKS       0x0000000E

#define PARTITION_TYPE_FAT12    0x00000001
#define PARTITION_TYPE_FAT16    0x00000002
#define PARTITION_TYPE_FAT32    0x00000003
#define PARTITION_TYPE_EXFAT    0x00000004
#define PARTITION_TYPE_NTFS     0x00000005
#define PARTITION_TYPE_WINRE    0x00000006
#define PARTITION_TYPE_MSR      0x00000007
#define PARTITION_TYPE_EXT2     0x00000008
#define PARTITION_TYPE_EXT3     0x00000009
#define PARTITION_TYPE_EXT4     0x0000000A
#define PARTITION_TYPE_SWAP     0x0000000B
#define PARTITION_TYPE_EFI      0x0000000C

struct _cmdline_args
{
  uint32_t                      command;

  char                          device_name[256];               ///< this is /dev/sda or /dev/nvme0n1 or an image file or ...
  char                          backup_file[256];

  char                          locale[64];                     ///< locale in Boot Configuration Data; defaults to 'en-US', can also be e.g. 'de-DE', ...

#ifdef _WINDOWS
  uint32_t                      win_device_no;
#endif

  uint64_t                      lba_range_start;
  uint64_t                      lba_range_end;

  uint64_t                      file_size;

  uint32_t                      num_part_defs;
  part_def                      part_defs[128];                 ///< GPTs can have up to 128 partitions

  char                          win_sys_drive;
  char                          linux_stick_drive;

  bool                          device_is_real_device;

  bool                          dryrun;
  bool                          yes_do_it;
  bool                          verbose;
  bool                          no_format;
  bool                          part_type_mbr;

  uint32_t                      num_physical_disks;             ///< number of enumerated physical disks
  disk_ptr                      pd_head;                        ///< head of physical disk list
  disk_ptr                      pd_tail;                        ///< tail of physical disk list

  disk_ptr                      work_disk;                      ///< selected disk, either physical or image file

#ifdef _WINDOWS
  win_volume_ptr                wvp;                            ///< all Windows volumes (with drive letter where applicable)
  diskpart_volume_ptr           dvp;                            ///< all volumes as enumerated by the external diskpart.exe tool
#endif
};

/**********************************************************************************************//**
 * @fn  bool readDevice(const char* device_name, uint64_t fp, uint8_t* buffer, uint32_t size);
 *
 * @brief Reads data from a device in 512 bytes units
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param           device_name name of the device / file (if image file)
 * @param           fp          file pointer in the device / file (must be divisible by 512)
 * @param [in,out]  buffer      buffer
 * @param           size        size of the data to be read (must be divisible by 512)
 *
 * @returns true (success), false on error.
 **************************************************************************************************/

bool readDevice(const char* device_name, uint64_t fp, uint8_t* buffer, uint32_t size);

/**********************************************************************************************//**
 * @fn  void format_disk_size(uint64_t size, char* buf, size_t buf_size);
 *
 * @brief Formats a disk size in KB, MB, GB or TB
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param           size      the size to be formatted
 * @param [in,out]  buf       target string buffer (string will be zero-terminated)
 * @param           buf_size  size of the string buffer in characters
 **************************************************************************************************/

void format_disk_size(uint64_t size, char* buf, size_t buf_size);

/**********************************************************************************************//**
 * @fn  uint64_t getDeviceSize(const char* device_name, uint32_t *logical_sector_size, uint32_t *physical_sector_size);
 *
 * @brief Gets the full size of the device / image file
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param           device_name           name of the device.
 * @param [in,out]  logical_sector_size   (optional) if not NULL, returns the logical sector size (usually 512, can be 4096 or 512 (emulation) for 4K drives)
 * @param [in,out]  physical_sector_size  (optional) if not NULL, returns the physical sector size (usually 512, can be 4096 for 4K drives)
 *
 * @returns the device size in number of 512 byte sectors (0 on error)
 **************************************************************************************************/

uint64_t getDeviceSize(const char* device_name, uint32_t *logical_sector_size, uint32_t *physical_sector_size);

uint64_t chs2lba ( uint32_t cylinder, uint32_t head, uint32_t sector );

void lba2chs ( uint64_t lba, uint32_t *cylinder, uint32_t *head, uint32_t *sector );

void hexdump ( const uint8_t *data, uint64_t size, uint64_t offset );

bool convertUTF162UTF8(const uint16_t* utf16, uint8_t* utf8, uint32_t utf8buffersize, bool win_console);

void format_guid(char* buffer, const uint8_t* guid, bool just_big_endian );

void parse_guid(uint8_t* guid, const char* guid_str, bool just_big_endian);

void convert_guid_from_to_mixed ( uint8_t *guid );

bool is_zero_guid(const uint8_t* guid);

typedef struct _disk_map *disk_map_ptr;

void free_disk_map ( disk_map_ptr dmp );

void format_64bit ( uint64_t x, char *buf, int buf_size );

bool check_device ( cmdline_args_ptr cap );

#ifdef _WINDOWS
int truncate(const char* file_name, uint64_t filesize);
#endif

#define MAX_STDOUT_CAPTURE            65536

int execute_external_program(uint8_t stdout_buffer[MAX_STDOUT_CAPTURE], bool wait_for_child, const char* prog, ...);

#ifdef _WINDOWS
int execute_external_program_with_input(const char* stdin_input, uint8_t stdout_buffer[MAX_STDOUT_CAPTURE], const char* prog, ...);
#endif

bool check_lba_range_is_free(disk_map_ptr dmp, uint64_t lba_start, uint64_t num_lbas);

bool find_last_partition(disk_ptr dp, disk_map_ptr dmp, uint64_t* lba_start, uint64_t* num_lbas, bool* is_ntfs, uint64_t* lba_free_start, uint64_t* num_lba_free);

#ifdef _WINDOWS

typedef struct _win_physicaldrive         win_physicaldrive, * win_physicaldrive_ptr;

struct _win_physicaldrive
{
  uint32_t              drive_no;
  char                  drive_description[256];
  char                  drive_vendor[128];
  char                  drive_product[128];
};

win_physicaldrive_ptr enumerateAllPhysicaldrives(uint32_t* numDrives);

win_volume_ptr enumerateAllWindowsVolumes(void);

#endif // _WINDOWS

#ifdef __cplusplus
}
#endif

#endif // _INC_PART_Y_H_

