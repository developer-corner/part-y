/**
 * @file   part-y.h
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @brief  implementation of functions that represent
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

#include <part-y.h>
#include <win_mbr2gpt.h>

#ifdef _WINDOWS
extern bool IsUserAdmin(void);
#ifdef _DEBUG
static _CrtMemState cms;
#endif
#endif

cmdline_args  ca;

static bool scan_size(char* p, char** endp, uint64_t *size )
{
  uint32_t          l, x = 0, y = 0;
  uint64_t          num_digits, factor = 1;
  char             *ep;

  if (NULL == p || NULL == endp || NULL == size)
    return false;

  *endp = 0;
  *size = 0;

  l = (uint32_t)strlen(p);

  if ((l >= (sizeof("REMAINING") - 1)) && (!memcmp(p, "REMAINING", sizeof("REMAINING") - 1)))
  {
    *endp = p + sizeof("REMAINING") - 1;
    *size = (uint64_t)-1;
    return true;
  }

  x = (uint32_t)strtoul(p, &ep, 10);
  if ('.' == *ep)
  {
    y = (uint32_t)strtoul(ep + 1, &p, 10);

    num_digits = (uint64_t)(p - ep);
    switch (num_digits)
    {
      case 1:
        factor = 10;
        break;
      case 2:
        factor = 100;
        break;
      case 3:
        factor = 1000;
        break;
      case 4:
        factor = 10000;
        break;
      case 5:
        factor = 100000;
        break;
      case 6:
        factor = 1000000;
        break;
      default:
        return false;
    }
  }
  else
    p = ep;

  l = (uint32_t)strlen(p);

  if ((l >= (sizeof("LBAS") - 1)) && (!memcmp(p, "LBAS", sizeof("LBAS") - 1)))
  {
    if (0 != y)
      return false;

    *endp = p + sizeof("LBAS") - 1;
    *size = ((uint64_t)x) << 9; // *512 = sector size
    return true;
  }

  if ((l >= 2) && (!memcmp(p, "MB", 2)))
  {
    *endp = p + 2;
    *size = (((uint64_t)x) * ((uint64_t)1048576)) + (((((uint64_t)y) * ((uint64_t)1048576))) / factor);
    return true;
  }

  if ((l >= 2) && (!memcmp(p, "GB", 2)))
  {
    *endp = p + 2;
    *size = (((uint64_t)x) * ((uint64_t)1073741824)) + (((((uint64_t)y) * ((uint64_t)1073741824))) / factor);
    return true;
  }

  if ((l >= 2) && (!memcmp(p, "TB", 2)))
  {
    *endp = p + 2;
    *size = (((uint64_t)x) * ((uint64_t)1099511627776)) + (((((uint64_t)y) * ((uint64_t)1099511627776))) / factor);
    return true;
  }

  return false;
}

static bool get_part_label(const char* p, char* buf, uint32_t max_chars)
{
  bool have_prime = false;
  bool have_quote = false;

  if (NULL == p || NULL == buf || max_chars < 2)
    return false;

  max_chars--; // zero-terminator

  if ('\'' == *p)
  {
    have_prime = true;
    p++;
  }
  else
    if ('"' == *p)
    {
      have_quote = true;
      p++;
    }

  while (0 != max_chars)
  {
    if (0 == *p)
    {
      if (have_prime || have_quote)
        return false;
      break;
    }
    if (have_prime && '\'' == *p)
    {
      p++;
      break;
    }
    if (have_quote && '"' == *p)
    {
      p++;
      break;
    }
    *(buf++) = *(p++);
  }

  if (0 != *p)
    return false;

  *buf = 0;
  
  return true;
}

static int onInfo(cmdline_args_ptr cap)
{
  if (NULL == cap->work_disk)
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET ": internal error, no working disk available.\n");
    return 1;
  }

  disk_dump_info(cap->work_disk);

  if (NULL != cap->work_disk->mbr_dmp)
  {
    if (!partition_dump_mbr(cap->work_disk))
      return 1;

    fprintf(stdout, "\n" CTRL_MAGENTA "Disk Map (MBR):" CTRL_RESET "\n\n");
    disk_dump_map(cap->work_disk->mbr_dmp);
    fprintf(stdout, "\n");
  }
  else
    fprintf(stdout, CTRL_YELLOW "INFO" CTRL_RESET ": no MBR found.\n");

  if (NULL != cap->work_disk->gpt_dmp)
  {
    if (!partition_dump_gpt(cap->work_disk))
      return 1;

    fprintf(stdout, CTRL_MAGENTA "Disk Map (GPT):" CTRL_RESET "\n\n");
    disk_dump_map(cap->work_disk->gpt_dmp);
    fprintf(stdout, "\n");
  }
  else
    fprintf(stdout, CTRL_YELLOW "INFO" CTRL_RESET ": no GPT found.\n");

  return 0;
}

static int onFill(cmdline_args_ptr cap)
{
  uint64_t      fill_size, current = 0, this_size;
  char          str[32];
  uint8_t      *zero_buffer;
#ifdef _WINDOWS
  DWORD         dwWritten;
#endif

  if (0 == cap->file_size)
    fill_size = cap->work_disk->device_size;
  else
    fill_size = cap->file_size;

  if (cap->dryrun)
  {
    fprintf(stdout, CTRL_MAGENTA "DRYRUN" CTRL_RESET ": Explaining what would be done.\n");

    if (((uint64_t)-1) == fill_size)
    {
      fill_size = cap->work_disk->device_size;

      format_64bit(cap->work_disk->device_size, str, sizeof(str));
      if (cap->device_is_real_device)
        fprintf(stdout, CTRL_YELLOW "INFO" CTRL_RESET ": Filling entire drive with zeros (" CTRL_RED "DANGEROUS" CTRL_RESET "): %s byte(s).\n",str);
      else
        fprintf(stdout, CTRL_YELLOW "INFO" CTRL_RESET ": Filling entire image file with zeros: %s byte(s).\n", str);
    }
    else
    {
      format_64bit(fill_size, str, sizeof(str));

      if (cap->device_is_real_device)
        fprintf(stdout, CTRL_YELLOW "INFO" CTRL_RESET ": Filling drive with zeros (" CTRL_RED "DANGEROUS" CTRL_RESET "): %s byte(s).\n", str);
      else
      {
        fprintf(stdout, CTRL_YELLOW "INFO" CTRL_RESET ": Filling image file with zeros: %s byte(s).\n", str);
        if (cap->work_disk->device_size != fill_size)
        {
          format_64bit(fill_size, str, sizeof(str));
          fprintf(stdout, CTRL_YELLOW "INFO" CTRL_RESET ": Image file size will be adjusted to %s byte(s).\n", str);
        }
      }
    }
  }
  else // no dry-run!
  {
#ifdef _WINDOWS

    HANDLE h = CreateFile(cap->device_name, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_WRITE_THROUGH | FILE_FLAG_NO_BUFFERING, NULL);

    if (INVALID_HANDLE_VALUE == h)
    {
      if (!cap->device_is_real_device)
        h = CreateFile(cap->device_name, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, CREATE_NEW, FILE_FLAG_WRITE_THROUGH | FILE_FLAG_NO_BUFFERING, NULL);
    }

    if (INVALID_HANDLE_VALUE == h)
    {
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET ": Unable to access the device/image file for writing.\n");
      return 1;
    }

#else

    int h = open(cap->device_name, O_WRONLY | O_SYNC | O_DIRECT);

    if (-1 == h)
    {
      if (!cap->device_is_real_device)
        h = open(cap->device_name, O_WRONLY | O_SYNC | O_DIRECT | O_CREAT);

      if (-1 == h)
      {
        fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET ": Unable to access the device/image file for writing.\n");
        return 1;
      }
    }

#endif

    zero_buffer = (uint8_t*)malloc(1024 * 1024);
    if (unlikely(NULL == zero_buffer))
    {
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET ": Insufficient memory available.\n");
ErrorExit:
#ifdef _WINDOWS
      CloseHandle(h);
#else
      close(h);
#endif
      return 1;
    }

    memset(zero_buffer, 0, 1024 * 1024);

    while (current != fill_size)
    {
      this_size = 1024 * 1024;
      if (this_size > (fill_size - current))
        this_size = fill_size - current;

#ifdef _WINDOWS
      if ((!WriteFile(h,zero_buffer,(DWORD)this_size,&dwWritten,NULL)) || (dwWritten != ((DWORD)this_size)))
#else
      if (((uint32_t)this_size) != write(h, zero_buffer, (uint32_t)this_size))
#endif
      {
        fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET ": Unable to perform write operation.\n");
        goto ErrorExit;
      }

      current += this_size;
    }
#ifdef _WINDOWS
    CloseHandle(h);
#else
    close(h);
#endif

    if ((!cap->device_is_real_device) && (fill_size != cap->work_disk->device_sectors))
      truncate(cap->device_name, fill_size);
  }

  return 0;
}

static int onEnumDisks(cmdline_args_ptr cap)
{
  disk_ptr                dp;
  char                    size_str[32], size_str2[32];
#ifdef _WINDOWS
  win_volume_ptr          wvp;
  diskpart_volume_ptr     dvp;
  char                    guid[48], filesystem[32], diskno[16];
#endif

  fprintf(stdout, CTRL_YELLOW "INFO" CTRL_RESET ": number of physical disks is %u\n", cap->num_physical_disks);

  if (0 != cap->num_physical_disks)
  {
    dp = cap->pd_head;

    fprintf(stdout, "device file         capacity    number of sectors   log.sec.size  phy.sec.size\n");
    fprintf(stdout, "------------------------------------------------------------------------------\n");

    while (NULL != dp)
    {
      format_disk_size(dp->device_size, size_str, sizeof(size_str));
      format_64bit(dp->device_sectors, size_str2, sizeof(size_str2));
      fprintf(stdout, CTRL_MAGENTA "%18s  " CTRL_GREEN "%9s   %17s   " CTRL_CYAN "%4u          %4u" CTRL_RESET "\n", dp->device_file, size_str, size_str2, dp->logical_sector_size, dp->physical_sector_size);
      dp = dp->next;
    }
  }

  // on Windows, also enumerate all volumes and diskpart volumes
  
#ifdef _WINDOWS

  wvp = cap->wvp;

  if (NULL != wvp)
  {
    fprintf(stdout, "\n   volume name               volume GUID                           drive\n");
    fprintf(stdout, "------------------------------------------------------------------------\n");
    while (NULL != wvp)
    {
      memcpy(guid, &wvp->volume_guid[1], 36);
      guid[36] = 0;
      if (((uint32_t)-1) != wvp->disk_number)
        snprintf(diskno, sizeof(diskno), "%u", wvp->disk_number);
      else
      {
        diskno[0] = '?';
        diskno[1] = 0x00;
      }

      fprintf(stdout, CTRL_YELLOW "%c  " CTRL_MAGENTA "%24s  " CTRL_GREEN "%36s  " CTRL_CYAN "%s" CTRL_RESET "\n", 0x00 == wvp->drive_letter ? '-' : wvp->drive_letter, wvp->device_name, guid, diskno);
      wvp = wvp->next;
    }
  }

  dvp = cap->dvp;

  if (NULL != dvp)
  {
    fprintf(stdout, "\nvolume number    drive letter    filesystem type\n");
    fprintf(stdout, "------------------------------------------------\n");
    while (NULL != dvp)
    {
      switch (dvp->fs_type)
      {
        case FSYS_WIN_FAT16:
          strcpy(filesystem, "FAT16");
          break;
        case FSYS_WIN_FAT32:
          strcpy(filesystem, "FAT32");
          break;
        case FSYS_WIN_EXFAT:
          strcpy(filesystem, "exFAT");
          break;
        case FSYS_WIN_NTFS:
          strcpy(filesystem, "NTFS");
          break;
        default:
          strcpy(filesystem, "unknown");
          break;
      };
      fprintf(stdout, CTRL_CYAN "%2u               "CTRL_YELLOW"%c               "CTRL_GREEN"%s"CTRL_RESET"\n", dvp->volume_number, 0 == dvp->drive_letter ? '-' : dvp->drive_letter, filesystem);
      dvp = dvp->next;
    }
  }

#endif // _WINDOWS

  return 0;
}

static int onHexdump(cmdline_args_ptr cap)
{
  uint64_t                i;
  uint8_t                 sector[512];
  DISK_HANDLE             h;

  if (NULL == cap->work_disk)
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET ": internal error, no working disk available.\n");
    return 1;
  }

  if (cap->lba_range_start >= cap->work_disk->device_sectors || cap->lba_range_end >= cap->work_disk->device_sectors)
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET ": specified LBA range is outside of the physical disk size.\n");
    return 1;
  }

  if (0 != cap->file_size)
  {
    if (cap->lba_range_start >= (cap->file_size >> 9) || cap->lba_range_end >= (cap->file_size >> 9))
    {
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET ": specified LBA range is outside of the user-supplied (overridden) disk size.\n");
      return 1;
    }
  }

  h = disk_open_device(cap->work_disk->device_file, false/*read-only*/);
  if (INVALID_DISK_HANDLE == h)
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET ": unable to open device/image file %s\n", cap->work_disk->device_file);
    return 1;
  }

  for (i = cap->lba_range_start; i <= cap->lba_range_end; i++)
  {
    fprintf(stdout, CTRL_YELLOW "INFO" CTRL_RESET ": Hexdump of LBA %"FMT64"u:\n", i);

    if (!disk_read(cap->work_disk,h,i << SECTOR_SHIFT, sector, sizeof(sector)))
    {
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET ": unable to read LBA from disk. ABORTING.\n");
      disk_close_device(h);
      return 1;
    }

    hexdump(sector, sizeof(sector), (i << 9));
  }

  disk_close_device(h);

  return 0;
}

static int onRestore (cmdline_args_ptr cap)
{
  char          message[256];
  DISK_HANDLE   h = INVALID_DISK_HANDLE;

  if (NULL == cap->work_disk)
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET ": No working disk available.\n");
    return 1;
  }

  if (0 == cap->backup_file[0])
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET ": Please specify a backup file.\n");
    return 1;
  }

  if (cap->dryrun)
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET ": Please specify '--yes-do-it' because there is NO dry-run available (restore).\n");
    return 1;
  }

  // Check that backup file is available

  fprintf(stdout, CTRL_CYAN "CHECKING" CTRL_RESET ": Have backup file ........................................: ");
  fflush(stdout);

#ifdef _WINDOWS
  if (_access(cap->backup_file, 0) == 0)
#else
  if (access(cap->backup_file, F_OK) == 0)
#endif
    fprintf(stdout, CTRL_GREEN "OK" CTRL_RESET "\n");
  else
  {
    fprintf(stdout, CTRL_RED "ERROR" CTRL_RESET "\n          The backup file %s is not available.\n",cap->backup_file);
    return 1;
  }

  // Restore it to the device

  fprintf(stdout, CTRL_CYAN "WORKING" CTRL_RESET " : Restoring backup to the disk device .....................: ");
  fflush(stdout);

  h = disk_open_device(cap->work_disk->device_file, true/*read-write*/);
  if (INVALID_DISK_HANDLE == h)
  {
    fprintf(stdout, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to open the device %s for reading AND writing.\n",cap->work_disk->device_file);
    return 1;
  }

  snprintf(message, sizeof(message), CTRL_CYAN "WORKING" CTRL_RESET " : Restoring backup to the disk device .....................: ");

  if (!restore_backup_file(cap->work_disk, h, cap->backup_file, message))
  {
    fprintf(stdout, CTRL_RED "ERROR" CTRL_RESET "\n          Unable to restore the backup file.\n");
    disk_close_device(h);
    return 1;
  }

  disk_close_device(h);

  fprintf(stdout, CTRL_GREEN "OK" CTRL_RESET "\n");

  return 0;
}

extern int onPrepareWindows10(cmdline_args_ptr cap);

extern uint8_t efi_load_option_additional_data_windows[0x88];

int main(int argc, char* argv[])
{
  int           exitcode = 1, i = -1;
  char         *p, *endp;
  uint32_t      part_type, l;
  uint64_t      part_size;
  char          part_label[40];
  disk_ptr      dp;

#if defined(_DEBUG) && defined(_WINDOWS)
  memset(&cms, 0, sizeof(cms));
  _CrtMemCheckpoint(&cms);

  _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_CHECK_ALWAYS_DF);
  _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDOUT);
  _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDOUT);
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDOUT);
#endif

#ifndef _WINDOWS
  if (0 != geteuid())
  {
    fprintf(stderr,CTRL_RED "ERROR" CTRL_RESET ": This program must be executed as root.\n");
    return 1;
  }
#else
  if (!IsUserAdmin())
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET ": This program must be executed with admin privileges (elevated).\n");
    return 1;
  }

  if (S_OK != CoInitializeEx(NULL, COINIT_MULTITHREADED))
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET ": Failed to call CoInitializeEx().\n");
    return 1;
  }

  if (S_OK != CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE, NULL))
  {
    fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET ": Failed to call CoInitializeSecurity().\n");
    return 1;
  }
#endif

  // Enable colored console output on Windows

#if defined(_WINDOWS)
  HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
  if (h == INVALID_HANDLE_VALUE)
    return 1;
  DWORD dwOutMode;
  if (GetConsoleMode(h, &dwOutMode)) // this fails if output is redirected to a file or pipe, respectively!
    SetConsoleMode(h, dwOutMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
  h = GetStdHandle(STD_ERROR_HANDLE);
  if (h == INVALID_HANDLE_VALUE)
    return 1;
  if (GetConsoleMode(h, &dwOutMode)) // this fails if output is redirected to a file or pipe, respectively!
    SetConsoleMode(h, dwOutMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif

  memset(&ca, 0, sizeof(ca));
  ca.win_sys_drive = 'C'; // C: is the default Windows system drive letter
  strncpy(ca.locale, "en-US", sizeof(ca.locale) - 1); // en-US is the default locale, de-DE is used by author, though...

#ifdef _WINDOWS
  ca.win_device_no = (uint32_t)-1;
#endif

  if (1 == argc)
    goto ShowHelp;

  if (!stricmp(argv[1], "version"))
    ca.command = COMMAND_VERSION;
  else
  if (!stricmp(argv[1], "help"))
    ca.command = COMMAND_HELP;
  else
  if (!stricmp(argv[1], "info"))
    ca.command = COMMAND_INFO;
  else
  if (!stricmp(argv[1], "backup"))
    ca.command = COMMAND_BACKUP;
  else
  if (!stricmp(argv[1], "restore"))
    ca.command = COMMAND_RESTORE;
  else
  if (!stricmp(argv[1], "create"))
    ca.command = COMMAND_CREATE;
  else
  if (!stricmp(argv[1], "convert"))
    ca.command = COMMAND_CONVERT;
  else
  if (!stricmp(argv[1], "preparewin10"))
    ca.command = COMMAND_PREPAREWIN10;
  else
  if (!stricmp(argv[1], "convertwin10"))
    ca.command = COMMAND_CONVERTWIN10;
  else
  if (!stricmp(argv[1], "writepmbr"))
    ca.command = COMMAND_WRITEPMBR;
  else
  if (!stricmp(argv[1], "repairgpt"))
    ca.command = COMMAND_REPAIRGPT;
  else
  if (!stricmp(argv[1], "fill"))
    ca.command = COMMAND_FILL;
  else
  if (!stricmp(argv[1], "hexdump"))
    ca.command = COMMAND_HEXDUMP;
  else
  if (!stricmp(argv[1], "enumdisks"))
    ca.command = COMMAND_ENUMDISKS;
  else
  {
ShowHelp:
    fprintf(stdout, PROGRAM_INFO "\n");
    fprintf(stdout, PROGRAM_AUTHOR "\n\n");
    fprintf(stdout, "  This is 'part-y' an MBR/GPT partition/format utility for Windows/Linux.\n");
    fprintf(stdout, "  Performs conversions, backups and restores partition table information,\n");
    fprintf(stdout, "  creates and formats (Linux-only) partitions.\n");
    fprintf(stdout, "  PLEASE DO READ the accompanying PDF manual!\n");
    fprintf(stdout, "  This tool works with physical (native) 512 byte sectors or with 512e\n");
    fprintf(stdout, "  (i.e. 512 byte logical on a 4096 byte physical sector size drive) ONLY.\n");
    fprintf(stdout, "  IT REFUSES TO RUN on a 4096/4096 (logical/physical) drive.\n\n");
    fprintf(stdout, "** THIS PROGRAM COMES WITH ABSOLUTELY NO WARRANTY. USE IT AT YOUR OWN RISK **\n\n");
    fprintf(stdout, CTRL_CYAN "usage: <command> [<options>...]" CTRL_RESET "\n\n");
    fprintf(stdout, CTRL_CYAN "------" CTRL_RESET "\n\n");
    fprintf(stdout, CTRL_YELLOW "  1.) available commands:" CTRL_RESET "\n");
    fprintf(stdout, "      " CTRL_YELLOW "version" CTRL_RESET "      displays program version and exits\n");
    fprintf(stdout, "      " CTRL_YELLOW "help" CTRL_RESET "         displays this help and exits\n\n");
    fprintf(stdout, "      " CTRL_YELLOW "info" CTRL_RESET "         displays information about disk / partition table(s)\n");
    fprintf(stdout, "      " CTRL_YELLOW "backup" CTRL_RESET "       creates partition table backup\n");
    fprintf(stdout, "      " CTRL_YELLOW "restore" CTRL_RESET "      restores a partition table/convertwin10 backup\n");
    fprintf(stdout, "      " CTRL_YELLOW "create" CTRL_RESET "       creates a full disk partioning in one step, optionally\n");
    fprintf(stdout, "                   formatting the partitions (Linux-only)\n");
    fprintf(stdout, "      " CTRL_YELLOW "convert" CTRL_RESET "      converts MBR to GPT\n");
    fprintf(stdout, "      " CTRL_YELLOW "preparewin10" CTRL_RESET " (Windows-only) performs checks if a conversion from\n");
    fprintf(stdout, "                   Windows 10 MBR-disk to Windows 10 GPT-disk is possible\n");
    fprintf(stdout, "                   Also creates Boot Configuration Data (BCD) using bcdedit.exe\n");
    fprintf(stdout, "      " CTRL_YELLOW "convertwin10" CTRL_RESET " converts a Windows 10 disk from MBR to GPT creating\n");
    fprintf(stdout, "                   missing partitions (e.g. EFI), installing BCD (boot\n");
    fprintf(stdout, "                   configuration data), etc. thus replacing MS Windows\n");
    fprintf(stdout, "                   tool mbr2gpt.exe (handles much more scenarios!).\n");
    fprintf(stdout, "                   DOES NOT WORK with encrypted drives (Bitlocker, other)\n");
    fprintf(stdout, "                   so you have to temporarily remove the encryption!\n");
    fprintf(stdout, "      " CTRL_YELLOW "writepmbr" CTRL_RESET "    establishes a protective MBR on a GPT-disk thus overwriting\n");
    fprintf(stdout, "                   a (hybrid) MBR.\n");
    fprintf(stdout, "      " CTRL_YELLOW "repairgpt" CTRL_RESET "    checks a GPT-drive, uses primary and secondary GPTs to repair\n");
    fprintf(stdout, "                   a corrupted GPT. This command can also be used if a drive is\n");
    fprintf(stdout, "                   resized (enlarged) to repair the secondary GPT at the end of\n");
    fprintf(stdout, "                   a drive.\n");
    fprintf(stdout, "      " CTRL_YELLOW "fill" CTRL_RESET "         fills a device/file with zeros (" CTRL_RED "DANGEROUS!" CTRL_RESET ")\n");
    fprintf(stdout, "      " CTRL_YELLOW "hexdump" CTRL_RESET "      dumps one or more LBAs\n");
    fprintf(stdout, "      " CTRL_YELLOW "enumdisks" CTRL_RESET "    enumerates all found physical disks\n");
    fprintf(stdout, "\n");

    fprintf(stdout, CTRL_GREEN "  2.) common options:" CTRL_RESET "\n");
    fprintf(stdout, "      " CTRL_GREEN "--disk=<disk>" CTRL_RESET " specify disk to operate on (can be an image file)\n");
    fprintf(stdout, "                    0, 1, 2 on Windows, /dev/sda, /dev/nvme0n1, \n");
    fprintf(stdout, "                    /dev/loop0, etc. on Linux. Or just an image file.\n");
    fprintf(stdout, "      " CTRL_GREEN "--yes-do-it" CTRL_RESET " all commands that perform disk writes require this\n");
    fprintf(stdout, "                  command line switch!\n");
    fprintf(stdout, "      " CTRL_GREEN "--dry-run" CTRL_RESET " do NOT write to disk, just perform a dry-run. Overrides\n");
    fprintf(stdout, "                --yes-do-it\n");
    fprintf(stdout, "      " CTRL_GREEN "--verbose" CTRL_RESET " be verbose, i.e. if a dry-run is executed, then the tool\n");
    fprintf(stdout, "                EXPLAINS what it would do.\n\n");
    fprintf(stdout, "      " CTRL_GREEN "--backup-file=<file>" CTRL_RESET " specify a backup file (where appropriate)\n");
    fprintf(stdout, "\n");

    fprintf(stdout, CTRL_MAGENTA "  3.) special options:" CTRL_RESET "\n");
    fprintf(stdout, "      " CTRL_MAGENTA "--lba-range=X,Y" CTRL_RESET " also saves this 512-byte sector range in a backup\n");
    fprintf(stdout, "                      scenario. Or specifies hexdump range.\n");
    fprintf(stdout, "      " CTRL_MAGENTA "--no-format" CTRL_RESET " does NOT try to format restored partitions\n");
    fprintf(stdout, "      " CTRL_MAGENTA "--win-sys-drive=X:" CTRL_RESET " define drive letter of Windows system drive,\n");
    fprintf(stdout, "                         defaults to C:\n");
    fprintf(stdout, "      " CTRL_MAGENTA "--linux-stick-drive=X:" CTRL_RESET " define drive letter of Linux live stick,\n");
    fprintf(stdout, "                         which receives processing data and backup when converting\n");
    fprintf(stdout, "                         Windows 10 from MBR to GPT.\n");
    fprintf(stdout, "      " CTRL_MAGENTA "--partition=X,Y[,\"Z\"]" CTRL_RESET " (can be specified more than once) : specifies\n");
    fprintf(stdout, "                       partitions to be created (aligned on 1MB boundary)\n");
    fprintf(stdout, "                       X = type: FAT12, FAT16, FAT32, exFAT, NTFS, WinRE, MSR, \n");
    fprintf(stdout, "                                 EXT2, EXT3, EXT4, SWAP, EFI\n");
    fprintf(stdout, "                       Y = size: xLBAS, x.yMB, x.yGB, x.yTB, REMAINING\n");
    fprintf(stdout, "                       option Z: label of the partition (GPT-only!)\n");
    fprintf(stdout, "      " CTRL_MAGENTA "--part-flags=<comma-sep. list>" CTRL_RESET " defines flags for the preceding\n");
    fprintf(stdout, "                   --partition switch. Flags can be: boot, system, hide-efi,\n");
    fprintf(stdout, "                   read-only, hidden, nomount\n");
    fprintf(stdout, "      " CTRL_MAGENTA "--part-type=MBR|GPT" CTRL_RESET " type of partition table (create command),\n");
    fprintf(stdout, "                          defaults to GPT.\n");
    fprintf(stdout, "      " CTRL_MAGENTA "--file-size=<size>" CTRL_RESET " for 'createimg' command; <size> is specified\n");
    fprintf(stdout, "                         as for --partition, see above.\n");
    fprintf(stdout, "                         Can also be used to limit the size of a device.\n");
    fprintf(stdout, "      " CTRL_MAGENTA "--locale=<locale>" CTRL_RESET " locale to be used in the Boot Configuration\n");
    fprintf(stdout, "                         Data (BCD); defaults to 'en-US'.\n");
    fprintf(stdout, "\n");
    if ((-1 != i) && (i < argc))
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET ": unable to parse command line argument: %s\n", argv[i]);
    return 1;
  }

  for (i = 2; i < argc; i++)
  {
    l = (uint32_t)strlen(argv[i]);

    if ((l > (sizeof("--disk=") - 1)) && (!memcmp(argv[i], "--disk=", sizeof("--disk=") - 1)))
      strncpy(ca.device_name, argv[i] + sizeof("--disk=") - 1, sizeof(ca.device_name) - 1);
    else
    if (!strcmp(argv[i],"--yes-do-it"))
      ca.yes_do_it = true;
    else
    if (!strcmp(argv[i],"--dry-run"))
      ca.dryrun = true;
    else
    if (!strcmp(argv[i],"--verbose"))
      ca.verbose = true;
    else
    if ((l > (sizeof("--backup-file=") - 1)) && (!memcmp(argv[i], "--backup-file=", sizeof("--backup-file=") - 1)))
      strncpy(ca.backup_file, argv[i] + sizeof("--backup-file=") - 1, sizeof(ca.backup_file) - 1);
    else
    if ((l > (sizeof("--lba-range=") - 1)) && (!memcmp(argv[i], "--lba-range=", sizeof("--lba-range=") - 1)))
    {
      ca.lba_range_start = (uint64_t)strtoul(argv[i] + sizeof("--lba-range=") - 1, &endp, 10);
      if (',' != *endp)
        goto ShowHelp;
      ca.lba_range_end = (uint64_t)strtoul(endp + 1, NULL, 10);
      if (ca.lba_range_end < ca.lba_range_start)
        goto ShowHelp;
    }
    else
    if (!strcmp(argv[i],"--no-format"))
      ca.no_format = true;
    else
    if ((l > (sizeof("--win-sys-drive=") - 1)) && (!memcmp(argv[i], "--win-sys-drive=", sizeof("--win-sys-drive=") - 1)))
    {
      ca.win_sys_drive = (char)toupper(argv[i][sizeof("--win-sys-drive=") - 1]);
      if (':' != argv[i][sizeof("--win-sys-drive=")])
        goto ShowHelp;
    }
    else
    if ((l > (sizeof("--linux-stick-drive=") - 1)) && (!memcmp(argv[i], "--linux-stick-drive=", sizeof("--linux-stick-drive=") - 1)))
    {
      ca.linux_stick_drive = (char)toupper(argv[i][sizeof("--linux-stick-drive=") - 1]);
      if (':' != argv[i][sizeof("--linux-stick-drive=")])
        goto ShowHelp;
    }
    else
    if ((l > (sizeof("--partition=") - 1)) && (!memcmp(argv[i], "--partition=", sizeof("--partition=") - 1)))
    {
      p = argv[i] + sizeof("--partition=") - 1;
      l = (uint32_t)strlen(p);

      if ((l > 6) && (!memcmp(p, "FAT12,", 6)))
      {
        part_type = PARTITION_TYPE_FAT12;
        p += 6;
      }
      else
      if ((l > 6) && (!memcmp(p, "FAT16,", 6)))
      {
        part_type = PARTITION_TYPE_FAT16;
        p += 6;
      }
      else
      if ((l > 6) && (!memcmp(p, "FAT32,", 6)))
      {
        part_type = PARTITION_TYPE_FAT32;
        p += 6;
      }
      else
      if ((l > 6) && (!memcmp(p, "exFAT,", 6)))
      {
        part_type = PARTITION_TYPE_EXFAT;
        p += 6;
      }
      else
      if ((l > 5) && (!memcmp(p, "NTFS,", 5)))
      {
        part_type = PARTITION_TYPE_NTFS;
        p += 5;
      }
      else
      if ((l > 6) && (!memcmp(p, "WinRE,", 6)))
      {
        part_type = PARTITION_TYPE_WINRE;
        p += 6;
      }
      else
      if ((l > 4) && (!memcmp(p, "MSR,", 4)))
      {
        part_type = PARTITION_TYPE_MSR;
        p += 4;
      }
      else
      if ((l > 5) && (!memcmp(p, "EXT2,", 5)))
      {
        part_type = PARTITION_TYPE_EXT2;
        p += 5;
      }
      else
      if ((l > 5) && (!memcmp(p, "EXT3,", 5)))
      {
        part_type = PARTITION_TYPE_EXT3;
        p += 5;
      }
      else
      if ((l > 5) && (!memcmp(p, "EXT4,", 5)))
      {
        part_type = PARTITION_TYPE_EXT4;
        p += 5;
      }
      else
      if ((l > 5) && (!memcmp(p, "SWAP,", 5)))
      {
        part_type = PARTITION_TYPE_SWAP;
        p += 5;
      }
      else
      if ((l > 4) && (!memcmp(p, "EFI,", 4)))
      {
        part_type = PARTITION_TYPE_EFI;
        p += 4;
      }
      else
        goto ShowHelp;

      if (!scan_size(p, &endp, &part_size))
        goto ShowHelp;

      if (0 != *endp && ',' != *endp)
        goto ShowHelp;

      memset(part_label, 0, sizeof(part_label));
      if (',' == *endp)
      {
        if (!get_part_label(endp + 1, part_label, 36))
          goto ShowHelp;
      }

      if (128 == ca.num_part_defs)
        goto ShowHelp;

      ca.part_defs[ca.num_part_defs].size = part_size;
      ca.part_defs[ca.num_part_defs].type = part_type;
      memcpy(ca.part_defs[ca.num_part_defs].label, part_label, 36);

      ca.num_part_defs++;
    }
    else
    if ((l > (sizeof("--part-flags=") - 1)) && (!memcmp(argv[i], "--part-flags=", sizeof("--part-flags=") - 1)))
    {
      p = argv[i] + sizeof("--part-flags=") - 1;
      if (0 == ca.num_part_defs)
        goto ShowHelp;
      while (0 != *p)
      {
        l = (uint32_t)strlen(p);
        if (l >= 4 && !memcmp(p, "boot", 4))
        {
          ca.part_defs[ca.num_part_defs - 1].flags |= GPT_ATTR_LEGACY_BIOS_BOOT;
          p += 4;
        }
        else
        if (l >= 6 && !memcmp(p, "system", 6))
        {
          ca.part_defs[ca.num_part_defs - 1].flags |= GPT_ATTR_SYSTEM_PARTITION;
          p += 6;
        }
        else
        if (l >= 8 && !memcmp(p, "hide-efi", 8))
        {
          ca.part_defs[ca.num_part_defs - 1].flags |= GPT_ATTR_HIDE_EFI;
          p += 8;
        }
        else
        if (l >= 9 && !memcmp(p, "read-only", 9))
        {
          ca.part_defs[ca.num_part_defs - 1].flags |= GPT_ATTR_READ_ONLY;
          p += 9;
        }
        else
        if (l >= 6 && !memcmp(p, "hidden", 6))
        {
          ca.part_defs[ca.num_part_defs - 1].flags |= GPT_ATTR_HIDDEN;
          p += 6;
        }
        else
        if (l >= 7 && !memcmp(p, "nomount", 7))
        {
          ca.part_defs[ca.num_part_defs - 1].flags |= GPT_ATTR_DO_NOT_MOUNT;
          p += 7;
        }
        else
          goto ShowHelp;

        if (0 == *p)
          break;

        if (',' != *p)
          goto ShowHelp;

        p++;
      } // of while
    }
    else
    if ((l > (sizeof("--part-type=") - 1)) && (!memcmp(argv[i], "--part-type=", sizeof("--part-type=") - 1)))
    {
      p = argv[i] + sizeof("--part-type=") - 1;
      if (!stricmp(p, "MBR"))
        ca.part_type_mbr = true;
      else
      if (!stricmp(p, "GPT"))
        ca.part_type_mbr = false;
      else
        goto ShowHelp;
    }
    else
    if ((l > (sizeof("--file-size=") - 1)) && (!memcmp(argv[i], "--file-size=", sizeof("--file-size=") - 1)))
    {
      p = argv[i] + sizeof("--file-size=") - 1;

      if (!scan_size(p, &endp, &ca.file_size))
        goto ShowHelp;
    }
    else
    if ((l > (sizeof("--locale=") - 1)) && (!memcmp(argv[i], "--locale=", sizeof("--locale=") - 1)))
    {
      strncpy(ca.locale, argv[i] + sizeof("--locale=") - 1, sizeof(ca.locale)-1);
    }
    else
      goto ShowHelp;
  } // for all command line arguments

  // perform some initializations
  
  if ((ca.verbose) && (COMMAND_VERSION != ca.command))
    fprintf(stdout, PROGRAM_INFO "\n\n");

  if (!ca.yes_do_it)
  {
    if (COMMAND_RESTORE == ca.command || COMMAND_CREATE == ca.command ||
      COMMAND_CONVERT == ca.command || COMMAND_CONVERTWIN10 == ca.command || COMMAND_PREPAREWIN10 == ca.command ||
      COMMAND_REPAIRGPT == ca.command || COMMAND_FILL == ca.command)
    {
      if (!ca.dryrun)
      {
        fprintf(stdout, CTRL_YELLOW "INFO" CTRL_RESET ": forcing dry-run because --yes-do-it not specified.\n");
        ca.dryrun = true;
      }
    }
  }
  else
  {
    if (ca.dryrun)
    {
      fprintf(stdout, CTRL_YELLOW "INFO" CTRL_RESET ": performing dry-run because it overrides --yes-do-it.\n");
      ca.yes_do_it = false;
    }
  }

  // scan all devices:
  
  if (COMMAND_VERSION != ca.command && COMMAND_HELP != ca.command)
  {
    ca.num_physical_disks = disk_explore_all(&ca.pd_head, &ca.pd_tail);

#ifdef _WINDOWS
    ca.wvp = disk_enumerate_windows_volumes();
    ca.dvp = disk_enumerate_diskpart_volumes(&ca);
#endif

    if (COMMAND_ENUMDISKS != ca.command)
    {
      ca.work_disk = disk_setup_device(&ca, ca.device_name);

      if (NULL == ca.work_disk)
      {
        fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET ": unable to setup the device/image file %s\n", ca.device_name);
        goto GlobalCleanUp;
      }

      ca.device_is_real_device = ca.work_disk->flags & DISK_FLAG_NOT_DEVICE_BUT_FILE ? false : true;

      if (ca.device_is_real_device)
      {
        if (0 == ca.work_disk->device_sectors)
        {
          fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET ": unable to get the device size in sectors (512 bytes units): %s\n", ca.device_name);
          goto GlobalCleanUp;
        }
        if (((uint64_t)-1) == ca.file_size)
          ca.file_size = ca.work_disk->device_sectors << 9;
      }
      else
      {
        if (0 == ca.work_disk->device_sectors)
        {
          if (0 == ca.file_size)
          {
            fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET ": please specify --file-size for the file: %s\n", ca.device_name);
            goto GlobalCleanUp;
          }

          if (((uint64_t)-1) == ca.file_size)
            ca.file_size = 0;

          if (511 & ca.file_size)
          {
            fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET ": --file-size must be divisible by 512\n");
            goto GlobalCleanUp;
          }

          ca.work_disk->device_sectors = ca.file_size >> 9;
        }
        else
        {
          if (511 & ca.work_disk->device_sectors)
          {
            fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET ": image file size is not divisible by 512: %s\n", ca.device_name);
            goto GlobalCleanUp;
          }

          ca.work_disk->device_sectors >>= 9;
        }
      }

      if (0 != ca.file_size)
      {
        if ((((uint64_t)-1) != ca.file_size) && (511 & ca.file_size))
        {
          fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET ": --file-size must be divisible by 512\n");
          goto GlobalCleanUp;
        }

        if (ca.device_is_real_device && ca.file_size > (ca.work_disk->device_sectors << 9))
        {
          fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET ": --file-size exceeds the size of the device file: %s\n", ca.device_name);
          goto GlobalCleanUp;
        }
      }
      else
      {
        //ca.file_size = ca.work_disk->device_sectors << 9;
      }

      if (0 != ca.file_size && ca.file_size != (ca.work_disk->device_sectors << 9))
      {
        if (((uint64_t)-1) == ca.file_size)
          fprintf(stdout, CTRL_YELLOW "INFO" CTRL_RESET ": physical device size %"FMT64"u is overridden by REMAINING (full size).\n", ca.work_disk->device_sectors << 9);
        else
          fprintf(stdout, CTRL_YELLOW "INFO" CTRL_RESET ": physical device size %"FMT64"u is overridden by %"FMT64"u\n", ca.work_disk->device_sectors << 9, ca.file_size);
      }
    }

    // truncate the file (if it is not a device)

    if (COMMAND_FILL != ca.command && COMMAND_ENUMDISKS != ca.command && COMMAND_INFO != ca.command && COMMAND_HEXDUMP != ca.command &&
      !ca.device_is_real_device && !ca.dryrun && ca.file_size != (ca.work_disk->device_sectors << 9))
    {
      if (0 != truncate(ca.device_name, ca.file_size))
      {
        fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET ": unable to truncate/extend %s to the specified file size\n", ca.device_name);
        goto GlobalCleanUp;
      }
      ca.work_disk->device_sectors = ca.file_size >> 9;
    }
  }

  switch (ca.command)
  {
    case COMMAND_VERSION:
      fprintf(stdout, PROGRAM_INFO "\n");
      exitcode = 0;
      break;

    case COMMAND_HELP:
      goto ShowHelp;

    case COMMAND_INFO:
      exitcode = onInfo(&ca);
      break;

    case COMMAND_RESTORE:
      exitcode = onRestore(&ca);
      break;

    case COMMAND_PREPAREWIN10:
#ifdef _LINUX
      fprintf(stderr,CTRL_RED "ERROR" CTRL_RESET ": this tool is only executable on MS Windows.\n");
#else
      exitcode = onPrepareWindows10(&ca);
#endif
      break;

    case COMMAND_CONVERTWIN10:
#ifdef _WINDOWS
      fprintf(stderr, CTRL_RED "ERROR" CTRL_RESET ": this tool is only executable on Linux.\n");
#else
      exitcode = win_mbr2gpt(&ca);
#endif
      break;

    case COMMAND_FILL:
      exitcode = onFill(&ca);
      break;

    case COMMAND_HEXDUMP:
      exitcode = onHexdump(&ca);
      break;

    case COMMAND_ENUMDISKS:
      exitcode = onEnumDisks(&ca);
      break;

    case COMMAND_REPAIRGPT:
    case COMMAND_WRITEPMBR:
    case COMMAND_BACKUP:
    case COMMAND_CREATE:
    case COMMAND_CONVERT:
      fprintf(stdout, CTRL_MAGENTA "SORRY" CTRL_GREEN ": Please check the next version of this tool. Currently not implemented!" CTRL_RESET "\n");
      break;

    default:
      break;
  }

GlobalCleanUp:

  // cleanup

  if (NULL != ca.work_disk)
  {
    dp = ca.pd_head;

    while (NULL != dp)
    {
      if (ca.work_disk == dp)
      {
        ca.work_disk = NULL;
        break;
      }
      dp = dp->next;
    }
  }

  disk_free_list(ca.pd_head);

#ifdef _WINDOWS

  disk_free_windows_volume_list(ca.wvp);
  disk_free_diskpart_volume_list(ca.dvp);

  CoUninitialize();

#endif

#if defined(_WINDOWS) && defined(_DEBUG)
  fprintf(stdout, "\n**************************************");
  fprintf(stdout, "\n* DUMPING WINDOWS MEMORY LEAKS NOW : *");
  fprintf(stdout, "\n**************************************\n");
  _CrtMemDumpAllObjectsSince(&cms);
  fprintf(stdout, "\n**************************************");
  fprintf(stdout, "\n* END OF WINDOWS MEMORY LEAK DUMP... *");
  fprintf(stdout, "\n**************************************\n");
#endif

  return exitcode;
}

