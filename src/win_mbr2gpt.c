/**
 * @file   win_mbr2gpt.c
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @brief  implementation of functions required to convert a Windows 10
 *         machine from an MBR-style partition table to the GUID Partition Table
 *         (most likely, Windows 11 will boot from secure UEFI setups only...).
 *         The Microsoft supplied tool mbr2gpt.exe DOES ONLY convert a small
 *         subsets of all possible configurations.
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

#include <part-y.h>
#include <win_mbr2gpt.h>
#include <bcd.h>

#ifdef _WINDOWS
extern const uint8_t guid_microsoft_basic_data[16];
extern const uint8_t guid_efi_system_partition[16];
extern const uint8_t guid_microsoft_reserved[16];
extern const uint8_t guid_empty_partition[16];
#endif

static uint8_t stdout_buffer[MAX_STDOUT_CAPTURE];

uint8_t efi_load_option_additional_data_windows[0x88] =
{
  0x57,0x49,0x4e,0x44,0x4f,0x57,0x53,0x00,0x01,0x00,0x00,0x00,0x88,0x00,0x00,0x00,
  0x78,0x00,0x00,0x00,0x42,0x00,0x43,0x00,0x44,0x00,0x4f,0x00,0x42,0x00,0x4a,0x00,
  0x45,0x00,0x43,0x00,0x54,0x00,0x3d,0x00,0x7b,0x00,0x39,0x00,0x64,0x00,0x65,0x00,
  0x61,0x00,0x38,0x00,0x36,0x00,0x32,0x00,0x63,0x00,0x2d,0x00,0x35,0x00,0x63,0x00,
  0x64,0x00,0x64,0x00,0x2d,0x00,0x34,0x00,0x65,0x00,0x37,0x00,0x30,0x00,0x2d,0x00,
  0x61,0x00,0x63,0x00,0x63,0x00,0x31,0x00,0x2d,0x00,0x66,0x00,0x33,0x00,0x32,0x00,
  0x62,0x00,0x33,0x00,0x34,0x00,0x34,0x00,0x64,0x00,0x34,0x00,0x37,0x00,0x39,0x00,
  0x35,0x00,0x7d,0x00,0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x10,0x00,0x00,0x00,
  0x04,0x00,0x00,0x00,0x7f,0xff,0x04,0x00
};

#define DIR_ENTRY_FLAG_IS_FOLDER      0x00000001    ///< this is a folder, not a file
#define DIR_ENTRY_FLAG_NOT_SCANNED    0x80000000    ///< this folder was not yet scanned recursively

typedef struct _dir_entry             dir_entry, * dir_entry_ptr;

struct _dir_entry
{
  dir_entry_ptr                       prev;
  dir_entry_ptr                       next;
  dir_entry_ptr                       parent;
  dir_entry_ptr                       child;
  uint64_t                            filesize;       ///< 0 for folders!
  uint32_t                            flags;
  char                                filename[256];
  
};

#ifdef _WINDOWS

static void freeDirEntries(dir_entry_ptr p)
{
  if (NULL != p)
  {
    if (NULL != p->child)
      freeDirEntries(p->child);

    if (NULL != p->next)
      freeDirEntries(p->next);

    free(p);
  }
}

static void outputDirEntries(dir_entry_ptr p)
{
  if (NULL != p)
  {
    fprintf(stdout, "%s : %s\n", p->flags & DIR_ENTRY_FLAG_IS_FOLDER ? "FOLDER" : " FILE ", p->filename);

    if (NULL != p->child)
      outputDirEntries(p->child);

    if (NULL != p->next)
      outputDirEntries(p->next);
  }
}

static uint64_t estimateFolderSize(dir_entry_ptr p)
{
  uint64_t      totalSize = 0;

  if (NULL != p)
  {
    totalSize += (p->filesize + 4095) & (~4095);

    if (NULL != p->child)
      totalSize += estimateFolderSize(p->child);

    if (NULL != p->next)
      totalSize += estimateFolderSize(p->next);
  }

  return totalSize;
}

static bool copy_full_tree(dir_entry_ptr p, const char *src_base_path, const char *dst_base_path)
{
  char          dst_file[256];
  uint32_t      l1, l2;

  if (NULL != p)
  {
    l1 = (uint32_t)strlen(src_base_path);
    l2 = (uint32_t)strlen(dst_base_path);

    if (memcmp(src_base_path, p->filename, l1))
      return false;

    memcpy(dst_file, dst_base_path, l2);
    strncpy(dst_file + l2, p->filename + l1, sizeof(dst_file) - l2 - 1);

    if (p->flags & DIR_ENTRY_FLAG_IS_FOLDER)
    {
      if (!CreateDirectoryA(dst_file, NULL))
        return false;
    }
    else
    {
      if (!file_copy(p->filename, dst_file))
        return false;
    }

    if (NULL != p->child)
    {
      if (!copy_full_tree(p->child, p->filename, dst_file))
        return false;
    }

    if (NULL != p->next)
    {
      if (!copy_full_tree(p->next, src_base_path, dst_base_path))
        return false;
    }
  }
  return true;
}

win_physicaldrive_ptr enumerateAllPhysicaldrives(uint32_t *numDrives )
{
  win_physicaldrive_ptr   drives = NULL;
  HKEY                    hKey = NULL;
  DWORD                   dwType, dwData, dwDataSize;
  uint32_t                i;
  char                    szKey[16], szValue[512], * p, * p2, * p3;

  *numDrives = 0;

  if (ERROR_SUCCESS != RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Services\\disk\\Enum", 0, KEY_READ, &hKey))
    return NULL;

  dwDataSize = sizeof(DWORD);
  if (ERROR_SUCCESS != RegQueryValueEx(hKey, "Count", NULL, &dwType, (LPBYTE)&dwData, (LPDWORD)&dwDataSize))
  {
ErrorExit:
    RegCloseKey(hKey);
    return NULL;
  }
  if (REG_DWORD != dwType)
    goto ErrorExit;

  *numDrives = (uint32_t)dwData;
  if (0 == *numDrives)
    goto ErrorExit;

  drives = (win_physicaldrive_ptr)malloc(sizeof(win_physicaldrive) * dwData);
  if (NULL == drives)
  {
    *numDrives = 0;
    goto ErrorExit;
  }

  memset(drives, 0, sizeof(win_physicaldrive) * dwData);

  for (i = 0; i < *numDrives; i++)
  {
    drives[i].drive_no = i;
    snprintf(szKey, sizeof(szKey), "%u", i);
    memset(szValue, 0, sizeof(szValue));
    dwDataSize = sizeof(szValue) - 1;
    if ((ERROR_SUCCESS == RegQueryValueEx(hKey, szKey, NULL, &dwType, (LPBYTE)szValue, &dwDataSize)) && (REG_SZ == dwType))
    {
      strncpy(drives[i].drive_description, szValue, sizeof(drives[i].drive_description) - 1);
      p = strstr(szValue, "&Ven_");
      if (NULL != p)
      {
        p += 5;
        p2 = strchr(p, '&');
        if (NULL != p2)
        {
          *p2 = 0; // p points to Vendor (may be empty)
          p2++;
          if (!memcmp(p2, "Prod_", 5))
          {
            p2 += 5; // p2 points to product
            p3 = strchr(p2, '\\');
            if (NULL != p3)
              *p3 = 0;
            strncpy(drives[i].drive_product, p2, sizeof(drives[i].drive_product) - 1);
            strncpy(drives[i].drive_vendor, p, sizeof(drives[i].drive_vendor) - 1);
          }
        }
      }
    }
  }

  RegCloseKey(hKey);

  return drives;
}

static bool _scanFolderRecursively(dir_entry_ptr cur)
{
  if ((DIR_ENTRY_FLAG_IS_FOLDER | DIR_ENTRY_FLAG_NOT_SCANNED) == cur->flags)
  {
    HANDLE            h;
    WIN32_FIND_DATA   wfd;
    char              path[256];
    dir_entry_ptr     dep = NULL, tail = NULL;

    cur->flags &= ~DIR_ENTRY_FLAG_NOT_SCANNED;

    snprintf(path, sizeof(path), "%s\\*", cur->filename);
    h = FindFirstFile(path, &wfd);
    if (INVALID_HANDLE_VALUE != h)
    {
      do
      {
        if (0 != (FILE_ATTRIBUTE_DIRECTORY & wfd.dwFileAttributes)) // a folder
        {
          if ((strcmp(wfd.cFileName, ".")) && (strcmp(wfd.cFileName, "..")))
          {
            dep = (dir_entry_ptr)malloc(sizeof(dir_entry));
            if (NULL == dep)
              return false;
            memset(dep, 0, sizeof(dir_entry));
            snprintf(dep->filename, sizeof(dep->filename), "%s\\%s", cur->filename, wfd.cFileName);
            dep->flags = DIR_ENTRY_FLAG_IS_FOLDER | DIR_ENTRY_FLAG_NOT_SCANNED;
          }
          else
            dep = NULL;
        }
        else // file
        {
          dep = (dir_entry_ptr)malloc(sizeof(dir_entry));
          if (NULL == dep)
            return false;
          memset(dep, 0, sizeof(dir_entry));
          snprintf(dep->filename, sizeof(dep->filename), "%s\\%s", cur->filename, wfd.cFileName);
          dep->filesize = ((uint64_t)wfd.nFileSizeLow) | (((uint64_t)wfd.nFileSizeHigh) << 32);
        }

        if (NULL != dep)
        {
          if (NULL == tail)
            cur->child = dep;
          else
          {
            tail->next = dep;
            dep->prev = tail;
          }
          tail = dep;
          dep->parent = cur;
        }
      } while (FindNextFile(h, &wfd));
      
      FindClose(h);
    }
  }

  while (NULL != cur)
  {
    if (NULL != cur->child)
    {
      if (!_scanFolderRecursively(cur->child))
        return false;
    }

    cur = cur->next;
    if (NULL == cur)
      break;

    if ((DIR_ENTRY_FLAG_IS_FOLDER | DIR_ENTRY_FLAG_NOT_SCANNED) == cur->flags)
    {
      if (!_scanFolderRecursively(cur))
        return false;
    }
  }

  return true;
}

static dir_entry_ptr scanFolderRecursively(const char* folder)
{
  dir_entry_ptr     head;

  if (NULL == folder)
    return NULL;

  head = (dir_entry_ptr)malloc(sizeof(dir_entry));
  if (NULL == head)
    return NULL;
  memset(head, 0, sizeof(dir_entry));
  strncpy(head->filename, folder, sizeof(head->filename) - 1);
  head->flags = DIR_ENTRY_FLAG_IS_FOLDER | DIR_ENTRY_FLAG_NOT_SCANNED;

  if (!_scanFolderRecursively(head))
  {
    freeDirEntries(head);
    return NULL;
  }

  return head;
}

static uint8_t stdout_buffer[MAX_STDOUT_CAPTURE];

static bool diskpartFindVolumeForDriveLetter(cmdline_args_ptr cap, char drive_letter, uint32_t *volume_no )
{
  char            diskpart_exe[256];
  FILE           *script;
  int             exit_code;
  char           *p, *p2, *endp, this_drive_letter;
  uint32_t        vol_no;

  snprintf(diskpart_exe, sizeof(diskpart_exe), "%c:\\Windows\\System32\\diskpart.exe", cap->win_sys_drive);

  script = fopen(".\\diskpart.script", "wt");
  if (NULL == script)
    return false;

  fprintf(script, "list volume\n");
  fclose(script);

  exit_code = execute_external_program(stdout_buffer, true, diskpart_exe, "/s", ".\\diskpart.script", NULL);

  DeleteFile(".\\diskpart.script");

  if (0 != exit_code)
    return false;

  p = (char*)stdout_buffer;
  while ((0 != *p) && (NULL != (p2 = strchr(p, '\n'))))
  {
    *p2 = 0;
    if (!memcmp(p + 2, "Volume ", 7))
    {
      this_drive_letter = p[15];
      if ((this_drive_letter >= 'A') && (this_drive_letter <= 'Z'))
      {
        p += 9;
        vol_no = (uint32_t)strtoul(p, &endp, 10);
        if (NULL != endp)
        {
          if (drive_letter == this_drive_letter)
          {
            *volume_no = vol_no;
            return true;
          }
        }
      }
    }
    p = p2 + 1;
  }

  return false;
}

#else // _LINUX

typedef struct _mount_entry                 mount_entry, *mount_entry_ptr;

struct _mount_entry
{
  mount_entry_ptr   next;
  mount_entry_ptr   prev;
  char              device[128];
  char              path[128];
  char              fs[64];
  char              flags[128];
  uint32_t          device_len;
  uint32_t          path_len;
};

static void free_mount_entries ( mount_entry_ptr mep )
{
  mount_entry_ptr next;

  while (NULL!=mep)
  {
    next = mep->next;
    free(mep);
    mep = next;
  }
}

static mount_entry_ptr enumerate_mount_points ( void )
{
  mount_entry_ptr   head = NULL;
  mount_entry_ptr   tail = NULL;
  mount_entry_ptr   run;
  FILE             *f = fopen("/proc/mounts","rt");
  char              buffer[256], *p[3];

  if (NULL == f)
    return head;

  memset(buffer,0,sizeof(buffer));
  while (fgets(buffer,sizeof(buffer),f))
  {
    // buffer is device
    p[0] = strchr(buffer,' ');
    if (NULL!=p[0])
    {
      // p[0] + 1 is path
      p[1] = strchr(p[0]+1,' ');
      if (NULL!=p[1])
      {
        // p[1] + 1 is file system
        p[2] = strchr(p[1]+1,' ');
        if (NULL!=p[2])
        {
          // p[2] + 1 is flags and suffix

          p[0][0] = p[1][0] = p[2][0] = 0;

          run = (mount_entry_ptr)malloc(sizeof(mount_entry));
          if (unlikely(NULL==run))
          {
            fclose(f);
            free_mount_entries(head);
            return NULL;
          }

          memset(run,0,sizeof(mount_entry));
          strncpy(run->device,buffer,sizeof(run->device)-1);
          run->device_len = (uint32_t)strlen(run->device);
          strncpy(run->path,p[0]+1,sizeof(run->path)-1);
          run->path_len = (uint32_t)strlen(run->path);
          strncpy(run->fs,p[1]+1,sizeof(run->fs)-1);
          strncpy(run->flags,p[2]+1,sizeof(run->flags)-1);

          run->prev = tail;
          if (NULL!=tail)
            tail->next = run;
          else
            head = run;
          tail = run;
        }
      }
    }
    memset(buffer,0,sizeof(buffer));
  }
  fclose(f);

  return head;
}

static bool myumount ( const char *path )
{
  int retry_cnt = 3;

  while (0 != umount2(path,MNT_FORCE))
  {
    if (EBUSY == errno)
    {
      retry_cnt--;
      if (0==retry_cnt)
        return false;
    }
    sleep(1);
  }
  return true;
}

static int unmount_all ( const char *device )
{
  mount_entry_ptr     mep = enumerate_mount_points();
  mount_entry_ptr     run;
  uint32_t            l_device;
  int                 num_umounts = 0;

  if (unlikely(NULL==mep))
    return -1;

  l_device = (uint32_t)strlen(device);

  run = mep;
  while (NULL != run)
  {
    if (run->device_len > l_device)
    {
      if (!memcmp(device,run->device,l_device)) // found
      {
        myumount(run->device);
        num_umounts++;
      }
    }

    run = run->next;
  }

  free_mount_entries(mep);

  return num_umounts;
}

static int remount_usb_stick_rw ( const char *path )
{
  mount_entry_ptr     mep = enumerate_mount_points();
  mount_entry_ptr     run;
  uint32_t            l_path;

  if (unlikely(NULL==mep))
    return -1;

  l_path = (uint32_t)strlen(path);

  run = mep;
  while (NULL != run)
  {
    if (run->path_len == l_path)
    {
      if (!memcmp(path,run->path,l_path)) // found
      {
        if (!memcmp(run->flags,"rw,",3)) // is already read/write
        {
          free_mount_entries(mep);
          return 0;
        }

        free_mount_entries(mep);

        if (0 != execute_external_program(NULL, true, MOUNT_EXECUTABLE, "-o", "remount,rw", path,NULL))
          return -1;

        return 0;
      }
    }

    run = run->next;
  }

  free_mount_entries(mep);

  return -1;
}

static void format_partition_device_name ( char *device_file, size_t device_file_size, const char *disk_device, uint32_t part_no )
{
  if ((NULL != strstr(disk_device,"loop")) || (NULL != strstr(disk_device,"nvm")))
    snprintf(device_file,device_file_size,"%sp%u",disk_device,part_no);
  else
    snprintf(device_file,device_file_size,"%s%u",disk_device,part_no);
}

#define MAX_PARTX_TRIES         5

int win_mbr2gpt(cmdline_args_ptr cap)
{
  uint32_t              l = (uint32_t)strlen(cap->device_name);
  int                   num_umounts;
  char                  efi_boot_mgr_executable[256], bcd_file[256], convert_file[256];
  uint8_t              *conversion_memory_pool = NULL, *conversion_data = NULL;
  FILE_HANDLE           f;
  disk_ptr              dp = NULL;
  uint64_t              device_size;
  uint32_t              i, logical_sector_size;
  DISK_HANDLE           d;
  char                  device_file[256], exec_cmd_line[256];
  FILE                 *fi;
  char                  buffer[256];
  bool                  found = false;
  uint8_t               all_guids[128];

  if (NULL == cap->work_disk)
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET ": No working disk available.\n");
    return 1;
  }

  if (l < 5 || memcmp(cap->device_name, "/dev/", 5))
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET ": The device (drive) name must begin with '/dev/'. STOP.\n");
    return 1;
  }

  if (cap->dryrun)
    fprintf(stdout, CTRL_CYAN " * DRYRUN " CTRL_MAGENTA "- will NOT modify the device!\n\n" CTRL_RESET);

  // 0.) Check that backup file is available
#if 0
  fprintf(stdout, CTRL_CYAN "CHECKING" CTRL_RESET ": Have backup file ........................................: ");
  fflush(stdout);
  if (access(cap->backup_file, F_OK) == 0)
    fprintf(stdout, CTRL_GREEN "OK" CTRL_RESET "\n");
  else
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          The backup file %s is not available.\n",cap->backup_file);
    return 1;
  }
#endif

  // 1.) Check that all required executables are available

  fprintf(stdout, CTRL_CYAN "CHECKING" CTRL_RESET ": Have make FAT32 partition tool ..........................: ");
  fflush(stdout);
  if (access(MKFS_FAT_EXECUTABLE, X_OK) == 0)
    fprintf(stdout, CTRL_GREEN "OK" CTRL_RESET "\n");
  else
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          The required tool %s is not available.\n", MKFS_FAT_EXECUTABLE);
    return 1;
  }

  fprintf(stdout, CTRL_CYAN "CHECKING" CTRL_RESET ": Have partx tool .........................................: ");
  fflush(stdout);
  if (access(PARTX_EXECUTABLE, X_OK) == 0)
    fprintf(stdout, CTRL_GREEN "OK" CTRL_RESET "\n");
  else
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          The required tool %s is not available.\n", PARTX_EXECUTABLE);
    return 1;
  }

  fprintf(stdout, CTRL_CYAN "CHECKING" CTRL_RESET ": Have mount tool .........................................: ");
  fflush(stdout);
  if (access(MOUNT_EXECUTABLE, X_OK) == 0)
    fprintf(stdout, CTRL_GREEN "OK" CTRL_RESET "\n");
  else
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          The required tool %s is not available.\n", MOUNT_EXECUTABLE);
    return 1;
  }

  fprintf(stdout, CTRL_CYAN "CHECKING" CTRL_RESET ": Have umount tool ........................................: ");
  fflush(stdout);
  if (access(UMOUNT_EXECUTABLE, X_OK) == 0)
    fprintf(stdout, CTRL_GREEN "OK" CTRL_RESET "\n");
  else
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          The required tool %s is not available.\n", UMOUNT_EXECUTABLE);
    return 1;
  }

  fprintf(stdout, CTRL_CYAN "CHECKING" CTRL_RESET ": Have cp tool ............................................: ");
  fflush(stdout);
  if (access(CP_EXECUTABLE, X_OK) == 0)
    fprintf(stdout, CTRL_GREEN "OK" CTRL_RESET "\n");
  else
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          The required tool %s is not available.\n", CP_EXECUTABLE);
    return 1;
  }

  fprintf(stdout, CTRL_CYAN "CHECKING" CTRL_RESET ": Have rm tool ............................................: ");
  fflush(stdout);
  if (access(RM_EXECUTABLE, X_OK) == 0)
    fprintf(stdout, CTRL_GREEN "OK" CTRL_RESET "\n");
  else
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          The required tool %s is not available.\n", RM_EXECUTABLE);
    return 1;
  }

  fprintf(stdout, CTRL_CYAN "CHECKING" CTRL_RESET ": Have ntfs-3g tool .......................................: ");
  fflush(stdout);
  if (access(NTFS3G_EXECUTABLE, X_OK) == 0)
    fprintf(stdout, CTRL_GREEN "OK" CTRL_RESET "\n");
  else
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          The required tool %s is not available.\n", NTFS3G_EXECUTABLE);
    return 1;
  }

  putenv("LD_LIBRARY_PATH=" LIVE_PATH);

  fprintf(stdout, CTRL_CYAN "CHECKING" CTRL_RESET ": Have efibootmgr tool ....................................: ");
  fflush(stdout);

  strncpy(efi_boot_mgr_executable,EFIBOOTMGR_EXECUTABLE,sizeof(efi_boot_mgr_executable)-1);

  if (access(efi_boot_mgr_executable, X_OK) == 0)
    fprintf(stdout, CTRL_GREEN "OK" CTRL_RESET "\n");
  else
  {
    strncpy(efi_boot_mgr_executable,EFIBOOTMGR_EXECUTABLE2,sizeof(efi_boot_mgr_executable)-1);
    if (access(efi_boot_mgr_executable, X_OK) == 0)
      fprintf(stdout, CTRL_GREEN "OK" CTRL_RESET "\n");
    else
    {
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          The required tool %s is not available.\n", EFIBOOTMGR_EXECUTABLE);
      return 1;
    }
  }

  fprintf(stdout, CTRL_CYAN "CHECKING" CTRL_RESET ": Can efibootmgr operate on EFI variables .................: ");
  fflush(stdout);

  snprintf(exec_cmd_line,sizeof(exec_cmd_line),"LD_LIBRARY_PATH=" LIVE_PATH " %s",efi_boot_mgr_executable);

  if (0 != execute_external_program(stdout_buffer,true,BASH_EXECUTABLE,"-c",exec_cmd_line,NULL))
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          efibootmgr is UNABLE to set EFI variables.\n");
    fprintf(stdout, "               Please ensure that you have booted in EFI mode.\n");
    return 1;
  }
  fprintf(stdout, CTRL_GREEN "OK" CTRL_RESET "\n");

  fprintf(stdout, CTRL_CYAN "CHECKING" CTRL_RESET ": Have vfat in /proc/filesystems ..........................: ");
  fflush(stdout);
  fi = fopen("/proc/filesystems","rt");
  if (NULL==fi)
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to read /proc/filesystems.\n");
    return 1;
  }
  while (NULL!=fgets(buffer,sizeof(buffer),fi))
  {
    if (NULL!=strstr(buffer,"vfat"))
    {
      found = true;
      break;
    }
  }
  fclose(fi);
  if (!found)
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          No 'vfat' found in /proc/filesystems.\n");
    return 1;
  }
  fprintf(stdout, CTRL_GREEN "OK" CTRL_RESET "\n");

  if (!cap->dryrun)
  {
    // Unmount all mount points of the target device

    fprintf(stdout, CTRL_CYAN "WORKING " CTRL_RESET ": Unmounting all mount points of target device ............: ");
    fflush(stdout);
    num_umounts = unmount_all(cap->device_name);
    if (num_umounts < 0)
    {
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to unmount target mount points.\n");
      return 1;
    }
    fprintf(stdout, CTRL_GREEN "OK[%i]" CTRL_RESET "\n", num_umounts);

    // Re-mount USB stick read/write

    fprintf(stdout, CTRL_CYAN "WORKING " CTRL_RESET ": Re-mounting USB live stick read/write ...................: ");
    fflush(stdout);
    if (0 != remount_usb_stick_rw(MNT_LIVE_PATH))
    {
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to re-mount USB stick (mount point: %s).\n",MNT_LIVE_PATH);
      return 1;
    }
    fprintf(stdout, CTRL_GREEN "OK" CTRL_RESET "\n");

    // Recursively delete any old temporary directory

    (void)execute_external_program(NULL, true, RM_EXECUTABLE, "-rf", MBR2GPT_TMP_PATH,NULL);

    // Create new (fresh) temporary directory

    fprintf(stdout, CTRL_CYAN "WORKING " CTRL_RESET ": Create temporary working directory ......................: ");
    fflush(stdout);
    if (0 != mkdir(MBR2GPT_TMP_PATH,0775))
    {
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n");
      return 1;
    }
    if (0 != mkdir(MBR2GPT_TMP_EFI_PATH,0775))
    {
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n");
      return 1;
    }

    fprintf(stdout, CTRL_GREEN "OK" CTRL_RESET "\n");

    // Locate the Windows drive and mount it

#if 0
    fprintf(stdout, CTRL_CYAN "WORKING " CTRL_RESET ": Locate windows directory and mount it ...................: ");
    fflush(stdout);
    if (0 != find_and_mount_windows_directory(cap))
    {
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n");
      return 1;
    }
    fprintf(stdout, CTRL_GREEN "OK" CTRL_RESET "\n");
#endif

    // Check that prepared Boot Configuration Data is there

    fprintf(stdout, CTRL_CYAN "CHECKING" CTRL_RESET ": Have prepared Boot Configuration Data ...................: ");
    fflush(stdout);
    snprintf(bcd_file,sizeof(bcd_file),LIVE_PATH "/" FILE_BCD);
    if (access(bcd_file, F_OK) == 0)
      fprintf(stdout, CTRL_GREEN "OK" CTRL_RESET "\n");
    else
    {
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          The Boot Configuration Data file %s is not available.\n",bcd_file);
      return 1;
    }

    // Check that prepared convert file is there

    fprintf(stdout, CTRL_CYAN "CHECKING" CTRL_RESET ": Have prepared conversion file (MBR to GPT) ..............: ");
    fflush(stdout);
    snprintf(convert_file,sizeof(convert_file),LIVE_PATH "/" FILE_CONVERSION);
    if (access(convert_file, F_OK) == 0)
      fprintf(stdout, CTRL_GREEN "OK" CTRL_RESET "\n");
    else
    {
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          The conversion file %s is not available.\n",convert_file);
      return 1;
    }

#if 0
    // Copy BCD file to temporary folder

    fprintf(stdout, CTRL_CYAN "WORKING " CTRL_RESET ": Copy prepared Boot Configuration Data ...................: ");
    fflush(stdout);
    if (0 != execute_external_program(NULL, true, CP_EXECUTABLE, MBR2GPT_TMP_WINDOWS_PATH "/mbr2gpt.BCD", MBR2GPT_TMP_PATH "/mbr2gpt.BCD",NULL))
    {
      myumount(MBR2GPT_TMP_WINDOWS_PATH);
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to copy the prepared BCD file.\n");
      return 1;
    }
    fprintf(stdout, CTRL_GREEN "OK" CTRL_RESET "\n");
#endif

    // Read prepared conversion file

    fprintf(stdout, CTRL_CYAN "WORKING " CTRL_RESET ": Read prepared conversion file ...........................: ");
    fflush(stdout);

    conversion_memory_pool = (uint8_t*)malloc( (3 + 33 + 33) * SECTOR_SIZE + 4096);

    if (NULL==conversion_memory_pool)
    {
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Insufficient memory available.\n");
      return 1;
    }

    conversion_data = (uint8_t*)((((uint64_t)conversion_memory_pool) + 4095) & ~4095);

    f = file_open(convert_file,true/*read-only*/);
    if (INVALID_FILE_HANDLE == f)
    {
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to read the prepared conversion file.\n");
      free(conversion_memory_pool);
      return 1;
    }
    if (!file_read(f,conversion_data, (3 + 33 + 33) * SECTOR_SIZE))
    {
      file_close(f, false/*no flush*/);
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to read the prepared conversion file.\n");
      free(conversion_memory_pool);
      return 1;
    }

    file_close(f, false/*no flush*/);

    fprintf(stdout, CTRL_GREEN "OK" CTRL_RESET "\n");

    // Ensure that the selected disk matches the disk used to prepare everything

    fprintf(stdout, CTRL_CYAN "WORKING " CTRL_RESET ": Ensure that the selected disk matches the preparations ..: ");
    fflush(stdout);
    dp = cap->work_disk;
    if (NULL == dp)
    {
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to scan the device %s.\n",cap->device_name);
      free(conversion_memory_pool);
      return 1;
    }

    device_size = READ_BIG_ENDIAN64(conversion_data,0x0000);
    logical_sector_size = READ_BIG_ENDIAN32(conversion_data,0x0008);
    //physical_sector_size = READ_BIG_ENDIAN32(conversion_data,0x000C);

    if (NULL == dp->mbr)
    {
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          No MBR-style partition table found on the device %s.\n",cap->device_name);
      free(conversion_memory_pool);
      return 1;
    }

    if (memcmp(dp->mbr->sp->data+0x10,&conversion_data[0x10],SECTOR_SIZE-0x10))
    {
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          The MBR of the device %s mismatches the stored MBR.\n",cap->device_name);
      free(conversion_memory_pool);
      return 1;
    }

    if (device_size != dp->device_size)
    {
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          The size of the device %s differ from preparation.\n",cap->device_name);
      free(conversion_memory_pool);
      return 1;
    }

    if (logical_sector_size != dp->logical_sector_size)
    {
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          The logical sector size of the device %s differ from preparation.\n",cap->device_name);
      free(conversion_memory_pool);
      return 1;
    }

    // do NOT compare physical sector sizes because it sometimes differ between Windows and Linux

    fprintf(stdout, CTRL_GREEN "OK" CTRL_RESET "\n");

    // Inform the kernel to remove all partitions

    fprintf(stdout, CTRL_CYAN "WORKING " CTRL_RESET ": Discard all partitions of the target disk ...............: ");
    fflush(stdout);
    if (0!=execute_external_program(NULL, true, PARTX_EXECUTABLE,"-d",cap->device_name,NULL))
    {
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to discard the (old) partitions of the disk device %s.\n",cap->device_name);
      free(conversion_memory_pool);
      return 1;
    }

    format_partition_device_name(device_file,sizeof(device_file),cap->device_name,1);
    while(1)
    {
      d = disk_open_device(device_file, false /* read-only */);
      if (INVALID_DISK_HANDLE == d)
        break;
      close(d);
      sleep(1);

      if (0!=execute_external_program(NULL, true, PARTX_EXECUTABLE,"-d",cap->device_name,NULL))
      {
        fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to discard the (old) partitions of the disk device %s (follow-up tries).\n",cap->device_name);
        free(conversion_memory_pool);
        return 1;
      }
    }

    fprintf(stdout, CTRL_GREEN "OK" CTRL_RESET "\n");

    // ACTION: Write the MBR or protective MBR plus the primary and the backup GPT partition tables to the disk

    fprintf(stdout, CTRL_CYAN "WORKING " CTRL_RESET ": Write the (protective) MBR plus GPTs to the disk ........: ");
    fflush(stdout);
    d = disk_open_device(cap->device_name, true/*read/write access*/);
    if (INVALID_DISK_HANDLE == d)
    {
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to open the disk device %s for reading and writing.\n",cap->device_name);
      free(conversion_memory_pool);
      return 1;
    }

    if (!disk_write(dp, d, 0, &conversion_data[SECTOR_SIZE], 34 * SECTOR_SIZE))
    {
      disk_close_device(d);
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to write the MBR plus the primary GPT to the disk %s.\n",cap->device_name);
      free(conversion_memory_pool);
      return 1;
    }

    if (!disk_write(dp, d, device_size - 33 * SECTOR_SIZE, &conversion_data[35 * SECTOR_SIZE], 33 * SECTOR_SIZE))
    {
      disk_close_device(d);
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to write the backup GPT to the disk %s.\n",cap->device_name);
      free(conversion_memory_pool);
      return 1;
    }

    fprintf(stdout, CTRL_GREEN "OK" CTRL_RESET "\n");

    disk_close_device(d);
    dp = NULL;

    // run partx to re-scan the modified partition table(s)

    fprintf(stdout, CTRL_CYAN "WORKING " CTRL_RESET ": Run partx tool to re-read the modified partition table(s): ");
    fflush(stdout);

    sleep(3);

    for (i = 0; i < MAX_PARTX_TRIES; i++)
    {
      if (0==execute_external_program(NULL, true, PARTX_EXECUTABLE,"-a",cap->device_name,NULL))
        break;
      sleep(1);
      (void)execute_external_program(NULL, true, PARTX_EXECUTABLE,"-d",cap->device_name,NULL);
      sleep(1);
    }

    if (MAX_PARTX_TRIES == i)
    {
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to re-read the (new) partitions of the disk device %s.\n",cap->device_name);
      free(conversion_memory_pool);
      return 1;
    }
    fprintf(stdout, CTRL_GREEN "OK" CTRL_RESET "\n");

    // zero-out some space on the 1st (ESP) and 2nd (MS Reserved)

    memcpy(all_guids, conversion_data + (2 + 33 + 33) * SECTOR_SIZE, 128);

    memset(conversion_data,0,(3 + 33 + 33) * SECTOR_SIZE);

    fprintf(stdout, CTRL_CYAN "WORKING " CTRL_RESET ": Zero-out some space on 1st and 2nd GPT partitions .......: ");
    fflush(stdout);

    format_partition_device_name(device_file,sizeof(device_file),cap->device_name,1);
    d = disk_open_device(device_file, true /* write access */);
    if (INVALID_DISK_HANDLE == d)
    {
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to zero-out some space on the 1st GPT partition on the device %s.\n",cap->device_name);
      free(conversion_memory_pool);
      return 1;
    }

    if (!disk_write(NULL, d, 0, conversion_data, (3 + 33 + 33) * SECTOR_SIZE))
    {
      disk_close_device(d);
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to zero-out some space on the 1st GPT partition on the device %s.\n",cap->device_name);
      free(conversion_memory_pool);
      return 1;
    }

    disk_close_device(d);

    format_partition_device_name(device_file,sizeof(device_file),cap->device_name,2);
    d = disk_open_device(device_file, true /* write access */);
    if (INVALID_DISK_HANDLE == d)
    {
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to zero-out some space on the 2nd GPT partition on the device %s.\n",cap->device_name);
      free(conversion_memory_pool);
      return 1;
    }

    if (!disk_write(NULL, d, 0, conversion_data, (3 + 33 + 33) * SECTOR_SIZE))
    {
      disk_close_device(d);
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to zero-out some space on the 2nd GPT partition on the device %s.\n",cap->device_name);
      free(conversion_memory_pool);
      return 1;
    }

    disk_close_device(d);

    fprintf(stdout, CTRL_GREEN "OK" CTRL_RESET "\n");

    // Format the ESP with FAT32 (1st GPT partition)

    fprintf(stdout, CTRL_CYAN "WORKING " CTRL_RESET ": Format EFI System Partition with FAT32 ..................: ");
    fflush(stdout);

    format_partition_device_name(device_file,sizeof(device_file),cap->device_name,1);
    if (0!=execute_external_program(NULL, true, MKFS_FAT_EXECUTABLE,"-F","32","-n","EFI-SYSTEM",device_file,NULL))
    {
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to format the 1st GPT partition (ESP) with FAT32 on the device %s.\n",cap->device_name);
      free(conversion_memory_pool);
      return 1;
    }

    fprintf(stdout, CTRL_GREEN "OK" CTRL_RESET "\n");

    // Mount new FAT32-formatted ESP

    fprintf(stdout, CTRL_CYAN "WORKING " CTRL_RESET ": Mount FAT32 formatted EFI System Partition ..............: ");
    fflush(stdout);
    if (0 != mount(device_file,MBR2GPT_TMP_EFI_PATH,"vfat",0,NULL))
    {
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to mount 1st GPT partition (ESP) on the device %s.\n",cap->device_name);
      free(conversion_memory_pool);
      return 1;
    }

    fprintf(stdout, CTRL_GREEN "OK" CTRL_RESET "\n");

#if 0
    // Locate the Windows drive and mount it

    fprintf(stdout, CTRL_CYAN "WORKING " CTRL_RESET ": Locate windows directory and mount it ...................: ");
    fflush(stdout);
    if (0 != find_and_mount_windows_directory(cap))
    {
      myumount(MBR2GPT_TMP_EFI_PATH);
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n");
      return 1;
    }
    fprintf(stdout, CTRL_GREEN "OK" CTRL_RESET "\n");
#endif

    // Recursively copy the EFI directory from the Windows installation to the EFI System Partition (ESP)

    fprintf(stdout, CTRL_CYAN "WORKING " CTRL_RESET ": Recursively copy EFI directory of Windows to the ESP ....: ");
    fflush(stdout);

    if (((0!=mkdir(MBR2GPT_TMP_EFI_PATH "/EFI",0775))) ||
        ((0!=mkdir(MBR2GPT_TMP_EFI_PATH "/EFI/Microsoft",0775))) ||
        ((0!=mkdir(MBR2GPT_TMP_EFI_PATH "/EFI/Microsoft/Boot",0775))) ||
        ((0!=mkdir(MBR2GPT_TMP_EFI_PATH "/EFI/Microsoft/Recovery",0775))) ||
        ((0!=mkdir(MBR2GPT_TMP_EFI_PATH "/EFI/Boot", 0775))))
    {
      myumount(MBR2GPT_TMP_EFI_PATH);
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to create required folders on the ESP.\n");
      free(conversion_memory_pool);
      return 1;
    }

    snprintf(exec_cmd_line,sizeof(exec_cmd_line),CP_EXECUTABLE " -pR " LIVE_PATH "/" DIR_WIN_EFI_FILES "/* " MBR2GPT_TMP_EFI_PATH "/EFI/Microsoft/Boot/");

    if (0!=execute_external_program(NULL, true, BASH_EXECUTABLE, "-c", exec_cmd_line,NULL))
    {
      myumount(MBR2GPT_TMP_EFI_PATH);
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to recursively copy EFI files/folders on the ESP.\n");
      free(conversion_memory_pool);
      return 1;
    }

    // copy bootmgfw.efi to bootx64.efi, too

    if (0!=execute_external_program(NULL, true, CP_EXECUTABLE, LIVE_PATH "/" DIR_WIN_EFI_FILES "/bootmgfw.efi", MBR2GPT_TMP_EFI_PATH "/EFI/Boot/bootx64.efi",NULL))
    {
      myumount(MBR2GPT_TMP_EFI_PATH);
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to copy bootmgfw.efi to bootx64.efi.\n");
      free(conversion_memory_pool);
      return 1;
    }

    fprintf(stdout, CTRL_GREEN "OK" CTRL_RESET "\n");

    // Patch BCD file according to new GUIDs in the GPT

    fprintf(stdout, CTRL_CYAN "WORKING " CTRL_RESET ": Patch Boot Configuration Data (BCD), GPT GUIDs ..........: ");
    fflush(stdout);
    if (!patch_device_partition_guids(bcd_file,
                                      MBR2GPT_TMP_EFI_PATH "/EFI/Microsoft/Boot/BCD",
                                      &all_guids[  0],  // GUID MBR disk
                                      &all_guids[ 32],  // GUID MBR EFI
                                      &all_guids[ 64],  // GUID MBR Windows system
                                      &all_guids[ 96],  // GUID MBR Windows RE
                                      &all_guids[ 16],  // GUID MBR disk
                                      &all_guids[ 48],  // GUID MBR EFI
                                      &all_guids[ 80],  // GUID MBR Windows system
                                      &all_guids[112])) // GUID MBR Windows RE
    {
      myumount(MBR2GPT_TMP_EFI_PATH);
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to patch the GPT GUIDs into the Boot Configuration Data (BCD).\n");
      free(conversion_memory_pool);
      return 1;
    }
    fprintf(stdout, CTRL_GREEN "OK" CTRL_RESET "\n");

    free(conversion_memory_pool);
    conversion_memory_pool = NULL;
    conversion_data = NULL;

    // Copy Boot Configuration Data (BCD) into the ESP

    fprintf(stdout, CTRL_CYAN "WORKING " CTRL_RESET ": Copy Boot Configuration Data (BCD) to the ESP (Recovery) : ");
    fflush(stdout);
    if (0!=execute_external_program(NULL, true, CP_EXECUTABLE,MBR2GPT_TMP_EFI_PATH "/EFI/Microsoft/Boot/BCD",MBR2GPT_TMP_EFI_PATH "/EFI/Microsoft/Recovery/BCD",NULL))
    {
      myumount(MBR2GPT_TMP_EFI_PATH);
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to copy the Boot Configuration Data (Recovery) to the ESP.\n");
      return 1;
    }

    // optionally copy BCD LOG files but do not fail if they do not exist (Windows will create them itself)

    //(void)execute_external_program(NULL, true, CP_EXECUTABLE,LIVE_PATH "/" FILE_BCD_LOG,MBR2GPT_TMP_EFI_PATH "/EFI/Microsoft/Boot/BCD.LOG",NULL);
    //(void)execute_external_program(NULL, true, CP_EXECUTABLE,LIVE_PATH "/" FILE_BCD_LOG1,MBR2GPT_TMP_EFI_PATH "/EFI/Microsoft/Boot/BCD.LOG1",NULL);
    //(void)execute_external_program(NULL, true, CP_EXECUTABLE,LIVE_PATH "/" FILE_BCD_LOG2,MBR2GPT_TMP_EFI_PATH "/EFI/Microsoft/Boot/BCD.LOG2",NULL);

    myumount(MBR2GPT_TMP_EFI_PATH);

    fprintf(stdout, CTRL_GREEN "OK" CTRL_RESET "\n");

    // run efibootmgr to establish boot entry in NVRAM

    fprintf(stdout, CTRL_CYAN "WORKING " CTRL_RESET ": Run efibootmgr to establish new start entry .............: ");
    fflush(stdout);

    f = file_open(MBR2GPT_TMP_PATH "/" FILE_EFI_ADDITION_DATA,false/*read-write*/);
    if (INVALID_FILE_HANDLE == f)
    {
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to create the efibootmgr additional data file: %s\n",MBR2GPT_TMP_PATH "/" FILE_EFI_ADDITION_DATA);
      return 1;
    }
    if (!file_write(f,efi_load_option_additional_data_windows, sizeof(efi_load_option_additional_data_windows)))
    {
      file_close(f, true/*do flush*/);
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to write the efibootmgr additional data file: %s\n",MBR2GPT_TMP_PATH "/" FILE_EFI_ADDITION_DATA);
      return 1;
    }

    file_close(f, true/*do flush*/);

    snprintf(exec_cmd_line,
             sizeof(exec_cmd_line),
             "LD_LIBRARY_PATH=" LIVE_PATH
             " %s -c -d %s -g -l '\\EFI\\MICROSOFT\\BOOT\\BOOTMGFW.EFI' -L 'Windows Boot Manager' -p 1 -@ " MBR2GPT_TMP_PATH "/" FILE_EFI_ADDITION_DATA,
             efi_boot_mgr_executable,
             cap->device_name);

    if (0 != execute_external_program(NULL,true,BASH_EXECUTABLE,"-c",exec_cmd_line,NULL))
    {
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to establish new Windows Boot Manager EFI NVRAM load option.\n");
      return 1;
    }

    fprintf(stdout, CTRL_GREEN "OK" CTRL_RESET "\n");

    fprintf(stdout, "\nPlease reboot the system now - Windows should start in UEFI mode.\n");
  } // of !dry run

  return 0;
}

#endif // !_WINDOWS

#ifdef _WINDOWS

static bool locateMBRWindowsBootPartition(mbr_part_sector_ptr mpsp, uint32_t *part_idx_in_mbr, uint64_t *start_lba, uint32_t *num_lbas)
{
  uint32_t          i;

  // Check the MBR only, none of the possibly available extended partition tables
  
  if (NULL == mpsp || NULL == part_idx_in_mbr || NULL == start_lba || NULL == num_lbas)
    return false;

  for (i = 0; i < 4; i++)
  {
    if ((0x80 == mpsp->part_table[i].boot_flag) && (0x07 == mpsp->part_table[i].part_type)) // bootable + NTFS format
    {
      *part_idx_in_mbr = i;
      *start_lba = mpsp->part_table[i].start_sector;
      *num_lbas = mpsp->part_table[i].num_sectors;

      return true;
    }
  }
  return false;
}

static bool locateWindowsRecoveryEnvironmentPartition(mbr_part_sector_ptr mpsp, uint64_t* start_lba, uint32_t* num_lbas)
{
  uint32_t          i;
  bool              found = false;

  // Check all partitions, including extended partitions

  if (NULL == mpsp || NULL == start_lba || NULL == num_lbas)
    return false;

  while (NULL != mpsp)
  {
    for (i = 0; i < 4; i++)
    {
      if (0x27 == mpsp->part_table[i].part_type) // "Hidden NTFS Windows RE"
      {
        *start_lba = mpsp->part_table[i].start_sector;
        *num_lbas = mpsp->part_table[i].num_sectors;
        found = true;
        // we do NOT stop here because there may be more than just one WinRE partitions -> we take the highest one
      }
    }
    mpsp = mpsp->next;
  }

  return found;
}

static bool diskpartShrinkVolume(cmdline_args_ptr cap, uint64_t last_partition_lba_start, uint32_t num_mb)
{
  char                    diskpart_exe[256];
  FILE                   *script;
  int                     exit_code;
  diskpart_volume_ptr     dvp = cap->dvp;

  if (NULL == cap->wvp || NULL == cap->dvp) // unable to get synchronized volume numbers, especially for diskpart.exe, abort!
    return false;

  while (NULL != dvp)
  {
    if (dvp->start_lba == last_partition_lba_start)
      break;
    dvp = dvp->next;
  }

  if (NULL == dvp)
    return false; // diskpart volume number not found!

  snprintf(diskpart_exe, sizeof(diskpart_exe), "%c:\\Windows\\System32\\diskpart.exe", cap->win_sys_drive);

  script = fopen(".\\diskpart.script", "wt");
  if (NULL == script)
    return false;

  fprintf(script, "select volume %u\n", dvp->volume_number);
  fprintf(script, "shrink desired=%u minimum=%u\n", num_mb, num_mb);
  fclose(script);

  exit_code = execute_external_program(stdout_buffer, true, diskpart_exe, "/s", ".\\diskpart.script", NULL);

  DeleteFile(".\\diskpart.script");

  return (0 == exit_code) ? true : false;
}

static bool determine_maximum_shrink_size(cmdline_args_ptr cap, uint64_t last_partition_lba_start, uint32_t *max_shrink_mb_size)
{
  char                    diskpart_exe[256];
  FILE                   *script;
  int                     exit_code;
  diskpart_volume_ptr     dvp = cap->dvp;
  char                   *p, *p2;

  if (NULL == cap->wvp || NULL == cap->dvp) // unable to get synchronized volume numbers, especially for diskpart.exe, abort!
    return false;

  while (NULL != dvp)
  {
    if (dvp->start_lba == last_partition_lba_start)
      break;
    dvp = dvp->next;
  }

  if (NULL == dvp)
    return false; // diskpart volume number not found!

  snprintf(diskpart_exe, sizeof(diskpart_exe), "%c:\\Windows\\System32\\diskpart.exe", cap->win_sys_drive);

  script = fopen(".\\diskpart.script", "wt");
  if (NULL == script)
    return false;

  fprintf(script, "select volume %u\n",dvp->volume_number);
  fprintf(script, "shrink querymax\n");
  fclose(script);

  exit_code = execute_external_program(stdout_buffer, true, diskpart_exe, "/s", ".\\diskpart.script", NULL);

  DeleteFile(".\\diskpart.script");

  if (0 != exit_code)
    return false;

  // output is like this (German Windows version): "Maximale Anzahl der Bytes, die freigegeben werden k�nnen:   72 GB (74125 MB)"

  p2 = strstr((const char*)stdout_buffer, " MB)");

  if (NULL == p2)
    return false;

  p = p2 - 1;

  while ((p > ((const char*)stdout_buffer)) && ('(' != *p))
    p--;

  if ('(' != *p)
    return false;

  *p2 = 0;
   
  *max_shrink_mb_size = (uint32_t)strtoul(p + 1, NULL, 10);

  return true;
}

//static const uint8_t guid_mbr_extended_partition[16] = { 'M','B','R','E','X','T','-','P','A','R','T','I','T','I','O','N' }; // Big Endian
static const uint8_t guid_mbr_extended_partition[16] = { 'E','R','B', 'M', 'T','X', 'P','-',  'A','R','T','I','T','I','O','N' }; // Mixed Endian

int onPrepareWindows10(cmdline_args_ptr cap)
{
#ifdef _LINUX
  (void)cap;
  fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET ": This tool is executable on MS Windows only.\n");
  return 1;
#else
  char                  buffer[256], buffer2[256], str1[64], str2[64], convert_file[256], bcd_file[256], message[128], linux_stick[256], szPartYFilePath[256];
  dir_entry_ptr         dep = NULL;
  uint64_t              bootEfiSize, efi_partition_lba = 0, msr_partition_lba = 0;
  uint32_t              i, j, shrink_vol_no = 1 << 30, idx = 0;
  bool                  need_diskpart_run = false, last_part_is_ntfs = false;
  win_volume_ptr        wvp = NULL, shrink_volume = NULL;
  gpt_ptr               new_g = NULL, new_g2 = NULL;
  uint32_t              winboot_part_idx, winboot_numlbas, winre_numlbas;
  uint64_t              winboot_startlba, free_lba_start = (uint64_t)-1, free_lba_num = (uint64_t)-1, winre_startlba = 0, winboot_size = 0;
  GUID                  guid_current, guid_disk, guid_efi_partition, guid_winsys_partition, guid_winre_partition, guid_msr_partition;
  GUID                  guid_mbr_disk, guid_mbr_efi, guid_mbr_winsys, guid_mbr_winre;
  uint32_t              vol_efi_index = 0, vol_winsys_index = 0, vol_winre_index = 0;
  mbr_part_sector_ptr   mpsp;
  uint8_t               part_guid[16];
  uint64_t              part_attributes;
  win_volume_ptr        this_volume = NULL;
  char                  this_part_name[64];
  disk_map_ptr          dmp = NULL, dmp_run;
  backup_header_ptr     bhp = NULL;
  DISK_HANDLE           h = INVALID_DISK_HANDLE;
  static uint8_t        conversion_data[ ( 3 + 33 + 33 ) * SECTOR_SIZE];
  FILE_HANDLE           fh = INVALID_FILE_HANDLE;
  bcdwmi                bw;
  bcdstore              bs;
  char                 *p;
  bool                  have_winsys_guid = false, have_winre_guid = false;

  memset(&guid_mbr_disk, 0, sizeof(GUID));
  memset(&guid_mbr_efi, 0, sizeof(GUID));
  memset(&guid_mbr_winsys, 0, sizeof(GUID));
  memset(&guid_mbr_winre, 0, sizeof(GUID));

  // 1.) Retrieve drive_ptr
  
  if (NULL == cap->work_disk)
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET ": no working drive available. STOP.\n");
ErrorExit:
    if (NULL != new_g)
      free(new_g);
    if (NULL != new_g2)
      free(new_g2);
    if (NULL != dmp)
      free_disk_map(dmp);
    if (NULL != bhp)
      free_backup_structure(bhp);
    if (INVALID_DISK_HANDLE != h)
      CloseHandle(h);
    if (INVALID_FILE_HANDLE != fh)
      file_close(fh, false);
    if (NULL != dep)
      freeDirEntries(dep);
    return 1;
  }

  if (0 == cap->linux_stick_drive)
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET ": please specify the drive letter of the Linux live stick.\n");
    goto ErrorExit;
  }

  // 2.) Check if MBR structure is clean

  fprintf(stdout, CTRL_CYAN "CHECKING" CTRL_RESET ": Master Boot Record and all partitions clean .............: ");
  fflush(stdout);
  if (NULL == cap->work_disk->mbr_dmp)
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to locate an MBR on the target %s that could be converted to GPT.\n", cap->work_disk->device_file);
    goto ErrorExit;
  }
  fprintf(stdout, CTRL_GREEN "OK" CTRL_RESET "\n");

  // 3.) Check that no GPT is there

  fprintf(stdout, CTRL_CYAN "CHECKING" CTRL_RESET ": GUID Partition Table MUST NOT be there ..................: ");
  fflush(stdout);
  if (NULL != cap->work_disk->gpt_dmp)
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n         The target %s already contains a GUID Partition Table and cannot be converted again.\n", cap->work_disk->device_file);
    goto ErrorExit;
  }
  fprintf(stdout, CTRL_GREEN "OK" CTRL_RESET "\n");

  // 4.) Ensure that logical sector size is 512 bytes (no matter what physical sector size is: 512 or 4096)

  fprintf(stdout, CTRL_CYAN "CHECKING" CTRL_RESET ": Logical sector size has to be 512 (physical can be 4096) : ");
  fflush(stdout);
  if (SECTOR_SIZE != cap->work_disk->logical_sector_size)
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          The target %s uses a logical sector size of %u (not 512). This tool is unable to proceed.\n", cap->work_disk->device_file, cap->work_disk->logical_sector_size);
    goto ErrorExit;
  }
  fprintf(stdout, CTRL_GREEN "OK" CTRL_RESET "\n");

  // 5.) Locate the Windows Recovery Environment (if any)
  
  fprintf(stdout, CTRL_CYAN "CHECKING" CTRL_RESET ": Locate MBR-style Windows Recovery Environment partition  : ");
  fflush(stdout);
  if (!locateWindowsRecoveryEnvironmentPartition(cap->work_disk->mbr, &winre_startlba, &winre_numlbas))
    fprintf(stdout, CTRL_YELLOW "n/a" CTRL_RESET "\n");
  else
    fprintf(stdout, CTRL_GREEN "OK" CTRL_RESET "\n");

  // 6.) Locate the MBR Windows boot partition
  
  fprintf(stdout, CTRL_CYAN "CHECKING" CTRL_RESET ": Locate MBR-style Windows Boot Partition ................ : ");
  fflush(stdout);
  if (!locateMBRWindowsBootPartition(cap->work_disk->mbr,&winboot_part_idx,&winboot_startlba, &winboot_numlbas))
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          The target %s does not have a Windows Boot Partition.\n", cap->work_disk->device_file);
    goto ErrorExit;
  }
  fprintf(stdout, CTRL_GREEN "OK" CTRL_RESET "\n");

  winboot_size = ((uint64_t)winboot_numlbas) << SECTOR_SHIFT;

  format_disk_size(winboot_numlbas << SECTOR_SHIFT, str1, sizeof(str1));
  fprintf(stdout, "          Windows Boot Partition at index %u (MBR), size: %s\n", winboot_part_idx, str1);

  // 7.) ensure that windows boot efi dir is 'there'

  snprintf(buffer, sizeof(buffer), "%c:%s", cap->win_sys_drive, WINDOWS_BOOT_EFI_DIR);

  fprintf(stdout, CTRL_CYAN "CHECKING" CTRL_RESET ": Scanning Windows boot EFI directory on drive %c: .........: ", cap->win_sys_drive);
  fflush(stdout);
  dep = scanFolderRecursively(buffer);
  if (NULL == dep)
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to locate Windows boot EFI directory: %s\n", buffer);
    goto ErrorExit;
  }
  fprintf(stdout, CTRL_GREEN "OK" CTRL_RESET "\n");

  // 8.) estimate the to-be-copied files from windows boot efi dir

  bootEfiSize = estimateFolderSize(dep) + (4 << 20); /* estimate for Boot Configuration Data plus BCD log files plus a little more space*/
  format_64bit(bootEfiSize, str1, sizeof(str1));
  format_disk_size(bootEfiSize, str2, sizeof(str2));

  fprintf(stdout, CTRL_YELLOW "INFO    " CTRL_RESET ": estimated size of EFI partition content is %s byte(s) or approx. %s.\n", str1, str2);

  if (winboot_size < (bootEfiSize + MS_RESERVED_PART_SIZE))
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Windows boot partition is too small to be split into ESP\n");
    fprintf(stdout,                             "          and Microsoft Reserved Partition.\n");

    format_64bit(winboot_size, str1, sizeof(str1));
    fprintf(stdout, CTRL_YELLOW "INFO    " CTRL_RESET ": size of Windows Boot Partition is %s.\n", str1);
    format_64bit(bootEfiSize + MS_RESERVED_PART_SIZE, str1, sizeof(str1));
    fprintf(stdout, CTRL_YELLOW "INFO    " CTRL_RESET ": the above mentioned EFI partition plus 16MB is required: %s.\n", str1);

    goto ErrorExit;
  }

  // 9.) check that the Linux live drive is a Linux live system
  
  fprintf(stdout, CTRL_CYAN "CHECKING" CTRL_RESET ": Ensure that Linux stick contains a live system ..........: ");
  fflush(stdout);

  snprintf(linux_stick, sizeof(linux_stick), "%c:\\syslinux.cfg", cap->linux_stick_drive);
  if (0 != _access(linux_stick, 0))
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          syslinux.cfg not found on the stick. Please check.\n");
    goto ErrorExit;
  }
  snprintf(linux_stick, sizeof(linux_stick), "%c:\\live\\filesystem.squashfs", cap->linux_stick_drive);
  if (0 != _access(linux_stick, 0))
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          live\\filesystem.squashfs not found on the stick. Please check.\n");
    goto ErrorExit;
  }

  fprintf(stdout, CTRL_GREEN "OK" CTRL_RESET "\n");

  // 10.) ensure that conversion directory is not already there

  fprintf(stdout, CTRL_CYAN "CHECKING" CTRL_RESET ": Ensure that conversion folder is not already there ......: ");
  fflush(stdout);

  snprintf(linux_stick, sizeof(linux_stick), "%c:\\" STICK_BASE_PATH, cap->linux_stick_drive);
  if (-1 != _access(linux_stick, 0))
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          %c:\\" STICK_BASE_PATH " folder is already on the live stick. Please remove or backup it.\n", cap->linux_stick_drive);
    goto ErrorExit;
  }

  fprintf(stdout, CTRL_GREEN "OK" CTRL_RESET "\n");

  // 11.) Ensure that the efibootmgr and its shared objects are available for a copy operation
  
  memset(szPartYFilePath, 0, sizeof(szPartYFilePath));
  (void)GetModuleFileNameA(NULL, szPartYFilePath, sizeof(szPartYFilePath));
  p = strrchr(szPartYFilePath, '\\');
  if (NULL != p)
    p[1] = 0;

  fprintf(stdout, CTRL_CYAN "CHECKING" CTRL_RESET ": (Linux) executables and shared objects available ........: ");
  fflush(stdout);

  snprintf(buffer, sizeof(buffer), "%s" WIN_EFIBOOTMGR_EXECUTABLE, szPartYFilePath);
  if (0 != _access(buffer, 0))
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Linux file %s not available.\n", buffer);
    goto ErrorExit;
  }

  snprintf(buffer, sizeof(buffer), "%s" WIN_EFIVAR_EXECUTABLE, szPartYFilePath);
  if (0 != _access(buffer, 0))
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Linux file %s not available.\n", buffer);
    goto ErrorExit;
  }

  snprintf(buffer, sizeof(buffer), "%s" WIN_EFIBOOTMGR_SO_1, szPartYFilePath);
  if (0 != _access(buffer, 0))
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Linux file %s not available.\n", buffer);
    goto ErrorExit;
  }

  snprintf(buffer, sizeof(buffer), "%s" WIN_EFIBOOTMGR_SO_2, szPartYFilePath);
  if (0 != _access(buffer, 0))
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Linux file %s not available.\n", buffer);
    goto ErrorExit;
  }

  snprintf(buffer, sizeof(buffer), "%s" WIN_PARTY_EXECUTABLE_LINUX, szPartYFilePath);
  if (0 != _access(buffer, 0))
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Linux file %s not available.\n", buffer);
    goto ErrorExit;
  }

  fprintf(stdout, CTRL_GREEN "OK" CTRL_RESET "\n");

  // 11.) 'simulate' the final GPT disk layout and ensure that there is enough free space on the drive
  
  fprintf(stdout, CTRL_CYAN "CHECKING" CTRL_RESET ": Computing disk space requirements (req. by conversion) ..: ");
  fflush(stdout);

  if (S_OK != CoCreateGuid(&guid_current))
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to generate new GUID (using COM-API).\n");
    goto ErrorExit;
  }

  memcpy(&guid_disk, &guid_current, sizeof(GUID));
  guid_current.Data1++;
  memset(&guid_efi_partition, 0, sizeof(GUID));
  memset(&guid_winsys_partition, 0, sizeof(GUID));
  memset(&guid_winre_partition, 0, sizeof(GUID));
  memset(&guid_msr_partition, 0, sizeof(GUID));

  if (!check_lba_range_is_free(cap->work_disk->mbr_dmp, 1, 33))
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          LBA sectors 1 thru 33 not available (free).\n");
    fprintf(stdout, CTRL_YELLOW "INFO    " CTRL_RESET ": Please use an external tool to free the first 1MB of the device.\n");
    goto ErrorExit;
  }

  // 12.) check that there is space left for the backup GPT
  
  if (!check_lba_range_is_free(cap->work_disk->mbr_dmp, cap->work_disk->device_sectors - 33, 33))
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          LBA sectors n-33 thru n-1 not available (free) - required to store backup GPT.\n");
    fprintf(stdout, CTRL_YELLOW "INFO    " CTRL_RESET ": Please use an external tool to free the last 1MB of the device.\n");
    goto ErrorExit;
  }

  // 13.) Create new GUID Partition Table in memory

  new_g = (gpt_ptr)malloc(sizeof(gpt));
  new_g2 = (gpt_ptr)malloc(sizeof(gpt));
  if (NULL == new_g || NULL == new_g2)
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Insufficient memory available (internal error).\n");
    goto ErrorExit;
  }

  memset(new_g, 0, sizeof(gpt));

  new_g->header.revision = 0x00010000;
  new_g->header.header_size = 0x5C;
  new_g->header.current_lba = 1;
  new_g->header.backup_lba = cap->work_disk->device_sectors - 1;
  new_g->header.first_usable_lba = 34;
  new_g->header.last_usable_lba = cap->work_disk->device_sectors - 34;
  new_g->header.starting_lba_part_entries = 2; // primary GPT
  new_g->header.size_of_part_entry = 128;

  // create GUID for the disk (the original GUID of the disk if it is MBR-partitioned is: {signature-0000-0000-0000-000000000000})
  // signature is the 32bit value in the MBR at offset 0x1B8

  memcpy(new_g->header.disk_guid, &guid_disk, sizeof(new_g->header.disk_guid)); // always use a brand-new GUID

  if (0 == cap->work_disk->mbr->disk_signature)
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          MBR boot signature is zero - unable to proceed.\n");
    goto ErrorExit;
  }

  guid_mbr_disk.Data1 = cap->work_disk->mbr->disk_signature;

  // first partition on disk is EFI System Partition (ESP)
  
  efi_partition_lba = winboot_startlba;
  msr_partition_lba = winboot_startlba + winboot_numlbas - (MS_RESERVED_PART_SIZE >> SECTOR_SHIFT);
  msr_partition_lba &= ~2047; // align to 1MB boundary

  memcpy(new_g->entries[new_g->header.number_of_part_entries].type_guid, guid_efi_system_partition, 16);

  this_volume = findWindowsVolumeByPartitionStartLBA(cap->wvp, cap->win_device_no, winboot_startlba);
  if (NULL == this_volume)
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to retrieve the Boot Partition GUID for the new EFI partition.\n");
    goto ErrorExit;
  }

  //parse_guid((uint8_t*)&guid_mbr_efi, &this_volume->volume_guid[1], false/*mixed endian*/);
  // this was wrong: it turned out that not the volume GUID but the real MBR partition GUID has to be used here
  // or we cannot patch the BCD correctly...
  if (!disk_mbr_get_partition_guid(cap->work_disk, winboot_startlba, winboot_numlbas, &guid_mbr_efi))
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to retrieve the Boot Partition GUID for the new EFI partition.\n");
    goto ErrorExit;
  }

  vol_efi_index = this_volume->volume_no;
  vol_efi_index = this_volume->volume_no;

  memcpy(&guid_efi_partition, &guid_current, sizeof(GUID));
  guid_current.Data1++;
  memcpy(new_g->entries[new_g->header.number_of_part_entries].partition_guid, &guid_efi_partition, 16);

  new_g->entries[new_g->header.number_of_part_entries].part_start_lba = efi_partition_lba;
  new_g->entries[new_g->header.number_of_part_entries].part_end_lba = msr_partition_lba - 1;
  new_g->entries[new_g->header.number_of_part_entries].attributes = GPT_ATTR_DO_NOT_MOUNT | GPT_ATTR_LEGACY_BIOS_BOOT;
  new_g->entries[new_g->header.number_of_part_entries].fs_type = FSYS_WIN_FAT32;
  setGPTPartitionName(new_g->entries[new_g->header.number_of_part_entries].part_name, new_g->entries[new_g->header.number_of_part_entries].part_name_utf8_oem, "EFI System Partition (ESP)");

  new_g->header.number_of_part_entries++;

  // second partition on disk is Microsoft Reserved Partition (16 MB)
  
  memcpy(new_g->entries[new_g->header.number_of_part_entries].type_guid, guid_microsoft_reserved, 16);

  memcpy(&guid_msr_partition, &guid_current, sizeof(GUID));
  guid_current.Data1++;
  memcpy(new_g->entries[new_g->header.number_of_part_entries].partition_guid, &guid_msr_partition, 16); // always new GUID for Microsoft Reserved

#if 0
  if (S_OK != CoCreateGuid(&winguid))
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to generate new GUID (using COM-API).\n");
    goto ErrorExit;
  }
  memcpy(new_g->entries[new_g->header.number_of_part_entries].partition_guid, &winguid, 16); // always new GUID for Microsoft Reserved
#endif
  new_g->entries[new_g->header.number_of_part_entries].part_start_lba = msr_partition_lba;
  new_g->entries[new_g->header.number_of_part_entries].part_end_lba = msr_partition_lba + (MS_RESERVED_PART_SIZE >> SECTOR_SHIFT) - 1;
  new_g->entries[new_g->header.number_of_part_entries].attributes = 0; // GPT_ATTR_DO_NOT_MOUNT;
  new_g->entries[new_g->header.number_of_part_entries].fs_type = FSYS_UNKNOWN;
  setGPTPartitionName(new_g->entries[new_g->header.number_of_part_entries].part_name, new_g->entries[new_g->header.number_of_part_entries].part_name_utf8_oem, "Microsoft Reserved Partition (MSR)");

  new_g->header.number_of_part_entries++;

#if 0
  // very important: During the testing, it turned out that MS Windows also counts the previously added Microsoft Reserved Partition
  // as a \Device\HarddiskVolumeX (without drive letter) although this partition is neither formatted as NTFS, FAT or exFAT.
  // Thank you, MS... (this demolished our Boot Configuration Data)
  
  new_volume_msr = (win_volume_ptr)malloc(sizeof(win_volume));
  if (NULL == new_volume_msr)
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to allocate new windows volume entry for the MSR partition.\n");
    goto ErrorExit;
  }

  memset(new_volume_msr, 0, sizeof(win_volume));

  format_guid(msr_guid, new_g->entries[new_g->header.number_of_part_entries - 1].partition_guid, false/*mixed endianess*/);
  snprintf(new_volume_msr->volume_name, sizeof(new_volume_msr->volume_name), "\\\\?\\Volume{%s}\\", msr_guid);
  // device_name "\Device\HarddiskVolumeX" -> see below
  snprintf(new_volume_msr->volume_guid, sizeof(new_volume_msr->volume_guid), "{%s}", msr_guid);
  new_volume_msr->start_lba = new_g->entries[new_g->header.number_of_part_entries - 1].part_start_lba;
  new_volume_msr->num_lbas = new_g->entries[new_g->header.number_of_part_entries - 1].part_end_lba - new_g->entries[new_g->header.number_of_part_entries - 1].part_start_lba + 1;
  new_volume_msr->disk_number = cap->win_device_no;
  // volume_no -> see below
  new_volume_msr->num_extents = 1;
  // no drive_letter!!!
#endif

  // sanity check
  
  if (NULL == this_volume) // we have removed the option to create a new EFI partition (we only support the conversion of the boot partition!)
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to retrieve the Boot Partition GUID for the new EFI partition. PLEASE REPORT THIS BUG.\n");
    goto ErrorExit;
  }

#if 0
  // sanity check #2
  
  if (NULL == this_volume->next)
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to modify the Windows list of HarddiskVolumes. There seems to be a gap. Refusing to proceed.\n");
    goto ErrorExit;
  }

  new_volume_msr->volume_no = this_volume->volume_no + 1;
  snprintf(new_volume_msr->device_name, sizeof(new_volume_msr->device_name), "\\Device\\HarddiskVolume%u", new_volume_msr->volume_no);

  // insert Microsoft Reserved as a 'fake' after this_volume = old boot partition (which becomes EFI partition)
  
  new_volume_msr->next = this_volume->next;
  new_volume_msr->prev = this_volume;

  this_volume->next = new_volume_msr;

  if (NULL != new_volume_msr->next)
    new_volume_msr->next->prev = new_volume_msr;
  // we do not have to care about 'tail' if NULL == new_volume_msr->next because this is not maintained as a pointer!
  
  // all subsequent HarddiskVolumes have to be renumbered (plus 1)

  new_volume_msr = new_volume_msr->next;

  while (NULL != new_volume_msr)
  {
    new_volume_msr->volume_no++;
    snprintf(new_volume_msr->device_name, sizeof(new_volume_msr->device_name), "\\Device\\HarddiskVolume%u", new_volume_msr->volume_no);
    new_volume_msr = new_volume_msr->next;
  }
#endif

  // now add all MBR partition (except for Windows Boot Partition - it is re-used for the ESP!!!)
  
  mpsp = cap->work_disk->mbr;
  while (NULL != mpsp)
  {
    for (i = 0; i < 4; i++)
    {
      if ( (0x00 == mpsp->part_table[i].part_type) || (MBR_IS_EXTENDED_PARTITION(mpsp->part_table[i].part_type)) )
      {
        idx++;
        continue;
      }

      this_volume = findWindowsVolumeByPartitionStartLBA(cap->wvp, cap->win_device_no, mpsp->part_table[i].start_sector);

      if (mpsp == cap->work_disk->mbr && winboot_part_idx == idx) // this is the Windows Boot partition
      {
        if (NULL == this_volume)
        {
          fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Volume pointer of (old) Windows boot partition is NULL. Please report this as a bug.\n");
          goto ErrorExit;
        }

        idx++;
      }
      else // any other MBR partition
      {
        if (!gpt_get_guid_for_mbr_type(mpsp->part_table[i].part_type, part_guid, &part_attributes))
        {
          fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to retrieve GPT partition GUID for MBR partition (no conversion GUID available for this MBR partition).\n");
          goto ErrorExit;
        }

        if (NULL == this_volume) // any other partition (e.g. Linux)
        {
          memcpy(new_g->entries[new_g->header.number_of_part_entries].type_guid, part_guid, 16);

          if (mpsp->part_table[i].fs_type >= FSYS_LINUX_EXT2 && mpsp->part_table[i].fs_type <= FSYS_LINUX_EXT4) // re-use the EXT2, 3, 4 partition UUID as the GPT partition GUID
          {
            new_g->entries[new_g->header.number_of_part_entries].partition_guid[0] = mpsp->part_table[i].uuid[3];
            new_g->entries[new_g->header.number_of_part_entries].partition_guid[1] = mpsp->part_table[i].uuid[2];
            new_g->entries[new_g->header.number_of_part_entries].partition_guid[2] = mpsp->part_table[i].uuid[1];
            new_g->entries[new_g->header.number_of_part_entries].partition_guid[3] = mpsp->part_table[i].uuid[0];

            new_g->entries[new_g->header.number_of_part_entries].partition_guid[4] = mpsp->part_table[i].uuid[5];
            new_g->entries[new_g->header.number_of_part_entries].partition_guid[5] = mpsp->part_table[i].uuid[4];

            new_g->entries[new_g->header.number_of_part_entries].partition_guid[6] = mpsp->part_table[i].uuid[7];
            new_g->entries[new_g->header.number_of_part_entries].partition_guid[7] = mpsp->part_table[i].uuid[6];

            memcpy(&new_g->entries[new_g->header.number_of_part_entries].partition_guid[8], &mpsp->part_table[i].uuid[8], 8);
          }
          else
          {
            memcpy(new_g->entries[new_g->header.number_of_part_entries].partition_guid, &guid_current, 16);
            guid_current.Data1++;
          }

          new_g->entries[new_g->header.number_of_part_entries].part_start_lba = mpsp->part_table[i].start_sector;
          new_g->entries[new_g->header.number_of_part_entries].part_end_lba = new_g->entries[new_g->header.number_of_part_entries].part_start_lba + ((uint64_t)mpsp->part_table[i].num_sectors) - 1;
          new_g->entries[new_g->header.number_of_part_entries].attributes = part_attributes;
          new_g->entries[new_g->header.number_of_part_entries].fs_type = mpsp->part_table[i].fs_type;

          new_g->header.number_of_part_entries++;
        }
        else // this is a Windows volume on the MBR-disk -> convert it
        {
          memcpy(new_g->entries[new_g->header.number_of_part_entries].type_guid, part_guid, 16);

          memcpy(new_g->entries[new_g->header.number_of_part_entries].partition_guid, &guid_current, 16);
          guid_current.Data1++;

          //parse_guid(part_guid, &this_volume->volume_guid[1], false/*mixed endian*/);
          //memcpy(new_g->entries[new_g->header.number_of_part_entries].partition_guid, part_guid, 16); // re-use Windows volume partition GUID

          if (cap->win_sys_drive == this_volume->drive_letter)
          {
            memcpy(&guid_winsys_partition, new_g->entries[new_g->header.number_of_part_entries].partition_guid, 16);
            have_winsys_guid = true;
            //parse_guid((uint8_t*)&guid_mbr_winsys, &this_volume->volume_guid[1], false/*mixed endian*/);
            if (!disk_mbr_get_partition_guid(cap->work_disk, this_volume->start_lba, this_volume->num_lbas, &guid_mbr_winsys))
            {
              fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to retrieve the partition GUID of the Windows system drive partition.\n");
              goto ErrorExit;
            }
            vol_winsys_index = this_volume->volume_no;
          }

          if (mpsp->part_table[i].start_sector == winre_startlba)
          {
            memcpy(&guid_winre_partition, new_g->entries[new_g->header.number_of_part_entries].partition_guid, 16);
            have_winre_guid = true;
            //parse_guid((uint8_t*)&guid_mbr_winre, &this_volume->volume_guid[1], false/*mixed endian*/);
            if (!disk_mbr_get_partition_guid(cap->work_disk, winre_startlba, winre_numlbas, &guid_mbr_winre))
            {
              fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to retrieve the partition GUID of the Windows Recovery Environment partition.\n");
              goto ErrorExit;
            }
            vol_winre_index = this_volume->volume_no;
          }

          new_g->entries[new_g->header.number_of_part_entries].part_start_lba = mpsp->part_table[i].start_sector;
          new_g->entries[new_g->header.number_of_part_entries].part_end_lba = new_g->entries[new_g->header.number_of_part_entries].part_start_lba + ((uint64_t)mpsp->part_table[i].num_sectors) - 1;
          new_g->entries[new_g->header.number_of_part_entries].attributes = ((0x00==this_volume->drive_letter) ? GPT_ATTR_DO_NOT_MOUNT : 0) | part_attributes;
          new_g->entries[new_g->header.number_of_part_entries].fs_type = mpsp->part_table[i].fs_type;
          if (0x00 == this_volume->drive_letter)
            snprintf(this_part_name, sizeof(this_part_name), "Windows partition");
          else
            snprintf(this_part_name, sizeof(this_part_name), "Windows drive %c:", this_volume->drive_letter);

          setGPTPartitionName(new_g->entries[new_g->header.number_of_part_entries].part_name, new_g->entries[new_g->header.number_of_part_entries].part_name_utf8_oem, this_part_name);

          new_g->header.number_of_part_entries++;
        }
      }

      idx++;
    } // for i 0..3

    mpsp = mpsp->next;
  }

  // 14.) perform sanity checks regarding partition GUIDs
  
  for (i = 0; i < new_g->header.number_of_part_entries; i++)
  {
    // partition GUID MUST NOT match disk GUID
    
    if (!memcmp(new_g->header.disk_guid, new_g->entries[i].partition_guid, 16))
    {
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Partition GUID matches disk GUID. Please re-run this tool.\n");
      goto ErrorExit;
    }

    // the partition GUIDs must be unique among each other
    
    for (j = 0; j < new_g->header.number_of_part_entries; j++)
    {
      if (i != j)
      {
        if (!memcmp(new_g->entries[j].partition_guid, new_g->entries[i].partition_guid, 16))
        {
          fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          At least two partition GUIDs match. Please re-run this tool.\n");
          goto ErrorExit;
        }
      }
    }
  }

  // 15.) create a disk map and ensure that all areas fit in the disk...
  
  memcpy(new_g2, new_g, sizeof(gpt));
  new_g2->header.current_lba = new_g->header.backup_lba;
  new_g2->header.backup_lba = new_g->header.current_lba;

  new_g->header.number_of_part_entries = 128;
  new_g2->header.number_of_part_entries = 128;

  new_g2->header.starting_lba_part_entries = cap->work_disk->device_sectors - 33;

  dmp = partition_create_disk_map_gpt(new_g, new_g2);
  if (NULL == dmp)
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to create new disk map.\n");
    goto ErrorExit;
  }

  dmp = sort_and_complete_disk_map(dmp, cap->work_disk->device_sectors);
  if (NULL == dmp)
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to create new (sorted) disk map.\n");
    goto ErrorExit;
  }

  dmp_run = dmp;
  while (NULL != dmp_run)
  {
    if (dmp_run->end_lba >= cap->work_disk->device_sectors)
    {
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Computed new disk map contains error(s). Please report this bug.\n");
      goto ErrorExit;
    }

    if (NULL != dmp_run->prev)
    {
      if (dmp_run->prev->end_lba >= dmp_run->start_lba)
      {
        fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Computed new disk map contains error(s). Please report this bug.\n");
        goto ErrorExit;
      }
    }

    dmp_run = dmp_run->next;
  }

  fprintf(stdout, CTRL_GREEN "OK" CTRL_RESET "\n");

  fprintf(stdout, CTRL_CYAN "WORKING" CTRL_RESET " : Ensure that we have the windows system drive GUID .......: ");
  fflush(stdout);
  if (!have_winsys_guid)
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to locate Windows system drive partition (letter %c:).\n", cap->win_sys_drive);
    goto ErrorExit;
  }
  fprintf(stdout, CTRL_GREEN "OK" CTRL_RESET "\n");

  if (cap->verbose)
  {
    fprintf(stdout, "\n" CTRL_YELLOW "INFO" CTRL_RESET ": [VERBOSE] New GPT will be (CRC32s not valid!):\n");
    partition_dump_temporary_gpt(new_g);
    fprintf(stdout, "\n" CTRL_YELLOW "INFO" CTRL_RESET ": [VERBOSE] New disk map will be:\n");
    disk_dump_map(dmp);
    fprintf(stdout, "\n");
  }

  if (!cap->dryrun) // OK, perform all necessary steps to really prepare the mbr-2-gpt conversion
  {
    snprintf(cap->backup_file, sizeof(cap->backup_file), "%s\\" FILE_BACKUP, linux_stick);

    // i) Create a backup of everything that will be written by the actual conversion (to be executed on a Linux system)
    
    fprintf(stdout, CTRL_CYAN "WORKING" CTRL_RESET " : Creating backup of all sectors to be written ............: ");
    fflush(stdout);

    if (!CreateDirectory(linux_stick, NULL))
    {
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to create directory %s\n", linux_stick);
      goto ErrorExit;
    }

    snprintf(buffer, sizeof(buffer), "%s\\" DIR_WIN_EFI_FILES, linux_stick);
    if (!CreateDirectory(buffer, NULL))
    {
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to create directory %s\n", buffer);
      goto ErrorExit;
    }

    bhp = bootstrap_backup(cap->work_disk->device_sectors);
    if (NULL == bhp)
    {
ErrorExit2:
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to create backup file %s\n",cap->backup_file);
      goto ErrorExit;
    }

    // add MBR (and for failsafe reasons all extended partition sectors)
    
    mpsp = cap->work_disk->mbr;

    while (NULL != mpsp)
    {
      if (!add_backup_record(bhp, mpsp->sp->lba, 1))
        goto ErrorExit2;
      mpsp = mpsp->next;
    }

    // add the primary GPT
    
    if (!add_backup_record(bhp, 1, 33))
      goto ErrorExit2;

    // add the backup GPT

    if (!add_backup_record(bhp, new_g2->header.starting_lba_part_entries, 33))
      goto ErrorExit2;

    // add the entire old (MBR) Boot Partition as well
    
    if (!add_backup_record(bhp, winboot_startlba, winboot_numlbas))
      goto ErrorExit2;

    // i) create the backup

    h = disk_open_device(cap->work_disk->device_file, false/*read-only*/);
    if (INVALID_DISK_HANDLE == h)
      goto ErrorExit2;

    snprintf(message,sizeof(message), CTRL_CYAN "WORKING" CTRL_RESET " : Creating backup of all sectors to be written ............: ");

    if (!create_backup_file(cap->work_disk, bhp, h, cap->backup_file, message))
      goto ErrorExit2;

    fprintf(stdout, CTRL_GREEN "OK" CTRL_RESET "\n");

    // ii) Verify the backup

    fprintf(stdout, CTRL_CYAN "WORKING" CTRL_RESET " : Verifying just created backup ...........................: ");
    fflush(stdout);

    snprintf(message, sizeof(message), CTRL_CYAN "WORKING" CTRL_RESET " : Verifying just created backup ...........................: ");

    if (!check_backup_file(cap->work_disk, h, cap->backup_file, message))
    {
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to verify backup file.\n");
      goto ErrorExit;
    }

    fprintf(stdout, CTRL_GREEN "OK" CTRL_RESET "\n");

    // iii) create the data file; it contains the new MBR, followed by the primary GPT, followed by the backup GPT (exactly 3+33+33 = 69 sectors)

    fprintf(stdout, CTRL_CYAN "WORKING" CTRL_RESET " : Creating conversion data file ...........................: ");
    fflush(stdout);

    snprintf(convert_file, sizeof(convert_file), "%s\\" FILE_CONVERSION, linux_stick);

    fh = file_open(convert_file, false/*reading and writing*/);
    if (INVALID_FILE_HANDLE == fh)
    {
ErrorExit3:
      if (INVALID_FILE_HANDLE != fh)
        file_close(fh, false);
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to create conversion data file %s\n",convert_file);
      goto ErrorExit;
    }

    // copy the original MBR to the head of the conversion file and add some information to be verified to it
    
    memcpy(conversion_data, cap->work_disk->mbr->sp->data, SECTOR_SIZE);
    WRITE_BIG_ENDIAN64(conversion_data,0x0000,cap->work_disk->device_size);
    WRITE_BIG_ENDIAN32(conversion_data,0x0008,cap->work_disk->logical_sector_size);
    WRITE_BIG_ENDIAN32(conversion_data,0x000C,cap->work_disk->physical_sector_size);

    // establish a protective MBR

    create_protective_mbr(cap->work_disk->device_sectors, &conversion_data[SECTOR_SIZE]);

    if (cap->verbose)
    {
      fprintf(stdout, CTRL_YELLOW "[VERBOSE]" CTRL_RESET " Hexdump of new MBR:\n");
      hexdump(&conversion_data[SECTOR_SIZE], SECTOR_SIZE, 0);
    }

    // Create the primary and backup GPTs in the conversion file

    gpt_create_table(&conversion_data[2 * SECTOR_SIZE], new_g, true /*primary*/); // primary GPT offset is sector #2 in the conversion file
    gpt_create_table(&conversion_data[35 * SECTOR_SIZE], new_g2, false /*secondary*/); // backup GPT offset is sector #35 (2+33) in the conversion file

    // Add all GUIDs, which have to be patched later on (Linux run)

    memcpy(&conversion_data[(2 + 33 + 33) * SECTOR_SIZE +   0], &guid_mbr_disk, 16);
    memcpy(&conversion_data[(2 + 33 + 33) * SECTOR_SIZE +  16], &guid_disk, 16);

    memcpy(&conversion_data[(2 + 33 + 33) * SECTOR_SIZE +  32], &guid_mbr_efi, 16);
    memcpy(&conversion_data[(2 + 33 + 33) * SECTOR_SIZE +  48], &guid_efi_partition, 16);

    memcpy(&conversion_data[(2 + 33 + 33) * SECTOR_SIZE +  64], &guid_mbr_winsys, 16);
    memcpy(&conversion_data[(2 + 33 + 33) * SECTOR_SIZE +  80], &guid_winsys_partition, 16);

    memcpy(&conversion_data[(2 + 33 + 33) * SECTOR_SIZE +  96], &guid_mbr_winre, 16);
    memcpy(&conversion_data[(2 + 33 + 33) * SECTOR_SIZE + 112], &guid_winre_partition, 16);

    if (!file_write(fh, conversion_data, sizeof(conversion_data)))
      goto ErrorExit3;

    file_close(fh,true), fh = INVALID_FILE_HANDLE;

    fprintf(stdout, CTRL_GREEN "OK" CTRL_RESET "\n");

    // iv) create Boot Configuration Data
    
    if (cap->verbose)
    {
      fprintf(stdout, CTRL_YELLOW "[VERBOSE]" CTRL_RESET ": partition indexes and GUIDs:\n");
      
      format_guid(buffer, (const uint8_t*)&guid_mbr_disk, false);
      fprintf(stdout, "old (MBR) disk GUID is {%s}\n", buffer);
      format_guid(buffer, (const uint8_t*)&guid_disk, false);
      fprintf(stdout, "new (GPT) disk GUID is {%s}\n\n", buffer);

      format_guid(buffer, (const uint8_t*)&guid_mbr_efi, false);
      fprintf(stdout, "old (MBR) EFI partition GUID is {%s}, volume index is %u\n", buffer, vol_efi_index);
      format_guid(buffer, (const uint8_t*)&guid_efi_partition, false);
      fprintf(stdout, "new (GPT) EFI partition GUID is {%s}\n\n", buffer);

      format_guid(buffer, (const uint8_t*)&guid_mbr_winsys, false);
      fprintf(stdout, "old (MBR) Windows system drive %c: partition GUID is {%s}, volume index is %u\n", cap->win_sys_drive, buffer, vol_winsys_index);
      format_guid(buffer, (const uint8_t*)&guid_winsys_partition, false);
      fprintf(stdout, "new (GPT) Windows system drive %c: partition GUID is {%s}\n\n", cap->win_sys_drive, buffer);

      format_guid(buffer, (const uint8_t*)&guid_mbr_winre, false);
      fprintf(stdout, "old (MBR) Windows RE partition GUID is {%s}, volume index is %u\n", buffer, vol_winre_index);
      format_guid(buffer, (const uint8_t*)&guid_winre_partition, false);
      fprintf(stdout, "new (GPT) Windows RE partition GUID is {%s}\n\n", buffer);

      format_guid(buffer, (const uint8_t*)&guid_msr_partition, false);
      fprintf(stdout, "new (GPT) Microsoft Reserved partition GUID is {%s}\n\n", buffer);
    }

    fprintf(stdout, CTRL_CYAN "WORKING" CTRL_RESET " : Create Boot Configuration Data (BCD) ....................: ");
    fflush(stdout);

    if (!bcd_connect(&bw))
    {
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n           Failed to connect to the BCD store WMI provider.\n");
      goto ErrorExit;
    }

    snprintf(bcd_file, sizeof(bcd_file), "%s\\" FILE_BCD, linux_stick);

    if (!bcd_createstore(&bw, bcd_file, &bs))
    {
      bcd_disconnect(&bw);
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to create BCD file %s\n", bcd_file);
      goto ErrorExit;
    }

    if (!bcd_create_objects_and_entries(&bs, vol_efi_index, vol_winsys_index, cap->win_sys_drive, vol_winre_index, cap->locale))
    {
      bcd_closestore(&bs);
      bcd_disconnect(&bw);
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to create objects and/or elements in BCD file %s\n", bcd_file);
      goto ErrorExit;
    }

    bcd_closestore(&bs);
    bcd_disconnect(&bw);

    fprintf(stdout, CTRL_GREEN "OK" CTRL_RESET "\n");

    fprintf(stdout, CTRL_CYAN "WORKING" CTRL_RESET " : Copy all Windows EFI files to the Linux Live stick ......: ");
    fflush(stdout);

    snprintf(buffer, sizeof(buffer), "%c:%s", cap->win_sys_drive, WINDOWS_BOOT_EFI_DIR);
    snprintf(buffer2, sizeof(buffer2), "%s\\" DIR_WIN_EFI_FILES, linux_stick);

    if (!copy_full_tree(dep->child, buffer, buffer2))
    {
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to copy Windows EFI files from %s to %s\n", buffer, buffer2);
      goto ErrorExit;
    }

    fprintf(stdout, CTRL_GREEN "OK" CTRL_RESET "\n");

    fprintf(stdout, CTRL_CYAN "WORKING" CTRL_RESET " : Copy all Linux EFI executables to the Linux Live stick ..: ");
    fflush(stdout);

    snprintf(buffer, sizeof(buffer), "%s" WIN_EFIBOOTMGR_EXECUTABLE, szPartYFilePath);
    snprintf(buffer2, sizeof(buffer2), "%s\\" WIN_EFIBOOTMGR_EXECUTABLE, linux_stick);

    if (0 != strcmp(buffer, buffer2))
    {
      if (!file_copy(buffer, buffer2))
      {
EExit:
        fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET "\n          Failed to copy %s to %s\n", buffer, buffer2);
        goto ErrorExit;
      }
      snprintf(buffer, sizeof(buffer), "%s" WIN_EFIVAR_EXECUTABLE, szPartYFilePath);
      snprintf(buffer2, sizeof(buffer2), "%s\\" WIN_EFIVAR_EXECUTABLE, linux_stick);
      if (!file_copy(buffer, buffer2))
        goto EExit;
      snprintf(buffer, sizeof(buffer), "%s" WIN_EFIBOOTMGR_SO_1, szPartYFilePath);
      snprintf(buffer2, sizeof(buffer2), "%s\\" WIN_EFIBOOTMGR_SO_1, linux_stick);
      if (!file_copy(buffer, buffer2))
        goto EExit;
      snprintf(buffer, sizeof(buffer), "%s" WIN_EFIBOOTMGR_SO_2, szPartYFilePath);
      snprintf(buffer2, sizeof(buffer2), "%s\\" WIN_EFIBOOTMGR_SO_2, linux_stick);
      if (!file_copy(buffer, buffer2))
        goto EExit;
      snprintf(buffer, sizeof(buffer), "%s" WIN_PARTY_EXECUTABLE_LINUX, szPartYFilePath);
      snprintf(buffer2, sizeof(buffer2), "%s\\" WIN_PARTY_EXECUTABLE_LINUX, linux_stick);
      if (!file_copy(buffer, buffer2))
        goto EExit;
    }

    fprintf(stdout, CTRL_GREEN "OK" CTRL_RESET "\n");

    fprintf(stdout, "\n" CTRL_GREEN "GOOD" CTRL_RESET ": You are ready to go!\n");
    fprintf(stdout, "      Please reboot your machine, switch to UEFI boot mode in the EFI\n");
    fprintf(stdout, "      firmware and boot into the Live Linux system.\n");
    fprintf(stdout, "      Execute the Linux version from /run/live/medium in a root prompt.\n");
  }
  else
  {
    fprintf(stdout, "\n" CTRL_CYAN "HINT" CTRL_RESET ": If the selected device is an SSD and you have established\n");
    fprintf(stdout, "      'over provisioning', then you should TEMPORARILY disable it\n");
    fprintf(stdout, "       (e.g. using 'Samsung Magician' or similar tool).\n");

    fprintf(stdout, "\n" CTRL_GREEN "GOOD" CTRL_RESET ": You are ready to go!\n");
  }

  free(new_g);
  free(new_g2);
  free_disk_map(dmp);

  if (NULL != bhp)
    free_backup_structure(bhp);
  if (INVALID_DISK_HANDLE != h)
    CloseHandle(h);

  freeDirEntries(dep);

  return 0;
#endif
}

#endif // _WINDOWS
