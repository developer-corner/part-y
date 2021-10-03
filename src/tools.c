#include <part-y.h>

void format_disk_size(uint64_t size, char* buf, size_t buf_size)
{
  if (size >= 1099511627776)
    snprintf(buf, buf_size, "%.2f TB", ((double)size) / 1099511627776.0);
  else
  if (size >= 1073741824)
    snprintf(buf, buf_size, "%.2f GB", ((double)size) / 1073741824.0);
  else
  if (size >= 1048576)
    snprintf(buf, buf_size, "%.2f MB", ((double)size) / 1048576.0);
  else
  if (size >= 1024)
    snprintf(buf, buf_size, "%.2f KB", ((double)size) / 1024.0);
  else
    snprintf(buf, buf_size, "%u", (uint32_t)size);
}

void format_64bit ( uint64_t x, char *buf, int buf_size )
{
  char          work[32];
  int           dots, l = snprintf(work,sizeof(work),"%"FMT64"u",x);

  memset(buf,0,buf_size);

  if (l >= buf_size)
  {
no_thousands:
    memcpy(buf,work,buf_size-1);
    return;
  }

  dots = (l - 1) / 3;

  if ((0 == dots) || ((l+dots) >= buf_size))
    goto no_thousands;

  buf += l + dots - 4;

  while (l > 3)
  {
    buf[0] = '.';
    buf[1] = work[l-3];
    buf[2] = work[l-2];
    buf[3] = work[l-1];
    buf -= 4;
    l -= 3;
  }
  switch(l)
  {
    case 1:
      buf[4-1] = work[l-1];
      break;
    case 2:
      buf[4-2] = work[l-2];
      buf[4-1] = work[l-1];
      break;
    case 3:
      buf[4-3] = work[l-3];
      buf[4-2] = work[l-2];
      buf[4-1] = work[l-1];
      break;
    default:
      break;
  }
}

// LBA = ((cylinder * heads_per_cylinder + heads) * sectors_per_track) + sector - 1
// cylinder: 0..1023, head: 0..255, sector: 1..63
// 1024 cylinders, 256 headers, 63 sectors
uint64_t chs2lba ( uint32_t cylinder, uint32_t head, uint32_t sector )
{
  if (0 == cylinder && 0 == head && 0 == sector)
    return 0;

  if (1023 == cylinder && 255 == head && 63 == sector)
    return (uint64_t)-1;

  return (((uint64_t)cylinder)*256+((uint64_t)head)) * 63 + ((uint64_t)sector) - 1;
}

void lba2chs ( uint64_t lba, uint32_t *cylinder, uint32_t *head, uint32_t *sector )
{
  uint64_t temp;

  if (0 == lba)
  {
    *cylinder = 0;
    *head = 0;
    *sector = 0;
    return;
  }

  temp = lba % (256 * 63);
  *cylinder = (uint32_t)(lba / (256 * 63));
  *head = (uint32_t)(temp / 63);
  *sector = ((uint32_t)(temp % 63)) + 1;
  
  if ((*cylinder > 1023) || (*head > 255))
  {
    *cylinder = 1023;
    *head = 255;
    *sector = 63;
  }

}

static const char hexdigit[16] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };

void hexdump ( const uint8_t *data, uint64_t size, uint64_t offset )
{
  char            szHexLine[128], szHex[32];
  unsigned char   x;
  int             i,j;

  if (!size)
    return;

  while (size>0)
  {
    memset(szHexLine,0x20,sizeof(szHexLine));
    szHexLine[77+8] = 0x0A;
    szHexLine[78+8] = 0x00;
    if (size>8)
      szHexLine[34+8] = '-';

    sprintf(szHex,"%016" FMT64 "X",offset);
    offset += 16;
    memcpy(szHexLine,szHex,16);

    i=0;j=0;
    while (size>0)
    {
      x = *(data++);
      size--;
      szHexLine[i*3+10+j+8] = hexdigit[x >> 4];
      szHexLine[i*3+11+j+8] = hexdigit[x & 15];

      if ((x<32) || (x>=127)) x = '.';

      szHexLine[i+61+8] = (char)x;

      i++;
      if (i==8)
        j = 2;
      if (i==16)
        break;
    }

    fprintf(stdout,"%s",szHexLine);
  }
}

/**********************************************************************************************//**
 * @fn  bool convertUTF162UTF8(const uint16_t* utf16, uint8_t* utf8, uint32_t utf8buffersize, bool win_console)
 *
 * @brief Converts a wide (UTF-16) string to an UTF-8 string. This function assumes that it is
 *        stored Little Endian so it returns false if it finds a BOM.
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param           utf16           pointer to zero-terminated UTF-16 character string.
 * @param [in,out]  utf8            pointer to the target buffer receiving the UTF-8 string.
 * @param           utf8buffersize  size of the UTF-8 string buffer in bytes. This string will
 *                                  always be zero-terminated.
 * @param           win_console     (only recognized on Windows hosts): if true, then convert the
 *                                  UTF-8 string to ASCII because this is used in the Windows console
 *                                  window
 *
 * @returns true on success, false otherwise (buffer too small, BOM detected, etc.)
 **************************************************************************************************/

bool convertUTF162UTF8(const uint16_t* utf16, uint8_t* utf8, uint32_t utf8buffersize, bool win_console)
{
  uint32_t              u16, u16_2;
#ifndef _WINDOWS
  (void)win_console;
#endif

  if (unlikely(NULL == utf16 || NULL == utf8 || 0 == utf8buffersize))
    return false;

#ifdef _WINDOWS
  if (win_console)
    return (0 == WideCharToMultiByte(CP_OEMCP, WC_ERR_INVALID_CHARS, (const wchar_t*)utf16, -1, (char*)utf8, utf8buffersize, NULL, NULL)) ? false : true;
#endif

  utf8buffersize--; // one character is needed for the null terminator

  if (0 == *utf16)
  {
    *utf8 = 0;
    return true;
  }

  if ((0xFF == ((const uint8_t*)utf16)[0]) && (0xFE == ((const uint8_t*)utf16)[1])) // LE BOM marker
    utf16++; // ignore it
  else
  if ((0xFE == ((const uint8_t*)utf16)[0]) && (0xFF == ((const uint8_t*)utf16)[1])) // BE BOM marker -> error for GUID partition tables
  {
ErrorExit:
    *utf8 = 0;
    return true;
  }

  while (0 != *utf16)
  {
    u16 = ((uint32_t)((const uint8_t*)utf16)[0]) | (((uint32_t)((const uint8_t*)utf16)[1]) << 8); // read LE!
    utf16++;

    if (u16 >= 0xD800 && u16 <= 0xDBFF) // high surrogate!
    {
      if (0 == *utf16)
        goto ErrorExit; // 2nd character required

      u16_2 = ((uint32_t)((const uint8_t*)utf16)[0]) | (((uint32_t)((const uint8_t*)utf16)[1]) << 8); // read LE!
      utf16++;

      if (likely(u16_2 >= 0xDC00 && u16_2 <= 0xDFFF)) // low surrogate
      {
        u16 = (((u16 & 0x3FF) << 10) | (u16_2 & 0x3FF)) + 0x10000;
      }
      else
        goto ErrorExit;
    }
    else
    if (unlikely(!((u16 <= 0xD7FF) || (u16 >= 0xE000 && u16 <= 0xFFFF))))
      goto ErrorExit;

    if (u16 < 0x80)
    {
      if (utf8buffersize < 1)
        goto ErrorExit;
      *(utf8++) = (uint8_t)u16;
    }
    else
    if (u16 < 0x800)
    {
      if (utf8buffersize < 2)
        goto ErrorExit;
      *(utf8++) = (char)((u16 >> 6) | 0xC0);
      *(utf8++) = (char)((u16 & 0x3F) | 0x80);
    }
    else
    if (u16 < 0x10000)
    {
      if (utf8buffersize < 3)
        goto ErrorExit;
      *(utf8++) = (char)((u16 >> 12) | 0xE0);
      *(utf8++) = (char)(((u16 >> 6) & 0x3F) | 0x80);
      *(utf8++) = (char)((u16 & 0x3F) | 0x80);
    }
    else
    {
      if (utf8buffersize < 4)
        goto ErrorExit;
      *(utf8++) = (char)((u16 >> 18) | 0xF0);
      *(utf8++) = (char)(((u16 >> 12) & 0x3F) | 0x80);
      *(utf8++) = (char)(((u16 >> 6) & 0x3F) | 0x80);
      *(utf8++) = (char)((u16 & 0x3F) | 0x80);
    }
  }

  *utf8 = 0;
  
  return true;
}

static uint8_t hex_digit_2_nibble[256] =
{
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, /* 0x00 .. 0x0F */
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, /* 0x10 .. 0x1F */
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, /* 0x20 .. 0x2F */
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, /* 0x30 .. 0x3F */
  0xFF, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, /* 0x40 .. 0x4F */
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, /* 0x50 .. 0x5F */
  0xFF, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, /* 0x60 .. 0x6F */
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, /* 0x70 .. 0x7F */
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, /* 0x80 .. 0x8F */
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, /* 0x90 .. 0x9F */
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, /* 0xA0 .. 0xAF */
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, /* 0xB0 .. 0xBF */
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, /* 0xC0 .. 0xCF */
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, /* 0xD0 .. 0xDF */
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, /* 0xE0 .. 0xEF */
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF  /* 0xF0 .. 0xFF */
};

// NO ERROR HANDLING, DO NOT USE THIS FUNCTION UNLESS YOU KNOW WHAT YOU ARE DOING
void parse_guid(uint8_t* guid, const char* guid_str, bool just_big_endian)
{
  if (just_big_endian)
  {
    guid[ 0] = (hex_digit_2_nibble[(uint8_t)guid_str[ 0]] << 4) | hex_digit_2_nibble[(uint8_t)guid_str[ 1]];
    guid[ 1] = (hex_digit_2_nibble[(uint8_t)guid_str[ 2]] << 4) | hex_digit_2_nibble[(uint8_t)guid_str[ 3]];
    guid[ 2] = (hex_digit_2_nibble[(uint8_t)guid_str[ 4]] << 4) | hex_digit_2_nibble[(uint8_t)guid_str[ 5]];
    guid[ 3] = (hex_digit_2_nibble[(uint8_t)guid_str[ 6]] << 4) | hex_digit_2_nibble[(uint8_t)guid_str[ 7]];

    guid[4] = (hex_digit_2_nibble[(uint8_t)guid_str[9]] << 4) | hex_digit_2_nibble[(uint8_t)guid_str[10]];
    guid[5] = (hex_digit_2_nibble[(uint8_t)guid_str[11]] << 4) | hex_digit_2_nibble[(uint8_t)guid_str[12]];

    guid[6] = (hex_digit_2_nibble[(uint8_t)guid_str[14]] << 4) | hex_digit_2_nibble[(uint8_t)guid_str[15]];
    guid[7] = (hex_digit_2_nibble[(uint8_t)guid_str[16]] << 4) | hex_digit_2_nibble[(uint8_t)guid_str[17]];
  }
  else // mixed endianess
  {
    guid[3] = (hex_digit_2_nibble[(uint8_t)guid_str[0]] << 4) | hex_digit_2_nibble[(uint8_t)guid_str[1]];
    guid[2] = (hex_digit_2_nibble[(uint8_t)guid_str[2]] << 4) | hex_digit_2_nibble[(uint8_t)guid_str[3]];
    guid[1] = (hex_digit_2_nibble[(uint8_t)guid_str[4]] << 4) | hex_digit_2_nibble[(uint8_t)guid_str[5]];
    guid[0] = (hex_digit_2_nibble[(uint8_t)guid_str[6]] << 4) | hex_digit_2_nibble[(uint8_t)guid_str[7]];

    guid[5] = (hex_digit_2_nibble[(uint8_t)guid_str[9]] << 4) | hex_digit_2_nibble[(uint8_t)guid_str[10]];
    guid[4] = (hex_digit_2_nibble[(uint8_t)guid_str[11]] << 4) | hex_digit_2_nibble[(uint8_t)guid_str[12]];

    guid[7] = (hex_digit_2_nibble[(uint8_t)guid_str[14]] << 4) | hex_digit_2_nibble[(uint8_t)guid_str[15]];
    guid[6] = (hex_digit_2_nibble[(uint8_t)guid_str[16]] << 4) | hex_digit_2_nibble[(uint8_t)guid_str[17]];
  }
  guid[8] = (hex_digit_2_nibble[(uint8_t)guid_str[19]] << 4) | hex_digit_2_nibble[(uint8_t)guid_str[20]];
  guid[9] = (hex_digit_2_nibble[(uint8_t)guid_str[21]] << 4) | hex_digit_2_nibble[(uint8_t)guid_str[22]];
  guid[10] = (hex_digit_2_nibble[(uint8_t)guid_str[24]] << 4) | hex_digit_2_nibble[(uint8_t)guid_str[25]];
  guid[11] = (hex_digit_2_nibble[(uint8_t)guid_str[26]] << 4) | hex_digit_2_nibble[(uint8_t)guid_str[27]];
  guid[12] = (hex_digit_2_nibble[(uint8_t)guid_str[28]] << 4) | hex_digit_2_nibble[(uint8_t)guid_str[29]];
  guid[13] = (hex_digit_2_nibble[(uint8_t)guid_str[30]] << 4) | hex_digit_2_nibble[(uint8_t)guid_str[31]];
  guid[14] = (hex_digit_2_nibble[(uint8_t)guid_str[32]] << 4) | hex_digit_2_nibble[(uint8_t)guid_str[33]];
  guid[15] = (hex_digit_2_nibble[(uint8_t)guid_str[34]] << 4) | hex_digit_2_nibble[(uint8_t)guid_str[35]];
}

void format_guid(char* buffer, const uint8_t* guid, bool just_big_endian) // mixed endianess
{
  if (!just_big_endian) // mixed endianess!
  {
    buffer[ 0] = hexdigit[guid[ 3] >> 4]; // little 32bit
    buffer[ 1] = hexdigit[guid[ 3] & 15];
    buffer[ 2] = hexdigit[guid[ 2] >> 4];
    buffer[ 3] = hexdigit[guid[ 2] & 15];
    buffer[ 4] = hexdigit[guid[ 1] >> 4];
    buffer[ 5] = hexdigit[guid[ 1] & 15];
    buffer[ 6] = hexdigit[guid[ 0] >> 4];
    buffer[ 7] = hexdigit[guid[ 0] & 15];
    buffer[ 8] = '-';
    buffer[ 9] = hexdigit[guid[ 5] >> 4]; // little 16bit
    buffer[10] = hexdigit[guid[ 5] & 15];
    buffer[11] = hexdigit[guid[ 4] >> 4];
    buffer[12] = hexdigit[guid[ 4] & 15];
    buffer[13] = '-';
    buffer[14] = hexdigit[guid[ 7] >> 4]; // little 16bit
    buffer[15] = hexdigit[guid[ 7] & 15];
    buffer[16] = hexdigit[guid[ 6] >> 4];
    buffer[17] = hexdigit[guid[ 6] & 15];
  }
  else // just big endian (as it should be)
  {
    buffer[ 0] = hexdigit[guid[ 0] >> 4];
    buffer[ 1] = hexdigit[guid[ 0] & 15];
    buffer[ 2] = hexdigit[guid[ 1] >> 4];
    buffer[ 3] = hexdigit[guid[ 1] & 15];
    buffer[ 4] = hexdigit[guid[ 2] >> 4];
    buffer[ 5] = hexdigit[guid[ 2] & 15];
    buffer[ 6] = hexdigit[guid[ 3] >> 4];
    buffer[ 7] = hexdigit[guid[ 3] & 15];
    buffer[ 8] = '-';
    buffer[ 9] = hexdigit[guid[ 4] >> 4];
    buffer[10] = hexdigit[guid[ 4] & 15];
    buffer[11] = hexdigit[guid[ 5] >> 4];
    buffer[12] = hexdigit[guid[ 5] & 15];
    buffer[13] = '-';
    buffer[14] = hexdigit[guid[ 6] >> 4];
    buffer[15] = hexdigit[guid[ 6] & 15];
    buffer[16] = hexdigit[guid[ 7] >> 4];
    buffer[17] = hexdigit[guid[ 7] & 15];
  }

  buffer[18] = '-';
  buffer[19] = hexdigit[guid[ 8] >> 4]; // big 16bit
  buffer[20] = hexdigit[guid[ 8] & 15];
  buffer[21] = hexdigit[guid[ 9] >> 4];
  buffer[22] = hexdigit[guid[ 9] & 15];
  buffer[23] = '-';
  buffer[24] = hexdigit[guid[10] >> 4]; // big endian 48bit
  buffer[25] = hexdigit[guid[10] & 15];
  buffer[26] = hexdigit[guid[11] >> 4];
  buffer[27] = hexdigit[guid[11] & 15];
  buffer[28] = hexdigit[guid[12] >> 4];
  buffer[29] = hexdigit[guid[12] & 15];
  buffer[30] = hexdigit[guid[13] >> 4];
  buffer[31] = hexdigit[guid[13] & 15];
  buffer[32] = hexdigit[guid[14] >> 4];
  buffer[33] = hexdigit[guid[14] & 15];
  buffer[34] = hexdigit[guid[15] >> 4];
  buffer[35] = hexdigit[guid[15] & 15];

  buffer[36] = 0x00; // zero-terminator
}

void convert_guid_from_to_mixed ( uint8_t *guid )
{
  uint8_t save[2];

  save[0] = guid[2];
  save[1] = guid[3];

  guid[3] = guid[0];
  guid[2] = guid[1];
  guid[1] = save[0]; // guid[2]
  guid[0] = save[1]; // guid[3]

  save[0] = guid[4];
  guid[4] = guid[5];
  guid[5] = save[0];

  save[0] = guid[6];
  guid[6] = guid[7];
  guid[7] = save[0];
}

static const uint8_t zero_guid[16] = { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 };

bool is_zero_guid(const uint8_t* guid)
{
  return (memcmp(guid, zero_guid, 16) ? false : true);
}

bool check_lba_range_is_free(disk_map_ptr dmp, uint64_t lba_start, uint64_t num_lbas)
{
  while (NULL != dmp)
  {
    if (dmp->is_free)
    {
      if (lba_start >= dmp->start_lba && (lba_start + num_lbas - 1) <= dmp->end_lba)
        return true;
    }

    dmp = dmp->next;
  }

  return false;
}

bool find_last_partition(disk_ptr dp, disk_map_ptr dmp, uint64_t* lba_start, uint64_t* num_lbas, bool *is_ntfs, uint64_t *lba_free_start, uint64_t *num_lba_free)
{
  uint64_t            max_lba_start = 0, max_lba_end = 0;
  mbr_part_sector_ptr mpsp = dp->mbr;
  uint32_t            i;

  *is_ntfs = false;
  *lba_free_start = (uint64_t)-1;
  *num_lba_free = (uint64_t)-1;

  while (NULL != dmp)
  {
    if (!dmp->is_free)
    {
      if (dmp->start_lba > max_lba_start)
      {
        max_lba_start = dmp->start_lba;
        max_lba_end = dmp->end_lba;
      }
    }
    else // free space
    {
      if (NULL == dmp->next) // check for trailing free space only
      {
        *lba_free_start = dmp->start_lba;
        *num_lba_free = dmp->end_lba - dmp->start_lba + 1;
      }
    }

    dmp = dmp->next;
  }

  if (0 == max_lba_start)
    return false;

  *lba_start = max_lba_start;
  *num_lbas = max_lba_end - max_lba_start + 1;
  
  while (NULL != mpsp)
  {
    for (i = 0; i < 4; i++)
    {
      if (mpsp->part_table[i].start_sector == *lba_start)
      {
        if (0x07 == mpsp->part_table[i].part_type)
          *is_ntfs = true;
        return true;
      }
    }

    mpsp = mpsp->next;
  }

  return false;
}

#ifdef _LINUX

/**********************************************************************************************//**
 * @fn  int execute_external_program(uint8_t stdout_buffer[MAX_STDOUT_CAPTURE], bool wait_for_child, const char* prog, ...)
 *
 * @brief This is the Linux version of executing an external executable as a forked child process.
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param stdout_buffer   Buffer to 131072 characters receiving the zero-terminated output of the
 *                        child. NULL if the output is not required.
 * @param wait_for_child  true to wait for the child process to be terminated. If a stdout_buffer
 *                        is specified, then this flag is implicitly set to true.
 * @param prog            the fully qualified file name of the process to be executed.
 * @param ...             Variable arguments providing the child process command line arguments.
 *                        NULL has to be specified as the final parameter.
 *
 * @returns the exit code of the process. 127 is returned if an error (not originating from the
 *          child)
 *          occurs. If wait_for_child is false, then a successful fork() returns 0 (OK). The real
 *          exit code of the child process is lost in this case.
 **************************************************************************************************/

int execute_external_program(uint8_t stdout_buffer[MAX_STDOUT_CAPTURE], bool wait_for_child, const char* prog, ...)
{
  va_list                   ap;
  uint32_t                  i, num_args = 0, stdout_idx = 0;
  const char** prog_args;
  pid_t                     pid;
  int                       exit_code = 127;
  int                       j, fdlimit = (int)sysconf(_SC_OPEN_MAX);
  int                       pipe_desc[2] = { -1, -1 };
  FILE                     *f;

  if (NULL != stdout_buffer)
  {
    if (pipe(pipe_desc) < 0)
      return 127;

    wait_for_child = true; // if we want to read child's output, we HAVE TO wait
  }

  va_start(ap, prog);
  while (NULL != va_arg(ap, char* const))
    num_args++;
  va_end(ap);

  prog_args = (const char**)malloc(sizeof(const char*) * (num_args + 2));
  if (unlikely(NULL == prog_args))
  {
    if (-1 != pipe_desc[0])
      close(pipe_desc[0]);
    if (-1 != pipe_desc[1])
      close(pipe_desc[1]);
    return 127;
  }

  prog_args[0] = prog;

  va_start(ap, prog);
  for (i = 0; i < num_args; i++)
  {
    prog_args[i + 1] = va_arg(ap, char* const);
  }
  prog_args[i + 1] = NULL;
  va_end(ap);

  pid = fork();
  if (pid < 0)
    return exit_code;
  if (pid > 0) // parent
  {
    free(prog_args);

    if (-1 != pipe_desc[1])
      close(pipe_desc[1]);

    if (-1 != pipe_desc[0])
    {
      f = fdopen(pipe_desc[0], "rt");
      if (NULL == f)
      {
        close(pipe_desc[0]);
        waitpid(pid, &exit_code, 0);
        return 127;
      }

      memset(stdout_buffer, 0, MAX_STDOUT_CAPTURE);
      while (!feof(f))
      {
        if (fgets((char*)&stdout_buffer[stdout_idx], MAX_STDOUT_CAPTURE - stdout_idx - 1, f))
        {
          stdout_idx += (uint32_t)strlen((const char*)(&stdout_buffer[stdout_idx]));
          if (stdout_idx == (MAX_STDOUT_CAPTURE - 1))
            break;
        }
      }

      fclose(f);
    }
    
    if (wait_for_child)
    {
      do 
      {
        pid = waitpid(pid, &exit_code, 0);
      } 
      while (pid == -1 && EINTR == errno);

      if (WIFEXITED(exit_code))
        return WEXITSTATUS(exit_code);

      return 127;
    }
    else
      return 0; // child process keeps running, so we can only return 0 (OK) here...
  }
  else // child
  {
    if (-1 != pipe_desc[0])
      close(pipe_desc[0]);
    for (j = 0; j < fdlimit; j++)
    {
      if (-1 != pipe_desc[1] && j == pipe_desc[1])
        continue; // do NOT close the PIPE descriptor
      close(j);
    }

    open("/dev/null", O_RDONLY); // this is 0 (stdin)

    if (-1 != pipe_desc[1])
    {
      dup2(pipe_desc[1], STDOUT_FILENO/*1*/);
      dup2(pipe_desc[1], STDERR_FILENO/*2*/);
      close(pipe_desc[1]);
    }
    else
    {
      open("/dev/null", O_RDWR); // this is 1 = stdout
      dup2(STDOUT_FILENO/*1*/, STDERR_FILENO/*2*/); // this is 2 = stderr
    }

    (void)execv(prog, (char* const*)prog_args);
    free(prog_args);
    _exit(exit_code);
  }
}

#else

extern BOOL APIENTRY MyCreatePipeEx(OUT LPHANDLE lpReadPipe,
  OUT LPHANDLE lpWritePipe,
  IN LPSECURITY_ATTRIBUTES lpPipeAttributes,
  IN DWORD nSize,
  DWORD dwReadMode,
  DWORD dwWriteMode);

/**********************************************************************************************//**
 * @fn  int execute_external_program(uint8_t stdout_buffer[MAX_STDOUT_CAPTURE], bool wait_for_child, const char* prog, ...)
 *
 * @brief This is the Windows version of executing an external executable as a forked child process.
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param stdout_buffer   Buffer to 4096 characters receiving the zero-terminated output of the
 *                        child. NULL if the output is not required.
 * @param wait_for_child  true to wait for the child process to be terminated. If a stdout_buffer
 *                        is specified, then this flag is implicitly set to true.
 * @param prog            the fully qualified file name of the process to be executed.
 * @param ...             Variable arguments providing the child process command line arguments.
 *                        NULL has to be specified as the final parameter.
 *
 * @returns the exit code of the process. 127 is returned if an error (not originating from the
 *          child)
 *          occurs. If wait_for_child is false, then a successful fork() returns 0 (OK). The real
 *          exit code of the child process is lost in this case.
 **************************************************************************************************/

int execute_external_program(uint8_t stdout_buffer[MAX_STDOUT_CAPTURE], bool wait_for_child, const char* prog, ...)
{
  va_list                   ap;
  uint32_t                  i, num_args = 0, stdout_idx = 0, bufferAvailable, toBeTransferred, all_args_len = 0, l;
  const char* arg;
  char* prog_args, * run;
  SECURITY_ATTRIBUTES       saAttr;
  HANDLE                    g_hChildStd_IN_Rd = INVALID_HANDLE_VALUE;
  HANDLE                    g_hChildStd_IN_Wr = INVALID_HANDLE_VALUE;
  HANDLE                    g_hChildStd_OUT_Rd = INVALID_HANDLE_VALUE;
  HANDLE                    g_hChildStd_OUT_Wr = INVALID_HANDLE_VALUE;
  PROCESS_INFORMATION       piProcInfo;
  STARTUPINFO               siStartInfo;
  OVERLAPPED                asyncRead;
  HANDLE                    hEvents[2];
  DWORD                     dwWaitResult, dwNumEvents = 1, dwExitCode = 0, dwRead;
  bool                      bProcessTerminated = false, bIoFailed = false;
  uint8_t                   read_buffer[512];

  memset(&saAttr, 0, sizeof(saAttr));
  memset(&piProcInfo, 0, sizeof(piProcInfo));
  memset(&siStartInfo, 0, sizeof(siStartInfo));
  memset(&asyncRead, 0, sizeof(asyncRead));

  if (NULL != stdout_buffer)
  {
    memset(stdout_buffer, 0, MAX_STDOUT_CAPTURE);
    wait_for_child = true; // if we want to read child's output, we HAVE TO wait
  }

  all_args_len += ((uint32_t)strlen(prog)) + 1;

  va_start(ap, prog);
  while (NULL != (arg = va_arg(ap, const char*)))
  {
    all_args_len += ((uint32_t)strlen(arg)) + 1; // either zero-terminator or space delimiter between arguments
    num_args++;
  }
  va_end(ap);

  prog_args = (char*)malloc(all_args_len);
  if (unlikely(NULL == prog_args))
    return 127;

  memset(prog_args, 0, all_args_len);
  va_start(ap, prog);
  run = prog_args;
  l = (uint32_t)strlen(prog);
  memcpy(run, prog, l);
  run += l;
  for (i = 0; i < num_args; i++)
  {
    *(run++) = 0x20; // space

    arg = va_arg(ap, const char*);

    l = (uint32_t)strlen(arg);
    memcpy(run, arg, l);
    run += l;
  }
  va_end(ap);

  saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
  saAttr.bInheritHandle = TRUE;
  saAttr.lpSecurityDescriptor = NULL;

  // We DO need the special named pipe implementation to are able to use the FILE_FLAG_OVERLAPPED flag!
  // if (!CreatePipe(&g_hChildStd_OUT_Rd, &g_hChildStd_OUT_Wr, &saAttr, 0))
  if (!MyCreatePipeEx(&g_hChildStd_OUT_Rd, &g_hChildStd_OUT_Wr, &saAttr, MAX_STDOUT_CAPTURE, FILE_FLAG_OVERLAPPED, 0/*FILE_FLAG_OVERLAPPED*/))
  {
ErrorExit:

    if (NULL != piProcInfo.hThread)
      CloseHandle(piProcInfo.hThread);

    if (NULL != piProcInfo.hProcess)
      CloseHandle(piProcInfo.hProcess);

    if (INVALID_HANDLE_VALUE != g_hChildStd_IN_Rd)
      CloseHandle(g_hChildStd_IN_Rd);
    if (INVALID_HANDLE_VALUE != g_hChildStd_IN_Wr)
      CloseHandle(g_hChildStd_IN_Wr);
    if (INVALID_HANDLE_VALUE != g_hChildStd_OUT_Rd)
      CloseHandle(g_hChildStd_OUT_Rd);
    if (INVALID_HANDLE_VALUE != g_hChildStd_OUT_Wr)
      CloseHandle(g_hChildStd_OUT_Wr);

    if (NULL != prog_args)
      free(prog_args);

    if (NULL != asyncRead.hEvent)
      CloseHandle(asyncRead.hEvent);

    return 127;
  }

  if (!SetHandleInformation(g_hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0))
    goto ErrorExit;

  if (!CreatePipe(&g_hChildStd_IN_Rd, &g_hChildStd_IN_Wr, &saAttr, 0))
    goto ErrorExit;

  if (!SetHandleInformation(g_hChildStd_IN_Wr, HANDLE_FLAG_INHERIT, 0))
    goto ErrorExit;

  if (NULL != stdout_buffer)
  {
    asyncRead.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL); // manual reset event
    if (NULL == asyncRead.hEvent)
      goto ErrorExit;
  }

  siStartInfo.cb = sizeof(STARTUPINFO);
  siStartInfo.hStdError = g_hChildStd_OUT_Wr;
  siStartInfo.hStdOutput = g_hChildStd_OUT_Wr;
  siStartInfo.hStdInput = g_hChildStd_IN_Rd;
  siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

  if (NULL != stdout_buffer)
  {
    if (!ReadFile(g_hChildStd_OUT_Rd, read_buffer, sizeof(read_buffer), NULL, &asyncRead))
    {
      if (ERROR_IO_PENDING != GetLastError())
        goto ErrorExit;
    }
    hEvents[1] = asyncRead.hEvent;
    dwNumEvents = 2;
  }

  // Create the child process. 

  if (!CreateProcess(prog, prog_args, NULL, NULL, TRUE, 0, NULL, NULL, &siStartInfo, &piProcInfo))
    goto ErrorExit;

  free(prog_args), prog_args = NULL;

  CloseHandle(g_hChildStd_OUT_Wr), g_hChildStd_OUT_Wr = INVALID_HANDLE_VALUE;
  CloseHandle(g_hChildStd_IN_Rd), g_hChildStd_IN_Rd = INVALID_HANDLE_VALUE;

  CloseHandle(piProcInfo.hThread); // always close thread handle LWP 0 (not needed anymore in the following code)
  piProcInfo.hThread = NULL;

  if (!wait_for_child) // asyncRead.hEvent is NULL in this case
  {
    CloseHandle(piProcInfo.hProcess);
    CloseHandle(g_hChildStd_OUT_Rd);
    CloseHandle(g_hChildStd_IN_Wr);
    return 0; // we do not wait for the child to be terminated, so just return 0 (OK) here
  }

  hEvents[0] = piProcInfo.hProcess;

  while (!bProcessTerminated && !bIoFailed)
  {
    dwWaitResult = WaitForMultipleObjects(dwNumEvents, hEvents, FALSE, INFINITE);
    switch (dwWaitResult)
    {
    case WAIT_OBJECT_0: // child process terminated
      if (!GetExitCodeProcess(piProcInfo.hProcess, &dwExitCode))
        dwExitCode = 127;
      bProcessTerminated = true;
      break;
    case WAIT_OBJECT_0 + 1: // stdout output from child is available (async. read)
      dwRead = 0;
      if ((GetOverlappedResult(g_hChildStd_OUT_Rd, &asyncRead, &dwRead, FALSE/*do not wait*/)) && (0 != dwRead))
      {
        bufferAvailable = MAX_STDOUT_CAPTURE - 1 - stdout_idx;
        toBeTransferred = (dwRead > bufferAvailable) ? bufferAvailable : dwRead;
        if (0 != toBeTransferred)
        {
          memcpy(&stdout_buffer[stdout_idx], read_buffer, toBeTransferred);
          stdout_idx += toBeTransferred;
        }
      }

      ResetEvent(asyncRead.hEvent); // should not be necessary (ReadFile normally does this) but anyway...
      asyncRead.Offset += dwRead; // maybe also not necessary for child process stdout reads but who knows...

      if (!ReadFile(g_hChildStd_OUT_Rd, read_buffer, sizeof(read_buffer), NULL, &asyncRead))
      {
        if (ERROR_IO_PENDING != GetLastError())
        {
          CancelIo(g_hChildStd_OUT_Rd);
          WaitForSingleObject(piProcInfo.hProcess, INFINITE);
          if (!GetExitCodeProcess(piProcInfo.hProcess, &dwExitCode))
            dwExitCode = 127;
          bIoFailed = true;
          break;
        }
      }

      break;

    default:
      CancelIo(g_hChildStd_OUT_Rd);
      WaitForSingleObject(piProcInfo.hProcess, INFINITE);
      if (!GetExitCodeProcess(piProcInfo.hProcess, &dwExitCode))
        dwExitCode = 127;
      bIoFailed = true;
      break;
    }
  }

  if (NULL != asyncRead.hEvent)
  {
    dwRead = 0;

    while ((GetOverlappedResult(g_hChildStd_OUT_Rd, &asyncRead, &dwRead, FALSE/*do not wait*/)) && (0 != dwRead))
    {
      bufferAvailable = MAX_STDOUT_CAPTURE - 1 - stdout_idx;
      toBeTransferred = (dwRead > bufferAvailable) ? bufferAvailable : dwRead;
      if (0 != toBeTransferred)
      {
        memcpy(&stdout_buffer[stdout_idx], read_buffer, toBeTransferred);
        stdout_idx += toBeTransferred;
      }

      ResetEvent(asyncRead.hEvent); // should not be necessary (ReadFile normally does this) but anyway...
      asyncRead.Offset += dwRead; // maybe also not necessary for child process stdout reads but who knows...
      dwRead = 0;

      if (!ReadFile(g_hChildStd_OUT_Rd, read_buffer, sizeof(read_buffer), NULL, &asyncRead))
      {
        if (ERROR_IO_PENDING != GetLastError())
          break;
      }
    }

    CancelIo(g_hChildStd_OUT_Rd); // for failsafe purposes...
    CloseHandle(asyncRead.hEvent);
  }

  CloseHandle(piProcInfo.hProcess);

  CloseHandle(g_hChildStd_OUT_Rd);
  CloseHandle(g_hChildStd_IN_Wr);

  return (int)dwExitCode;
}

int execute_external_program_with_input(const char *stdin_input, uint8_t stdout_buffer[MAX_STDOUT_CAPTURE], const char* prog, ...)
{
  va_list                   ap;
  uint32_t                  i, num_args = 0, stdout_idx = 0, bufferAvailable, toBeTransferred, all_args_len = 0, l, input_len;
  const char* arg;
  char* prog_args, * run;
  SECURITY_ATTRIBUTES       saAttr;
  HANDLE                    g_hChildStd_IN_Rd = INVALID_HANDLE_VALUE;
  HANDLE                    g_hChildStd_IN_Wr = INVALID_HANDLE_VALUE;
  HANDLE                    g_hChildStd_OUT_Rd = INVALID_HANDLE_VALUE;
  HANDLE                    g_hChildStd_OUT_Wr = INVALID_HANDLE_VALUE;
  PROCESS_INFORMATION       piProcInfo;
  STARTUPINFO               siStartInfo;
  OVERLAPPED                asyncRead;
  HANDLE                    hEvents[2];
  DWORD                     dwWaitResult, dwNumEvents = 1, dwExitCode = 0, dwRead = 0, dwWritten = 0;
  bool                      bProcessTerminated = false, bIoFailed = false;
  uint8_t                   read_buffer[512];

  memset(&saAttr, 0, sizeof(saAttr));
  memset(&piProcInfo, 0, sizeof(piProcInfo));
  memset(&siStartInfo, 0, sizeof(siStartInfo));
  memset(&asyncRead, 0, sizeof(asyncRead));

  if (NULL != stdout_buffer)
    memset(stdout_buffer, 0, MAX_STDOUT_CAPTURE);

  input_len = (uint32_t)strlen(stdin_input);

  all_args_len += ((uint32_t)strlen(prog)) + 1;

  va_start(ap, prog);
  while (NULL != (arg = va_arg(ap, const char*)))
  {
    all_args_len += ((uint32_t)strlen(arg)) + 1; // either zero-terminator or space delimiter between arguments
    num_args++;
  }
  va_end(ap);

  prog_args = (char*)malloc(all_args_len);
  if (unlikely(NULL == prog_args))
    return 127;

  memset(prog_args, 0, all_args_len);
  va_start(ap, prog);
  run = prog_args;
  l = (uint32_t)strlen(prog);
  memcpy(run, prog, l);
  run += l;
  for (i = 0; i < num_args; i++)
  {
    *(run++) = 0x20; // space

    arg = va_arg(ap, const char*);

    l = (uint32_t)strlen(arg);
    memcpy(run, arg, l);
    run += l;
  }
  va_end(ap);

  saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
  saAttr.bInheritHandle = TRUE;
  saAttr.lpSecurityDescriptor = NULL;

  // We DO need the special named pipe implementation to are able to use the FILE_FLAG_OVERLAPPED flag!
  // if (!CreatePipe(&g_hChildStd_OUT_Rd, &g_hChildStd_OUT_Wr, &saAttr, 0))
  if (!MyCreatePipeEx(&g_hChildStd_OUT_Rd, &g_hChildStd_OUT_Wr, &saAttr, MAX_STDOUT_CAPTURE, FILE_FLAG_OVERLAPPED, 0/*FILE_FLAG_OVERLAPPED*/))
  {
ErrorExit:

    if (NULL != piProcInfo.hThread)
      CloseHandle(piProcInfo.hThread);

    if (NULL != piProcInfo.hProcess)
      CloseHandle(piProcInfo.hProcess);

    if (INVALID_HANDLE_VALUE != g_hChildStd_IN_Rd)
      CloseHandle(g_hChildStd_IN_Rd);
    if (INVALID_HANDLE_VALUE != g_hChildStd_IN_Wr)
      CloseHandle(g_hChildStd_IN_Wr);
    if (INVALID_HANDLE_VALUE != g_hChildStd_OUT_Rd)
      CloseHandle(g_hChildStd_OUT_Rd);
    if (INVALID_HANDLE_VALUE != g_hChildStd_OUT_Wr)
      CloseHandle(g_hChildStd_OUT_Wr);

    if (NULL != prog_args)
      free(prog_args);

    if (NULL != asyncRead.hEvent)
      CloseHandle(asyncRead.hEvent);

    return 127;
  }

  if (!SetHandleInformation(g_hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0))
    goto ErrorExit;

  if (!CreatePipe(&g_hChildStd_IN_Rd, &g_hChildStd_IN_Wr, &saAttr, 0))
    goto ErrorExit;

  if (!SetHandleInformation(g_hChildStd_IN_Wr, HANDLE_FLAG_INHERIT, 0))
    goto ErrorExit;

  if (NULL != stdout_buffer)
  {
    asyncRead.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL); // manual reset event
    if (NULL == asyncRead.hEvent)
      goto ErrorExit;
  }

  siStartInfo.cb = sizeof(STARTUPINFO);
  siStartInfo.hStdError = g_hChildStd_OUT_Wr;
  siStartInfo.hStdOutput = g_hChildStd_OUT_Wr;
  siStartInfo.hStdInput = g_hChildStd_IN_Rd;
  siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

  if (NULL != stdout_buffer)
  {
    if (!ReadFile(g_hChildStd_OUT_Rd, read_buffer, sizeof(read_buffer), NULL, &asyncRead))
    {
      if (ERROR_IO_PENDING != GetLastError())
        goto ErrorExit;
    }
    hEvents[1] = asyncRead.hEvent;
    dwNumEvents = 2;
  }

  // Create the child process. 

  if (!CreateProcess(prog, prog_args, NULL, NULL, TRUE, 0, NULL, NULL, &siStartInfo, &piProcInfo))
    goto ErrorExit;

  free(prog_args), prog_args = NULL;

  // CloseHandle(g_hChildStd_OUT_Wr), g_hChildStd_OUT_Wr = INVALID_HANDLE_VALUE;
  CloseHandle(g_hChildStd_IN_Rd), g_hChildStd_IN_Rd = INVALID_HANDLE_VALUE;

  CloseHandle(piProcInfo.hThread); // always close thread handle LWP 0 (not needed anymore in the following code)
  piProcInfo.hThread = NULL;

  // Write the full input for the child process
  
  if ((!WriteFile(g_hChildStd_OUT_Wr, (LPCVOID)stdin_input, (DWORD)input_len, &dwWritten, NULL)) || (dwWritten != ((DWORD)input_len)))
    goto ErrorExit;

  hEvents[0] = piProcInfo.hProcess;

  while (!bProcessTerminated && !bIoFailed)
  {
    dwWaitResult = WaitForMultipleObjects(dwNumEvents, hEvents, FALSE, INFINITE);
    switch (dwWaitResult)
    {
    case WAIT_OBJECT_0: // child process terminated
      if (!GetExitCodeProcess(piProcInfo.hProcess, &dwExitCode))
        dwExitCode = 127;
      bProcessTerminated = true;
      break;
    case WAIT_OBJECT_0 + 1: // stdout output from child is available (async. read)
      dwRead = 0;
      if ((GetOverlappedResult(g_hChildStd_OUT_Rd, &asyncRead, &dwRead, FALSE/*do not wait*/)) && (0 != dwRead))
      {
        bufferAvailable = MAX_STDOUT_CAPTURE - 1 - stdout_idx;
        toBeTransferred = (dwRead > bufferAvailable) ? bufferAvailable : dwRead;
        if (0 != toBeTransferred)
        {
          memcpy(&stdout_buffer[stdout_idx], read_buffer, toBeTransferred);
          stdout_idx += toBeTransferred;
        }
      }

      ResetEvent(asyncRead.hEvent); // should not be necessary (ReadFile normally does this) but anyway...
      asyncRead.Offset += dwRead; // maybe also not necessary for child process stdout reads but who knows...

      if (!ReadFile(g_hChildStd_OUT_Rd, read_buffer, sizeof(read_buffer), NULL, &asyncRead))
      {
        if (ERROR_IO_PENDING != GetLastError())
        {
          bIoFailed = true;
          break;
        }
      }

      break;

    default:
      CancelIo(g_hChildStd_OUT_Rd);
      WaitForSingleObject(piProcInfo.hProcess, INFINITE);
      if (!GetExitCodeProcess(piProcInfo.hProcess, &dwExitCode))
        dwExitCode = 127;
      bIoFailed = true;
      break;
    }
  }

  if (NULL != asyncRead.hEvent)
  {
    dwRead = 0;

    while ((GetOverlappedResult(g_hChildStd_OUT_Rd, &asyncRead, &dwRead, FALSE/*do not wait*/)) && (0 != dwRead))
    {
      bufferAvailable = MAX_STDOUT_CAPTURE - 1 - stdout_idx;
      toBeTransferred = (dwRead > bufferAvailable) ? bufferAvailable : dwRead;
      if (0 != toBeTransferred)
      {
        memcpy(&stdout_buffer[stdout_idx], read_buffer, toBeTransferred);
        stdout_idx += toBeTransferred;
      }

      ResetEvent(asyncRead.hEvent); // should not be necessary (ReadFile normally does this) but anyway...
      asyncRead.Offset += dwRead; // maybe also not necessary for child process stdout reads but who knows...
      dwRead = 0;

      if (!ReadFile(g_hChildStd_OUT_Rd, read_buffer, sizeof(read_buffer), NULL, &asyncRead))
      {
        if (ERROR_IO_PENDING != GetLastError())
          break;
      }
    }

    CancelIo(g_hChildStd_OUT_Rd); // for failsafe purposes...
    CloseHandle(asyncRead.hEvent);
  }

  CloseHandle(piProcInfo.hProcess);

  CloseHandle(g_hChildStd_OUT_Rd);
  CloseHandle(g_hChildStd_IN_Wr);

  return (int)dwExitCode;
}

#endif // _LINUX
