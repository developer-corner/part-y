/**
 * @file   win_mbr2gpt.h
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @brief  declaration of structures and functions required to convert a Windows 10
 *         machine from an MBR-style partition table to the GUID Partition Table
 *         (most likely, Windows 11 will boot from secure UEFI setups only...)
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

#ifndef _INC_WIN_MBR2GPT_H_
#define _INC_WIN_MBR2GPT_H_

#ifdef __cplusplus
extern "C" {
#endif

#define MS_RESERVED_PART_SIZE         (16<<20)

#define STICK_BASE_PATH               "MBR2GPT"

#define MNT_LIVE_PATH                 "/run/live/medium"
#define LIVE_PATH                     MNT_LIVE_PATH "/MBR2GPT"

#define BASH_EXECUTABLE               "/bin/bash"
#define NTFS3G_EXECUTABLE             "/bin/ntfs-3g"
#define UNZIP_EXECUTABLE              "/usr/bin/unzip"
#define MKFS_FAT_EXECUTABLE           "/usr/sbin/mkfs.vfat"
#define PARTX_EXECUTABLE              "/usr/bin/partx"
#define MOUNT_EXECUTABLE              "/usr/bin/mount"
#define UMOUNT_EXECUTABLE             "/usr/bin/umount"
#define RM_EXECUTABLE                 "/usr/bin/rm"
#define CP_EXECUTABLE                 "/usr/bin/cp"
#define EFIBOOTMGR_EXECUTABLE         "/usr/bin/efibootmgr"
#define EFIBOOTMGR_EXECUTABLE2        LIVE_PATH "/efibootmgr"
#define MBR2GPT_TMP_PATH              LIVE_PATH "/mbr2gpt.tmp"
//#define MBR2GPT_TMP_WINDOWS_PATH      LIVE_PATH "/mbr2gpt.tmp/Windows"
#define MBR2GPT_TMP_EFI_PATH          LIVE_PATH "/mbr2gpt.tmp/EFI"
#define WINDOWS_EFI_DIR               "Windows/Boot/EFI"

#define WIN_EFIBOOTMGR_EXECUTABLE     "efibootmgr"
#define WIN_EFIBOOTMGR_SO_1           "libefiboot.so.1"
#define WIN_EFIBOOTMGR_SO_2           "libefivar.so.1"
#define WIN_EFIVAR_EXECUTABLE         "efivar"
#define WIN_PARTY_EXECUTABLE_LINUX    "part-y"

#define FILE_CONVERSION               "mbr2gpt.conversion"    // on Linux: LIVE_PATH "/" ...
#define FILE_BACKUP                   "mbr2gpt.backup"        // on Linux: LIVE_PATH "/" ...
#define FILE_BCD                      "mbr2gpt.bcd"           // on Linux: LIVE_PATH "/" ...
#define FILE_BCD_LOG                  "mbr2gpt.bcd.LOG"       // on Linux: LIVE_PATH "/" ...
#define FILE_BCD_LOG1                 "mbr2gpt.bcd.LOG1"      // on Linux: LIVE_PATH "/" ...
#define FILE_BCD_LOG2                 "mbr2gpt.bcd.LOG2"      // on Linux: LIVE_PATH "/" ...
#define DIR_WIN_EFI_FILES             "WindowsEFIFiles"       // on Linux: LIVE_PATH "/" DIR_WIN_EFI_FILES

#define FILE_EFI_ADDITION_DATA        "efi_load_option_additional.data"

#ifdef _WINDOWS
int onPrepareWindows10(cmdline_args_ptr cap);
#else
int win_mbr2gpt(cmdline_args_ptr cap);
#endif

#ifdef __cplusplus
}
#endif

#endif // _INC_WIN_MBR2GPT_H_

