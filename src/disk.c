/**
 * @file   disk.c
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @brief  implementation of functions that manage
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

#include <part-y.h>

const uint8_t guid_empty_partition[16]      = { 0x00,0x00,0x00,0x00, 0x00,0x00, 0x00,0x00, 0x00,0x00, 0x00,0x00,0x00,0x00,0x00,0x00 }; // endianess does not matter here

#ifdef _WINDOWS

static void enumAllPhysicaldrives(disk_ptr *head, disk_ptr *tail, uint32_t *total)
{
  HKEY                    hKey = NULL;
  DWORD                   dwType, dwData, dwDataSize;
  uint32_t                i, numDrives;
  char                    szKey[16], szValue[512], * p, * p2, * p3;
  disk_ptr                item;
  DISK_HANDLE             h;

  if (NULL == total)
    return;

  if (NULL == head || NULL == tail)
    return;

  *total = 0;

  if (ERROR_SUCCESS != RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Services\\disk\\Enum", 0, KEY_READ, &hKey))
    return;

  dwDataSize = sizeof(DWORD);
  if (ERROR_SUCCESS != RegQueryValueEx(hKey, "Count", NULL, &dwType, (LPBYTE)&dwData, (LPDWORD)&dwDataSize))
  {
ErrorExit:
    RegCloseKey(hKey);
    return;
  }
  if (REG_DWORD != dwType)
    goto ErrorExit;

  numDrives = (uint32_t)dwData;
  if (0 == numDrives)
    goto ErrorExit;

  for (i = 0; i < numDrives; i++)
  {
    item = (disk_ptr)malloc(sizeof(disk));
    if (NULL == item)
      goto ErrorExit;
    memset(item, 0, sizeof(disk));

    item->device_no = i;
    snprintf(item->device_file, sizeof(item->device_file), "\\\\.\\PhysicalDrive%u", i);

    snprintf(szKey, sizeof(szKey), "%u", i);

    memset(szValue, 0, sizeof(szValue));
    dwDataSize = sizeof(szValue) - 1;
    if ((ERROR_SUCCESS == RegQueryValueEx(hKey, szKey, NULL, &dwType, (LPBYTE)szValue, &dwDataSize)) && (REG_SZ == dwType))
    {
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
               
            strncpy(item->product, p2, sizeof(item->product) - 1);
            strncpy(item->vendor, p, sizeof(item->vendor) - 1);
          }
        }
      }
    }

    item->logical_sector_size = SECTOR_SIZE;
    item->physical_sector_size = SECTOR_SIZE;

    h = disk_open_device(item->device_file, false/*read-only*/);
    if (INVALID_DISK_HANDLE == h)
    {
      item->flags |= DISK_FLAG_READ_ACCESS_ERROR;
    }
    else
    {
      DWORD             length_needed = 32768, check_size;
      uint8_t          *drive_layout = (uint8_t*)malloc(length_needed);
      
      item->device_sectors = disk_get_size(item->device_file, h, &item->logical_sector_size, &item->physical_sector_size);
      item->device_size = item->device_sectors << SECTOR_SHIFT;

      if (NULL != drive_layout)
      {
        if (DeviceIoControl(h, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, NULL, 0, drive_layout, length_needed, &length_needed, (LPOVERLAPPED)NULL))
        {
          PDRIVE_LAYOUT_INFORMATION_EX dlix = (PDRIVE_LAYOUT_INFORMATION_EX)drive_layout;

          check_size = sizeof(DRIVE_LAYOUT_INFORMATION_EX) + (dlix->PartitionCount - 1) * sizeof(PARTITION_INFORMATION_EX);

          if (check_size == length_needed)
          {
            item->win_drive_layout = (PDRIVE_LAYOUT_INFORMATION_EX)malloc(length_needed);
            if (NULL != item->win_drive_layout)
              memcpy(item->win_drive_layout, drive_layout, length_needed);
          }
        }
        free(drive_layout);
      }

      disk_close_device(h);
    }

    item->prev = *tail;
    if (NULL == *tail)
      *head = item;
    else
      (*tail)->next = item;
    *tail = item;
    
    (*total)++;

  } // of for i

  RegCloseKey(hKey);
}

#else // LINUX

static const char linux_disk_prefixes[][8] = { "hd", "sd", "nvm" };

static void enumAllPhysicaldrives(disk_ptr* head, disk_ptr* tail, uint32_t* total)
{
  DIR                      *dir;
  struct dirent            *ent;
  size_t                    i, l, el;
  disk_ptr                  item;
  DISK_HANDLE               h;

  if (NULL == total)
    return;

  if (NULL == head || NULL == tail)
    return;

  *total = 0;

  dir = opendir("/sys/block");
  if (NULL == dir)
    return;

  while (NULL != (ent = readdir(dir)))
  {
    el = (size_t)strlen(ent->d_name);

    if ('.' == ent->d_name[0])
      continue; // also '.' and '..'

    for (i = 0; i < sizeof(linux_disk_prefixes) / sizeof(linux_disk_prefixes[0]); i++)
    {
      l = (size_t)strlen(linux_disk_prefixes[i]);
        if ((el > l) && (!memcmp(ent->d_name, linux_disk_prefixes[i], l)))
        {
          item = (disk_ptr)malloc(sizeof(disk));
            if (NULL == item)
            {
              closedir(dir);
                return;
            }
          memset(item, 0, sizeof(disk));

          snprintf(item->device_file, sizeof(item->device_file), "/dev/%s", ent->d_name);

          item->logical_sector_size = SECTOR_SIZE;
          item->physical_sector_size = SECTOR_SIZE;

          h = disk_open_device(item->device_file, false/*read-only*/);
          if (INVALID_DISK_HANDLE == h)
          {
            item->flags |= DISK_FLAG_READ_ACCESS_ERROR;
          }
          else
          {
            item->device_sectors = disk_get_size(item->device_file, h, &item->logical_sector_size, &item->physical_sector_size);
            item->device_size = item->device_sectors << SECTOR_SHIFT;
            disk_close_device(h);
          }

          item->prev = *tail;
          if (NULL == *tail)
            *head = item;
          else
            (*tail)->next = item;
          *tail = item;

          (*total)++;

          break;
        }
    }
  }

  closedir(dir);
}

#endif // !_WINDOWS

void disk_dump_info(disk_ptr dp)
{
  char                size_str[32], size_str2[32];

  fprintf(stdout, CTRL_YELLOW "INFO" CTRL_RESET ": device file is %s\n", dp->device_file);
  fprintf(stdout, CTRL_YELLOW "INFO" CTRL_RESET ": device file is a physical disk: %s\n", dp->flags & DISK_FLAG_NOT_DEVICE_BUT_FILE ? CTRL_RED "no" CTRL_RESET : CTRL_GREEN "yes" CTRL_RESET);
#ifdef _WINDOWS  
  if (!(dp->flags & DISK_FLAG_NOT_DEVICE_BUT_FILE))
    fprintf(stdout, CTRL_YELLOW "INFO" CTRL_RESET ": vendor is '%s', product is '%s'\n", dp->vendor, dp->product);
#endif

  format_disk_size(dp->device_size, size_str, sizeof(size_str));
  format_64bit(dp->device_sectors, size_str2, sizeof(size_str2));
  fprintf(stdout, CTRL_YELLOW "INFO" CTRL_RESET ": device size is %s (%s sectors)\n", size_str, size_str2);
  fprintf(stdout, CTRL_YELLOW "INFO" CTRL_RESET ": physical sector size is %u, logical sector size is %u\n", dp->physical_sector_size, dp->logical_sector_size);
  fprintf(stdout, CTRL_YELLOW "INFO" CTRL_RESET ": MBR partition table: %s; GUID partition table: %s\n", 
    dp->flags & DISK_FLAG_HAS_MBR ? CTRL_GREEN "yes" CTRL_RESET : CTRL_RED "no" CTRL_RESET, 
    dp->flags & DISK_FLAG_HAS_GPT ? CTRL_GREEN "yes" CTRL_RESET : CTRL_RED "no" CTRL_RESET);

  if (dp->flags & DISK_FLAG_HAS_MBR)
  {
    if (dp->flags & DISK_FLAG_MBR_IS_PROTECTIVE)
    {
      if (0==(dp->flags & DISK_FLAG_HAS_GPT))
        fprintf(stdout, CTRL_CYAN "STATUS" CTRL_RESET ": PROTECTIVE MBR exists but" CTRL_RED " no primary GPT found" CTRL_RESET ".\n");
      else
        fprintf(stdout, CTRL_CYAN "STATUS" CTRL_RESET ": PROTECTIVE MBR exists.\n");
    }
    else
      fprintf(stdout, CTRL_CYAN "STATUS" CTRL_RESET ": MBR exists and is " CTRL_GREEN "HEALTHY" CTRL_RESET ".\n");
  }

  if (dp->primary_gpt_exists)
  {
    if (dp->primary_gpt_corrupt)
      fprintf(stdout, CTRL_CYAN "STATUS" CTRL_RESET ": primary GPT exists" CTRL_RED " but is corrupt" CTRL_RESET ".\n");
    else
      fprintf(stdout, CTRL_CYAN "STATUS" CTRL_RESET ": primary GPT exists\n");
  }

  if (dp->backup_gpt_exists)
  {
    if (dp->backup_gpt_corrupt)
      fprintf(stdout, CTRL_CYAN "STATUS" CTRL_RESET ": secondary/backup GPT exists" CTRL_RED " but is corrupt" CTRL_RESET ".\n");
    else
      fprintf(stdout, CTRL_CYAN "STATUS" CTRL_RESET ": secondary/backup GPT exists\n");
  }

  if (dp->primary_gpt_exists && dp->backup_gpt_exists && dp->gpts_mismatch)
    fprintf(stdout, CTRL_CYAN "STATUS" CTRL_RESET ": primary and secondary/backup GPTs" CTRL_RED " DO mismatch" CTRL_RESET ".\n");

  if (dp->primary_gpt_exists && !dp->primary_gpt_corrupt && dp->backup_gpt_exists && !dp->backup_gpt_corrupt && !dp->gpts_mismatch && NULL != dp->gpt_dmp)
    fprintf(stdout, CTRL_CYAN "STATUS" CTRL_RESET ": primary and secondary/backup GPTs are " CTRL_GREEN "consistent and HEALTHY" CTRL_RESET ".\n");
}

uint32_t disk_explore_all(disk_ptr* head, disk_ptr* tail)
{
  uint32_t                  total_disks;
  disk_ptr                  item;

  if (unlikely(NULL == head || NULL == tail))
    return 0;

  *head = NULL;
  *tail = NULL;

  enumAllPhysicaldrives(head, tail, &total_disks);

  if (0 == total_disks)
    return 0;

  item = *head;
  while (NULL != item)
  {
    disk_scan_partitions(item);
    item = item->next;
  }

  return total_disks;
}

void disk_free_list(disk_ptr head)
{
  disk_ptr          next;

  while (NULL != head)
  {
    next = head->next;
    if (NULL != head->mbr)
      partition_free_mbr_part_sector_list(head->mbr);
    if (NULL != head->gpt1)
      partition_free_gpt(head->gpt1);
    if (NULL != head->gpt2)
      partition_free_gpt(head->gpt2);
    if (NULL != head->mbr_dmp)
      free_disk_map(head->mbr_dmp);
    if (NULL != head->gpt_dmp)
      free_disk_map(head->gpt_dmp);
#ifdef _WINDOWS
    if (NULL != head->win_drive_layout)
      free(head->win_drive_layout);
    if (NULL != head->mbr_partition_info)
      free(head->mbr_partition_info);
#endif
    free(head);
    head = next;
  }
}

#ifdef _WINDOWS

#define O_RDONLY     _O_RDONLY
#define O_WRONLY     _O_WRONLY
#define O_RDWR       _O_RDWR
#define O_APPEND     _O_APPEND
#define O_CREAT      _O_CREAT
#define O_TRUNC      _O_TRUNC
#define O_EXCL       _O_EXCL
#define O_TEXT       _O_TEXT
#define O_BINARY     _O_BINARY
#define O_RAW        _O_BINARY
#define O_TEMPORARY  _O_TEMPORARY
#define O_NOINHERIT  _O_NOINHERIT
#define O_SEQUENTIAL _O_SEQUENTIAL
#define O_RANDOM     _O_RANDOM

DISK_HANDLE disk_open_device(const char* device_file, bool writeAccess )
{
  return writeAccess ? CreateFileA(device_file, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH | FILE_FLAG_NO_BUFFERING, NULL) :
                       CreateFileA(device_file, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_NO_BUFFERING, NULL);
}

void disk_close_device(DISK_HANDLE h)
{
  if (INVALID_DISK_HANDLE != h)
    CloseHandle(h);
}

uint64_t disk_get_size(const char* device_file, DISK_HANDLE h, uint32_t* logical_sector_size, uint32_t* physical_sector_size)
{
  DWORD                               dummy;
  uint64_t                            res;
  DISK_GEOMETRY_EX                    dg;
  DWORD                               outsize;
  STORAGE_PROPERTY_QUERY              storageQuery;
  STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR diskAlignment = { 0 };
  (void)device_file;

  memset(&diskAlignment, 0, sizeof(STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR));

  if (!DeviceIoControl(h, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, NULL, 0, &dg, sizeof(dg), &dummy, (LPOVERLAPPED)NULL))
    return 0;

  if (NULL != logical_sector_size || NULL != physical_sector_size)
  {
    memset(&storageQuery, 0, sizeof(STORAGE_PROPERTY_QUERY));
    storageQuery.PropertyId = StorageAccessAlignmentProperty;
    storageQuery.QueryType = PropertyStandardQuery;

    if (!DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &storageQuery, sizeof(STORAGE_PROPERTY_QUERY),
      &diskAlignment, sizeof(STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR), &outsize, NULL))
      return 0;

    if (NULL != logical_sector_size)
      *logical_sector_size = (uint32_t)diskAlignment.BytesPerLogicalSector;

    if (NULL != physical_sector_size)
      *physical_sector_size = (uint32_t)diskAlignment.BytesPerPhysicalSector;
  }


  res = (uint64_t)dg.DiskSize.QuadPart;

  return (0 != (res & SECTOR_SIZE_MASK)) ? 0 : (res >> SECTOR_SHIFT);
}

bool disk_read(disk_ptr dp, DISK_HANDLE h, uint64_t fp, uint8_t* buffer, uint32_t size)
{
  DWORD               read = 0;
  LARGE_INTEGER       distToMove, newFp;

  if (DISK_FLAG_READ_ACCESS_ERROR & dp->flags)
    return false;

  if ((0 != (size & 511)) || (0 != (fp & 511)) || (INVALID_DISK_HANDLE == h) || (NULL == buffer) || (0 == size))
    return false;

  distToMove.QuadPart = fp;
  if (!SetFilePointerEx(h, distToMove, &newFp, FILE_BEGIN))
  {
    dp->flags |= DISK_FLAG_READ_ACCESS_ERROR;
    return false;
  }
  if (newFp.QuadPart != fp)
  {
    dp->flags |= DISK_FLAG_READ_ACCESS_ERROR;
    return false;
  }

  if ((!ReadFile(h, (LPVOID)buffer, size, &read, NULL)) || (size != read))
  {
    dp->flags |= DISK_FLAG_READ_ACCESS_ERROR;
    return false;
  }

  return true;
}

bool disk_write(disk_ptr dp, DISK_HANDLE h, uint64_t fp, const uint8_t * buffer, uint32_t size )
{
  DWORD               written = 0;
  LARGE_INTEGER       distToMove, newFp;

  if (DISK_FLAG_WRITE_ACCESS_ERROR & dp->flags)
    return false;

  if ((0 != (size & 511)) || (0 != (fp & 511)) || (INVALID_DISK_HANDLE == h) || (NULL == buffer) || (0 == size))
    return false;

  distToMove.QuadPart = fp;
  if (!SetFilePointerEx(h, distToMove, &newFp, FILE_BEGIN))
  {
    dp->flags |= DISK_FLAG_READ_ACCESS_ERROR;
    return false;
  }
  if (newFp.QuadPart != fp)
  {
    dp->flags |= DISK_FLAG_READ_ACCESS_ERROR;
    return false;
  }

  if ((!WriteFile(h, (LPVOID)buffer, size, &written, NULL)) || (size != written))
  {
    dp->flags |= DISK_FLAG_WRITE_ACCESS_ERROR;
    return false;
  }

  return true;
}

int truncate(const char* file_name, uint64_t filesize)
{
  HANDLE h = CreateFile(file_name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  LARGE_INTEGER newSize, currentFp;

  if (INVALID_HANDLE_VALUE == h)
    return -1;

  newSize.QuadPart = (LONGLONG)filesize;

  if (!SetFilePointerEx(h, newSize, &currentFp, FILE_BEGIN))
  {
    CloseHandle(h);
    return -1;
  }

  if (!SetEndOfFile(h))
  {
    CloseHandle(h);
    return -1;
  }

  CloseHandle(h);

  return 0;
}

#else // _LINUX

DISK_HANDLE disk_open_device (const char* device_file, bool writeAccess)
{
  return writeAccess ? open(device_file, O_RDWR | O_SYNC | O_DIRECT) :
                       open(device_file, O_RDONLY | O_SYNC);
}

void disk_close_device(DISK_HANDLE h)
{
  if (INVALID_DISK_HANDLE != h)
  {
    syncfs(h);
    close(h);
  }
}

uint64_t disk_get_size(const char *device_file, DISK_HANDLE h, uint32_t* logical_sector_size, uint32_t* physical_sector_size)
{
  uint64_t            res;

  res = (uint64_t)lseek(h, 0, SEEK_END);

  (void)lseek(h, 0, SEEK_SET);

  if (NULL != logical_sector_size || NULL != physical_sector_size)
  {
    const char* p = strstr(device_file, "/dev/");
    char sys_file[256];
    FILE* f;

    if (NULL == p) // e.g. this is an image file; simulation of sectors sizes (logical, physical) is (512,512)
    {
      if (NULL != logical_sector_size)
        *logical_sector_size = 512;
      if (NULL != physical_sector_size)
        *physical_sector_size = 512;
    }
    else
    {
      p += sizeof("/dev/") - 1;

      if (NULL != logical_sector_size)
      {
        snprintf(sys_file, sizeof(sys_file), "/sys/block/%s/queue/hw_sector_size", p);
        f = fopen(sys_file, "rt");
        if (NULL == f)
        {
          *logical_sector_size = 512;
        }
        else
        {
          memset(sys_file, 0, sizeof(sys_file));
          (void)fread(sys_file, 1, sizeof(sys_file), f);
          fclose(f);
          *logical_sector_size = (uint32_t)strtoul(sys_file, NULL, 10);
          if (0 == *logical_sector_size)
            *logical_sector_size = 512;
        }
      }

      if (NULL != physical_sector_size)
      {
        snprintf(sys_file, sizeof(sys_file), "/sys/block/%s/queue/physical_block_size", p);
        f = fopen(sys_file, "rt");
        if (NULL == f)
        {
          *physical_sector_size = 512;
        }
        else
        {
          memset(sys_file, 0, sizeof(sys_file));
          (void)fread(sys_file, 1, sizeof(sys_file), f);
          fclose(f);
          *physical_sector_size = (uint32_t)strtoul(sys_file, NULL, 10);
          if (0 == *physical_sector_size)
            *physical_sector_size = 512;
        }
      }
    }
  }

  return (0 != (res & SECTOR_SIZE_MASK)) ? 0 : (res >> SECTOR_SHIFT);
}

bool disk_read(disk_ptr dp, DISK_HANDLE h, uint64_t fp, uint8_t* buffer, uint32_t size)
{
  if (DISK_FLAG_READ_ACCESS_ERROR & dp->flags)
    return false;

  if ((0 != (size & 511)) || (0 != (fp & 511)) || (INVALID_DISK_HANDLE == h) || (NULL == buffer) || (0 == size))
    return false;

  if (fp != ((uint64_t)lseek(h, fp, SEEK_SET)))
  {
    dp->flags |= DISK_FLAG_READ_ACCESS_ERROR;
    return false;
  }

  if (size != read(h, buffer, size))
  {
    dp->flags |= DISK_FLAG_READ_ACCESS_ERROR;
    return false;
  }

  return true;
}

bool disk_write(disk_ptr dp, DISK_HANDLE h, uint64_t fp, const uint8_t * buffer, uint32_t size)
{
  ssize_t written;

  if (NULL != dp)
  {
    if (DISK_FLAG_READ_ACCESS_ERROR & dp->flags)
      return false;
  }

  if ((0 != (size & 511)) || (0 != (fp & 511)) || (INVALID_DISK_HANDLE == h) || (NULL == buffer) || (0 == size))
    return false;

  if (fp != ((uint64_t)lseek(h, fp, SEEK_SET)))
  {
    dp->flags |= DISK_FLAG_READ_ACCESS_ERROR;
    return false;
  }

  if (size != (written=write(h, buffer, size)))
  {
    if (NULL != dp)
      dp->flags |= DISK_FLAG_WRITE_ACCESS_ERROR;
    return false;
  }

  return true;
}

#endif // !_WINDOWS

uint64_t disk_getFileSize(DISK_HANDLE h)
{
  return file_get_size(h);
}

void disk_free_sector(sector_ptr sp)
{
  if (NULL != sp)
  {
    if (NULL != sp->data_malloc_ptr)
      free(sp->data_malloc_ptr);
    free(sp);
  }
}

void disk_free_sector_list(sector_ptr sp)
{
  sector_ptr            next;

  while (NULL != sp)
  {
    next = sp->next;
    disk_free_sector(sp);
    sp = next;
  }
}

sector_ptr disk_read_sectors(disk_ptr dp, DISK_HANDLE h, sector_ptr* head, sector_ptr* tail, uint64_t lba, uint32_t num_sectors)
{
  sector_ptr                item, run;
  size_t                    data_size;

  if (unlikely(NULL == dp || INVALID_DISK_HANDLE == h || 0 == num_sectors))
    return NULL;

  if ((lba + num_sectors) > dp->device_sectors)
    return NULL;

  item = (sector_ptr)malloc(sizeof(sector));
  if (unlikely(NULL == item))
    return NULL;

  memset(item, 0, sizeof(sector));

  item->dp = dp;
  item->lba = lba;
  item->num_sectors = num_sectors;

  data_size = (((((size_t)item->num_sectors) << SECTOR_SHIFT) + (dp->physical_sector_size - 1)) & (~(dp->physical_sector_size - 1))) + SECTOR_MEM_ALIGN;

  item->data_malloc_ptr = malloc(data_size);
  if (unlikely(NULL == item->data_malloc_ptr))
  {
    free(item);
    return NULL;
  }

  item->data = (uint8_t*) ((((uint64_t)item->data_malloc_ptr) + (SECTOR_MEM_ALIGN - 1)) & (~(SECTOR_MEM_ALIGN - 1)));

  if (!disk_read(dp, h, lba << SECTOR_SHIFT, item->data, item->num_sectors << SECTOR_SHIFT))
  {
    free(item->data_malloc_ptr);
    free(item);
    return NULL;
  }

  if (NULL == head)
    return item; // do not perform any chaining

  // OK, now insert it at the right position in the double linked list of sectors (no checks for overlaps, though!)
  // sorting is purely performed based on 1st LBA of a sector range

  if (NULL == *head)
  {
    *head = *tail = item;
  }
  else
  {
    run = *head;

    while (NULL != run)
    {
      if (item->lba <= run->lba) // ok, insert before run
      {
        item->prev = run->prev;
        item->next = run;

        run->prev = item;

        if (NULL != item->prev)
          item->prev->next = item;
        else
          *head = item;

        break;
      }

      run = run->next;
    }

    if (NULL == run) // insert at tail
    {
      run->prev = *tail;
      if (NULL == *tail)
        *head = run;
      else
        (*tail)->next = run;
      *tail = run;
    }
  }

  return item;
}

static void exchange_disk_map_ptr(disk_map_ptr a, disk_map_ptr b, disk_map_ptr* head, disk_map_ptr* tail)
{
  disk_map_ptr    c;
  disk_map_ptr    a_prev;
  disk_map_ptr    a_next;
  disk_map_ptr    b_prev;
  disk_map_ptr    b_next;

  if (b->next == a) // exchange a and b
  {
    c = a;
    a = b;
    b = c;
  }

  a_prev = a->prev;
  a_next = a->next;
  b_prev = b->prev;
  b_next = b->next;

  if (a->next == b) // a and b are neighbours, i.e. a->next = b && b->prev = a
  {
    a->prev = b;
    a->prev->next = a;

    a->next = b_next;
    if (NULL == a->next)
      *tail = a;
    else
      a->next->prev = a;

    b->prev = a_prev;
    if (NULL == b->prev)
      *head = b;
    else
      b->prev->next = b;

    b->next = a;
    b->next->prev = b;
  }
  else
  {
    a->prev = b_prev;
    if (NULL == a->prev)
      *head = a;
    else
      a->prev->next = a;

    a->next = b_next;
    if (NULL == a->next)
      *tail = a;
    else
      a->next->prev = a;

    b->prev = a_prev;
    if (NULL == b->prev)
      *head = b;
    else
      b->prev->next = b;

    b->next = a_next;
    if (NULL == b->next)
      *tail = b;
    else
      b->next->prev = b;
  }
}

disk_map_ptr sort_and_complete_disk_map(disk_map_ptr dmp, uint64_t deviceSectorSize)
{
  disk_map_ptr            head = dmp;
  disk_map_ptr            tail, run = dmp, run2, min;
  uint64_t                lba = 0;

  if (NULL == dmp)
    return NULL;

  tail = dmp;
  while (NULL != tail->next)
    tail = tail->next;

  // sort all entries in ascending order

  while (NULL != run)
  {
    run2 = run->next;

    if (NULL == run2)
      break;

    min = run;

    while (NULL != run2)
    {
      if (run2->start_lba < min->start_lba)
        min = run2;
      run2 = run2->next;
    }

    if (min != run) // we have to exchange an entry
    {
      exchange_disk_map_ptr(min, run, &head, &tail);
      run = min->next; // => idempotent!!!
    }
    else
      run = run->next;
  }

  // after sorting: fill all gaps with empty areas

  run = head;

  while (NULL != run)
  {
    if (unlikely(run->start_lba < lba))
    {
    ErrorExit:
      free_disk_map(dmp);
      return NULL;
    }

    if (run->start_lba > lba) // OK, there is a gap we have to fill now
    {
      dmp = (disk_map_ptr)malloc(sizeof(disk_map));
      if (unlikely(NULL == dmp))
        goto ErrorExit;
      memset(dmp, 0, sizeof(disk_map));

      dmp->start_lba = lba;
      dmp->end_lba = run->start_lba - 1;
      dmp->is_free = true;
      strcpy(dmp->description, "unallocated (free) space");

      dmp->next = run;
      if (NULL == run->prev)
        head = dmp;
      else
        dmp->prev = run->prev;
      run->prev = dmp;

      if (NULL != dmp->prev)
        dmp->prev->next = dmp;
    }

    lba = run->end_lba + 1;
    run = run->next;
  }

  if (tail->end_lba != (deviceSectorSize - 1))
  {
    dmp = (disk_map_ptr)malloc(sizeof(disk_map));
    if (unlikely(NULL == dmp))
      goto ErrorExit;
    memset(dmp, 0, sizeof(disk_map));

    dmp->start_lba = tail->end_lba + 1;
    dmp->end_lba = deviceSectorSize - 1;
    dmp->is_free = true;
    strcpy(dmp->description, "unallocated (free) space");

    tail->next = dmp;
    dmp->prev = tail;

    tail = dmp;
  }

  // if partition GUIDs are present, then ensure that there are no double ones

  run = head;

  while (NULL != run)
  {
    if (!is_zero_guid(run->guid))
    {
      run2 = head;
      while (NULL != run2)
      {
        if ((run != run2) && (!is_zero_guid(run2->guid)))
        {
          if (!memcmp(run->guid, run2->guid, 16))
            goto ErrorExit;
        }
        run2 = run2->next;
      }
    }
    run = run->next;
  }

  return head;
}

disk_ptr disk_create_new(cmdline_args_ptr cap, const char* device_file, bool is_image_file)
{
  disk_ptr        dp;
  DISK_HANDLE     h;

  if (NULL!=cap->pd_head)
  {
    dp = cap->pd_head;
    while (NULL != dp)
    {
      if (!strcmp(device_file, dp->device_file))
        return dp;
      dp = dp->next;
    }
  }

  // not in list, create new one

  h = disk_open_device(device_file, false/*read-only*/);
  if (INVALID_DISK_HANDLE == h)
    return NULL;

  dp = (disk_ptr)malloc(sizeof(disk));

  if (unlikely(NULL == dp))
  {
    disk_close_device(h);
    return NULL;
  }

  memset(dp, 0, sizeof(disk));

  strncpy(dp->device_file, device_file, sizeof(dp->device_file) - 1);

  if (is_image_file)
  {
    dp->logical_sector_size = dp->physical_sector_size = 512;
    dp->device_size = disk_getFileSize(h);
    dp->device_sectors = dp->device_size >> SECTOR_SHIFT;
    dp->flags |= DISK_FLAG_NOT_DEVICE_BUT_FILE;
  }
  else
  {
    dp->device_sectors = disk_get_size(dp->device_file, h, &dp->logical_sector_size, &dp->physical_sector_size);
    dp->device_size = dp->device_sectors << SECTOR_SHIFT;
  }

  disk_close_device(h);

  disk_scan_partitions(dp);

  return dp;
}

disk_ptr disk_setup_device(cmdline_args_ptr cap, const char* device_file)
{
  disk_ptr dp;

#ifdef _WINDOWS

  if (device_file[0] >= '0' && (device_file[0] <= '9')) // this is a physical drive
  {
    uint32_t i, device_no = (uint32_t)atoi(device_file);
    
    cap->win_device_no = device_no;

    dp = cap->pd_head;

    if (device_no >= cap->num_physical_disks)
    {
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET ": the specified physical drive number is out of bounds (%u):\n", device_no);
      for (i = 0; i < cap->num_physical_disks; i++)
      {
        fprintf(stdout, "  drive %u is '%s' (vendor: '%s')\n", i, dp->product, dp->vendor);
        dp = dp->next;
      }
          
      return NULL;
    }

    while (0 != device_no)
    {
      dp = dp->next;
      device_no--;
    }
  }
  else
  {
    dp = disk_create_new(cap, device_file, true/*yes, is image file*/);
  }

#else // _LINUX
  struct stat   st;

  if (0 != stat(device_file, &st))
    return NULL;

  if (S_ISREG(st.st_mode))
  {
    dp = disk_create_new(cap, device_file, true/*yes, is image file*/);
  }
  else
  if (S_ISBLK(st.st_mode))
  {
    dp = disk_create_new(cap, device_file, false/*no, is block device, not file*/);
  }
  else
    return NULL;

#endif

  return dp;
}

#ifdef _WINDOWS
#define MBR_PARTITION_INFO_MAX_SIZE           32768
#endif

#ifdef _WINDOWS

bool disk_mbr_get_partition_guid(disk_ptr dp, uint64_t start_lba, uint64_t num_lbas, GUID *guid)
{
  PDRIVE_LAYOUT_INFORMATION_EX    pdliex;
  DWORD                           i;

  if (NULL == dp || NULL == guid || NULL == dp->mbr_partition_info || 0 == dp->mbr_part_info_size)
    return false;

  pdliex = (PDRIVE_LAYOUT_INFORMATION_EX)dp->mbr_partition_info;

  for (i = 0; i < pdliex->PartitionCount; i++)
  {
    if ((start_lba == (pdliex->PartitionEntry[i].StartingOffset.QuadPart >> SECTOR_SHIFT)) &&
        (num_lbas == (pdliex->PartitionEntry[i].PartitionLength.QuadPart >> SECTOR_SHIFT)))
    {
      char buffer[64];
      memcpy(guid, &pdliex->PartitionEntry[i].Mbr.PartitionId, sizeof(GUID));
      format_guid(buffer, (const uint8_t*)guid, false);
      fprintf(stdout, "\n => HAVE PARTITION GUID {%s}\n", buffer);
      return true;
    }
  }

  return false;
}

#endif

bool disk_scan_partitions(disk_ptr dp)
{
  DISK_HANDLE                   h = disk_open_device(dp->device_file, false/*read-only*/);
  uint64_t                      backup_gpt_lba = dp->device_sectors - 1;
#ifdef _WINDOWS
  PDRIVE_LAYOUT_INFORMATION_EX  pdliex = NULL;
#endif

  if (INVALID_DISK_HANDLE == h)
    return false;

  dp->mbr = partition_scan_mbr(dp, h);
  if (NULL == dp->mbr)
    dp->flags &= ~(DISK_FLAG_HAS_MBR | DISK_FLAG_MBR_IS_PROTECTIVE);
  else
  {
    dp->flags |= DISK_FLAG_HAS_MBR;
    dp->mbr_dmp = partition_create_disk_map_mbr(dp);
    if (NULL != dp->mbr_dmp)
      dp->mbr_dmp = sort_and_complete_disk_map(dp->mbr_dmp, dp->device_sectors);

#ifdef _WINDOWS

    dp->mbr_partition_info = (uint8_t*)malloc(MBR_PARTITION_INFO_MAX_SIZE);

    if (NULL == dp->mbr_partition_info)
      return false;

    memset(dp->mbr_partition_info, 0, MBR_PARTITION_INFO_MAX_SIZE);
    dp->mbr_part_info_size = 0;

    if (!DeviceIoControl(h, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, NULL, 0, dp->mbr_partition_info, MBR_PARTITION_INFO_MAX_SIZE, &dp->mbr_part_info_size, NULL))
    {
      free(dp->mbr_partition_info);
      dp->mbr_partition_info = NULL;
      dp->mbr_part_info_size = 0;
    }
    else
    {
      if (dp->mbr_part_info_size < sizeof(DRIVE_LAYOUT_INFORMATION_EX))
      {
        free(dp->mbr_partition_info);
        dp->mbr_partition_info = NULL;
        dp->mbr_part_info_size = 0;
      }
      else
      {
        pdliex = (PDRIVE_LAYOUT_INFORMATION_EX)dp->mbr_partition_info;

        if (PARTITION_STYLE_MBR != pdliex->PartitionStyle)
        {
          free(dp->mbr_partition_info);
          dp->mbr_partition_info = NULL;
          dp->mbr_part_info_size = 0;
        }
      }
    }

#endif
  }

  dp->gpt1 = partition_scan_gpt(dp, h, 1 /* LBA of the primary GPT*/);

  if (NULL != dp->gpt1)
  {
    dp->primary_gpt_exists = true;
    if (dp->gpt1->header.header_corrupt || dp->gpt1->header.entries_corrupt)
      dp->primary_gpt_corrupt = true;
    
    if (dp->gpt1->header.backup_lba < dp->device_sectors)
      backup_gpt_lba = dp->gpt1->header.backup_lba;
  }

  dp->gpt2 = partition_scan_gpt(dp, h, backup_gpt_lba);
  if (NULL != dp->gpt2)
  {
    dp->backup_gpt_exists = true;
    if (dp->gpt2->header.header_corrupt || dp->gpt2->header.entries_corrupt)
      dp->backup_gpt_corrupt = true;
  }

  if (dp->primary_gpt_exists && !dp->primary_gpt_corrupt && dp->backup_gpt_exists && !dp->backup_gpt_corrupt)
  {
    dp->gpts_mismatch = !partition_compare_gpts(dp->gpt1, dp->gpt2);
    if (!dp->gpts_mismatch)
    {
      dp->flags |= DISK_FLAG_HAS_GPT;
      dp->gpt_dmp = partition_create_disk_map_gpt(dp->gpt1, dp->gpt2);
      if (NULL != dp->gpt_dmp)
        dp->gpt_dmp = sort_and_complete_disk_map(dp->gpt_dmp, dp->device_sectors);
    }
  }

  if ((dp->primary_gpt_exists && !dp->primary_gpt_corrupt) || (dp->backup_gpt_exists && !dp->backup_gpt_corrupt))
    (void)partition_peek_fs_for_gpt(dp, h);

  disk_close_device(h);

  return true;
}

void free_disk_map(disk_map_ptr dmp)
{
  disk_map_ptr next;

  while (NULL != dmp)
  {
    next = dmp->next;
    free(dmp);
    dmp = next;
  }
}

void disk_dump_map(disk_map_ptr dmp)
{
  char              size_str[16];

  fprintf(stdout, "     Start LBA                 End LBA            Number of LBAs        Size          Occupied?     \n");
  fprintf(stdout, "----------------------------------------------------------------------------------------------------\n");
  while (NULL != dmp)
  {
    format_disk_size((dmp->end_lba - dmp->start_lba + 1) << 9, size_str, sizeof(size_str));
    fprintf(stdout, "%s%20"FMT64"u .. %20"FMT64"u = %20"FMT64"u (%10s) %s" CTRL_RESET " '%s'\n", dmp->is_free ? CTRL_GREEN : CTRL_RED, dmp->start_lba, dmp->end_lba, dmp->end_lba - dmp->start_lba + 1, size_str, dmp->is_free ? "UNALLOCATED (FREE)" : "ALLOCATED   (USED)",
      dmp->description);
    dmp = dmp->next;
  }
}

#ifdef _WINDOWS

static void findDriveLetterForVolume(const char* szVolumeName, win_volume_ptr wvp)
{
  char* p, * pNames = (char*)malloc(16384);
  DWORD               dwReturned = 0;
  uint32_t            l;

  if (!GetVolumePathNamesForVolumeNameA(szVolumeName, pNames, 16384, &dwReturned))
    return;

  p = pNames;
  while (0 != *p)
  {
    l = (uint32_t)strlen(p);
    if (':' == p[1] && '\\' == p[2])
    {
      wvp->drive_letter = p[0];
      free(pNames);
      return;
    }
    p += l + 1;
  }

  free(pNames);
}

void disk_free_windows_volume_list(win_volume_ptr item)
{
  win_volume_ptr       next;

  while (NULL != item)
  {
    next = item->next;
    free(item);
    item = next;
  }
}

win_volume_ptr findWindowsVolumeByPartitionStartLBA(win_volume_ptr head, uint32_t disk_number, uint64_t start_lba)
{

  if (NULL == head || 0 == start_lba)
    return NULL;

  while (NULL != head)
  {
    if ((head->start_lba == start_lba) && (head->disk_number == disk_number))
      return head;

    head = head->next;
  }

  return NULL;
}

#define DEWV_BUFFER_SIZE      32768

win_volume_ptr disk_enumerate_windows_volumes(void)
{
  HANDLE                      h;
  char                        szVolumeName[256], szDeviceName[256];
  uint32_t                    l;
  DWORD                       dwCharCount;
  win_volume_ptr              head = NULL, tail = NULL, wvp, run;
  HANDLE                      hDrive;
  char                        szDrive[256];
  PVOLUME_DISK_EXTENTS        volumeDiskExtents;
  DWORD                       dwBytesReturned;
  uint8_t                    *buffer = (uint8_t*)malloc(DEWV_BUFFER_SIZE);

  if (NULL == buffer)
    return NULL;

  memset(szVolumeName, 0, sizeof(szVolumeName));
  h = FindFirstVolumeA(szVolumeName, sizeof(szVolumeName));
  if (INVALID_HANDLE_VALUE == h)
  {
    free(buffer);
    return NULL;
  }

  volumeDiskExtents = (PVOLUME_DISK_EXTENTS)buffer;

  do
  {
    l = (uint32_t)strlen(szVolumeName);

    if ((l < 48) || (memcmp(szVolumeName, "\\\\?\\Volume{", 11)))
      continue;

    memset(szDeviceName, 0, sizeof(szDeviceName));

    szVolumeName[l - 1] = 0x00; // remove trailing backslash
    dwCharCount = QueryDosDeviceA(&szVolumeName[4], szDeviceName, ARRAYSIZE(szDeviceName));
    szVolumeName[l - 1] = '\\'; // re-establish trailing backslash

    if ( (0 == dwCharCount) || memcmp(szDeviceName,"\\Device\\HarddiskVolume",sizeof("\\Device\\HarddiskVolume")-1) )
      continue;

    wvp = (win_volume_ptr)malloc(sizeof(win_volume));
    if (NULL == wvp)
      continue;

    memset(wvp, 0, sizeof(win_volume));
    strncpy(wvp->device_name, szDeviceName, sizeof(wvp->device_name) - 1);
    memcpy(wvp->volume_guid, &szVolumeName[10], 38);
    strncpy(wvp->volume_name, szVolumeName, sizeof(wvp->volume_name) - 1);

    // try to find a drive letter for this volume

    findDriveLetterForVolume(szVolumeName, wvp);

    // try to get the physical drive and partition start sector

    wvp->disk_number = (uint32_t)-1;
    wvp->start_lba = (uint64_t)-1;
    wvp->num_lbas = (uint64_t)-1;

    memset(szDrive, 0, sizeof(szDrive));
    memcpy(szDrive, szVolumeName, l - 1);
    //snprintf(szDrive, sizeof(szDrive), "\\\\.\\%c:", wvp->drive_letter);

    if (DRIVE_FIXED != GetDriveType(szVolumeName))
    {
      free(wvp);
    }
    else
    {
      hDrive = CreateFile(szDrive, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
      if (INVALID_HANDLE_VALUE != hDrive)
      {
        if (DeviceIoControl(hDrive, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0, volumeDiskExtents, DEWV_BUFFER_SIZE, &dwBytesReturned, NULL))
        {
          wvp->disk_number = (uint32_t)volumeDiskExtents->Extents[0].DiskNumber;
          wvp->start_lba = ((uint64_t)volumeDiskExtents->Extents[0].StartingOffset.QuadPart) >> 9; // assumed that sector size is 512
          wvp->num_lbas = ((uint64_t)volumeDiskExtents->Extents[0].ExtentLength.QuadPart) >> 9; // assumed that sector size is 512
          wvp->num_extents = (uint32_t)volumeDiskExtents->NumberOfDiskExtents; // we ONLY support volumes comprised of ONE SINGLE DISK EXTENT!!!
        }
        CloseHandle(hDrive);
      }

      wvp->volume_no = (uint32_t)strtoul(wvp->device_name + sizeof("\\Device\\HarddiskVolume") - 1, NULL, 10);

      if (NULL == head)
        head = tail = wvp;
      else
      {
        run = head;
        while (NULL != run)
        {
          if (wvp->volume_no <= run->volume_no)
            break;
          run = run->next;
        }

        // run = NULL; // TODO: this disables the sorting, just for debugging

        if (NULL == run)
        {
          wvp->prev = tail;
          tail->next = wvp;
          tail = wvp;
        }
        else
        {
          wvp->prev = run->prev;
          wvp->next = run;

          run->prev = wvp;
          if (run == head)
            head = wvp;
          else
            wvp->prev->next = wvp;
        }
      }
    }

    memset(szVolumeName, 0, sizeof(szVolumeName));
  } 
  while (FindNextVolumeA(h, szVolumeName, sizeof(szVolumeName)));

  FindVolumeClose(h);

  free(buffer);

  return head;
}

void disk_free_diskpart_volume_list(diskpart_volume_ptr item)
{
  diskpart_volume_ptr       next;

  while (NULL != item)
  {
    next = item->next;
    free(item);
    item = next;
  }
}

// EBD0A0A2 - B9E5 - 4433 - 87C0 - 68B6B72699C7   microsoft basic data
// C12A7328 - F81F - 11D2 - BA4B - 00A0C93EC93B   EFI system partition
// E3C9E316 - 0B5C - 4DB8 - 817D - F92DF00215AE   Microsoft Reserved
//
// -- little endian -----   -- big endian -----

const uint8_t guid_microsoft_basic_data[16] = { 0xA2,0xA0,0xD0,0xEB, 0xE5,0xB9, 0x33,0x44, 0x87,0xC0, 0x68,0xB6,0xB7,0x26,0x99,0xC7 }; // mixed endian...
const uint8_t guid_efi_system_partition[16] = { 0x28,0x73,0x2A,0xC1, 0x1F,0xF8, 0xD2,0x11, 0xBA,0x4B, 0x00,0xA0,0xC9,0x3E,0xC9,0x3B }; // mixed endian...
const uint8_t guid_microsoft_reserved[16]   = { 0x16,0xE3,0xC9,0xE3, 0x5C,0x0B, 0xB8,0x4D, 0x81,0x7D, 0xF9,0x2D,0xF0,0x02,0x15,0xAE }; // mixed endian...

diskpart_volume_ptr disk_enumerate_diskpart_volumes(cmdline_args_ptr cap)
{
  diskpart_volume_ptr         head = NULL;
  diskpart_volume_ptr         tail = NULL;
  diskpart_volume_ptr         item, run;
  char                        diskpart_exe[256];
  FILE                       *script;
  int                         exit_code;
  char                       *p, *p2, *endp, *p3;
  uint8_t                    *stdout_buffer;
  disk_ptr                    dp;
  uint32_t                    i, j;

  snprintf(diskpart_exe, sizeof(diskpart_exe), "%c:\\Windows\\System32\\diskpart.exe", cap->win_sys_drive);

  script = fopen(".\\diskpart.script", "wt");
  if (NULL == script)
    return NULL;

  fprintf(script, "list volume\n");
  fclose(script);

  stdout_buffer = (uint8_t*)malloc(MAX_STDOUT_CAPTURE);
  if (unlikely(NULL == stdout_buffer))
    return NULL;

  exit_code = execute_external_program(stdout_buffer, true, diskpart_exe, "/s", ".\\diskpart.script", NULL);
  (void)DeleteFile(".\\diskpart.script");
  if (0 != exit_code)
  {
    free(stdout_buffer);
    return NULL;
  }

  p = (char*)stdout_buffer;

  while ((0 != *p) && (NULL != (p2 = strchr(p, '\n'))))
  {
    if (!memcmp(p, "  --------", 10))
    {
      p = p2 + 1;
      break;
    }
    p = p2 + 1;
  }

  while ((0 != *p) && (NULL != (p2 = strchr(p, '\n'))))
  {
    *p2 = 0;
     
    if (memcmp(p + 39, "Partition", 9))
    {
      p = p2 + 1;
      continue;
    }

    if (!memcmp(p + 2, "Volume ", 7))
    {
      item = (diskpart_volume_ptr)malloc(sizeof(diskpart_volume));
      if (unlikely(NULL == item))
      {
        free(stdout_buffer);
        disk_free_diskpart_volume_list(head);
        return NULL;
      }
      memset(item, 0, sizeof(diskpart_volume));

      item->drive_letter = p[15];
      if ((item->drive_letter < 'A') || (item->drive_letter > 'Z'))
        item->drive_letter = 0x00;

      p += 9;

      if ((*p < '0') || (*p > '9'))
      {
        free(item);
        p = p2 + 1;
        continue;
      }

      item->volume_number = (uint32_t)strtoul(p, &endp, 10);

      p += (32 - 9);

      p3 = strchr(p, ' ');
      if (NULL != p3)
      {
        *p3 = 0;
        if (!_stricmp(p, "NTFS"))
          item->fs_type = FSYS_WIN_NTFS;
        else
        if (!_stricmp(p, "EXFAT"))
          item->fs_type = FSYS_WIN_EXFAT;
        else
        if (!_stricmp(p, "FAT32"))
          item->fs_type = FSYS_WIN_FAT32;
        else
        if (!_stricmp(p, "FAT16"))
          item->fs_type = FSYS_WIN_FAT16;
      }

      item->prev = tail;
      if (NULL == tail)
        head = item;
      else
        tail->next = item;
      tail = item;
    }
    p = p2 + 1;
  }

  free(stdout_buffer);

  // consolidate it with the partition information we have read from the disk
  
  dp = cap->pd_head;
  run = head;
  for (i = 0; i < cap->num_physical_disks; i++)
  {
    if (NULL != dp->gpt_dmp) // has GPT
    {
      gpt_ptr g = dp->gpt1;

      for (j = 0; j < g->header.number_of_part_entries; j++)
      {
        if ((!memcmp(guid_microsoft_basic_data, g->entries[j].type_guid, 16)) || // MSDN just states this one
            (!memcmp(guid_efi_system_partition, g->entries[j].type_guid, 16))) // but THAT'S WRONG, also ESPs are taken into account (because they are FAT-formatted)
        {
          if ((NULL == run) || (FSYS_UNKNOWN == run->fs_type) || g->entries[j].fs_type != run->fs_type) // abort, we cannot get this working!
          {
            disk_free_diskpart_volume_list(head);
            return NULL;
          }
          else
            run->start_lba = g->entries[j].part_start_lba;
          run = run->next;
        }
      }
    }
    else
    if (NULL != dp->mbr_dmp) // has MBR
    {
      mbr_part_sector_ptr mpsp = dp->mbr;

      while (NULL != mpsp)
      {
        for (j = 0; j < 4; j++)
        {
          switch (mpsp->part_table[j].part_type) // all FAT/NTFS are interesting for us
          {
            case 0x01: // "FAT12"
            case 0x04: // "FAT16 < 32MB"
            case 0x06: // "FAT16"
            case 0x07: // "HPFS/NTFS/exFAT"
            case 0x0B: // "WIN95 FAT32"
            case 0x0C: // "WIN95 FAT32 (LBA)"
            case 0x0E: // "WIN95 FAT16 (LBA)"
            case 0x0F: // "WIN95 Extended Partition (LBA)"
            case 0xef: // "EFI" -> is EFI here correctly? If not, then move it down to the next switch
              if ((NULL == run) || (FSYS_UNKNOWN == run->fs_type) || (mpsp->part_table[j].fs_type != run->fs_type)) // abort, we cannot get this working!
              {
                disk_free_diskpart_volume_list(head);
                return NULL;
              }
              else
                run->start_lba = mpsp->part_table[j].start_sector;
              run = run->next;
              break;

            default:
              break;
          }
        }
        mpsp = mpsp->next;
      }

      // diskpart puts hidden volumes at the end of the list (if I observed this correctly - what a mess!)
      
      mpsp = dp->mbr;

      while (NULL != mpsp)
      {
        for (j = 0; j < 4; j++)
        {
          switch (mpsp->part_table[j].part_type) // all FAT/NTFS are interesting for us
          {
            case 0x11: // "Hidden FAT12"
            case 0x14: // "Hidden FAT16 < 32MB"
            case 0x16: // "Hidden FAT16"
            case 0x17: // "Hidden HPFS/NTFS"
            case 0x1b: // "Hidden WIN95 FAT32"
            case 0x1c: // "Hidden WIN95 FAT32 (LBA)"
            case 0x1e: // "Hidden WIN95 FAT16 (LBA)"
            case 0x27: // "Hidden NTFS Windows RE"
              if ((NULL == run) || (FSYS_UNKNOWN == run->fs_type) || (mpsp->part_table[j].fs_type != run->fs_type)) // abort, we cannot get this working!
              {
                disk_free_diskpart_volume_list(head);
                return NULL;
              }
              else
                run->start_lba = mpsp->part_table[j].start_sector;
              run = run->next;
              break;

            default:
              break;
          }
        }
        mpsp = mpsp->next;
      }
    }
    // else ignore it...
    dp = dp->next;
  }

  if (NULL != run) // abort, we cannot get this working!
  {
    disk_free_diskpart_volume_list(head);
    return NULL;
  }

  return head;
}

#endif
