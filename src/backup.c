/**
 * @file   backup.c
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @brief  implementation of functions that manage
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

#include <part-y.h>

static const char backup_signature[16] = { 'P','A','R','T','-','Y','-','B','A','C','K','-','F','I','L','E' };

#define BACKUP_BUFFER_SIZE        (16<<20)    ///< 16 Megs

backup_header_ptr bootstrap_backup(uint64_t device_sectors)
{
  backup_header_ptr       bhp = (backup_header_ptr)malloc(sizeof(backup_header));

  if (unlikely(NULL == bhp))
    return NULL;

  memset(bhp, 0, sizeof(backup_header));

  memcpy(bhp->signature, backup_signature, 16);
  bhp->version = BACKUP_VERSION;
  bhp->first_record_ofs = SECTOR_SIZE;
  bhp->device_sectors = device_sectors;

  return bhp;
}

bool add_backup_record(backup_header_ptr bhp, uint64_t start_lba, uint64_t num_sectors)
{
  backup_record_ptr       run, brp;

  if (NULL == bhp || 0 == num_sectors)
    return false;

  if ((start_lba + num_sectors) > bhp->device_sectors)
    return false;

  brp = (backup_record_ptr)malloc(sizeof(backup_record));

  if (unlikely(NULL == brp))
    return false;

  memset(brp, 0, sizeof(backup_record));

  brp->start_lba = start_lba;
  brp->num_lbas = num_sectors;

  if (NULL == bhp->head) // add it as the head item
  {
    bhp->head = bhp->tail = brp;
  }
  else // keep the double linked list sorted, i.e. find the right position of this backup record
  {
    run = bhp->head;
    while (NULL != run)
    {
      if (start_lba <= run->start_lba)
        break;
      run = run->next;
    }

    if (NULL == run) // add to tail
    {
      brp->prev = bhp->tail;
      bhp->tail->next = brp;
      bhp->tail = brp;
    }
    else // add in front of 'run'
    {
      brp->next = run;
      brp->prev = run->prev;

      run->prev = brp;

      if (NULL != brp->prev)
        brp->prev->next = brp;
      else
        bhp->head = brp;
    }
  }

  bhp->num_records++;

  return true;
}

void free_backup_structure(backup_header_ptr bhp)
{
  backup_record_ptr         next;

  if (NULL == bhp)
    return;

  while (NULL != bhp->head)
  {
    next = bhp->head->next;
    free(bhp->head);
    bhp->head = next;
  }

  free(bhp);
}

bool create_backup_file(disk_ptr dp, backup_header_ptr bhp, DISK_HANDLE h, const char* backup_file, const char *message)
{
  uint8_t             header[SECTOR_SIZE], sector[SECTOR_SIZE];
  FILE_HANDLE         f;
  backup_record_ptr   brp;
  uint8_t            *buffer = NULL, *aligned_buffer;
  uint64_t            to_be_transferred, lba, this_size, overall_size, overall_counter = 0;
  sha3_context        ctx;
  const uint8_t      *hash;

  if (NULL == dp || NULL == bhp || INVALID_DISK_HANDLE == h || NULL == backup_file)
    return false;

  memset(&ctx, 0, sizeof(ctx));
  sha3_Init(&ctx, 512);

  f = file_open(backup_file, false/*open for write*/);
  if (INVALID_FILE_HANDLE == f)
    return false;

  // write header

  brp = bhp->head;

  overall_size = SECTOR_SIZE; // header

  while (NULL != brp)
  {
    overall_size += (brp->num_lbas + 1) << SECTOR_SHIFT;
    brp = brp->next;
  }

  memset(header, 0x55, sizeof(sector));

  memcpy(&header[0x0000], bhp->signature, 16);

  WRITE_BIG_ENDIAN32(header, 0x0010, bhp->version);
  WRITE_BIG_ENDIAN32(header, 0x0014, bhp->first_record_ofs);

  WRITE_BIG_ENDIAN64(header, 0x0018, bhp->device_sectors);
  WRITE_BIG_ENDIAN64(header, 0x0020, bhp->num_records);

  WRITE_BIG_ENDIAN64(header, 0x0028, overall_size);

  sha3_Update(&ctx, header, SECTOR_SIZE);

  if (!file_write(f, header, SECTOR_SIZE))
  {
ErrorExit:
    file_close(f,false/*do not flush*/);
    unlink(backup_file);
    if (NULL != buffer)
      free(buffer);
    return false;
  }

  overall_counter += SECTOR_SIZE;

  // write records
  
  buffer = (uint8_t*)malloc(BACKUP_BUFFER_SIZE + SECTOR_SIZE);
  if (unlikely(NULL == buffer))
    goto ErrorExit;

  aligned_buffer = (uint8_t*)((((uint64_t)buffer) + (SECTOR_SIZE - 1)) & (~(SECTOR_SIZE - 1)));

  brp = bhp->head;

  while (NULL != brp)
  {
    memset(sector, 0xAA, sizeof(sector));
    WRITE_BIG_ENDIAN64(sector, 0x0000, brp->start_lba);
    WRITE_BIG_ENDIAN64(sector, 0x0008, brp->num_lbas);

    sha3_Update(&ctx, sector, SECTOR_SIZE);

    if (!file_write(f, sector, SECTOR_SIZE))
      goto ErrorExit;

    overall_counter += SECTOR_SIZE;

    to_be_transferred = brp->num_lbas << SECTOR_SHIFT;

    lba = brp->start_lba;

    while (0 != to_be_transferred)
    {
      this_size = to_be_transferred > BACKUP_BUFFER_SIZE ? BACKUP_BUFFER_SIZE : to_be_transferred;

      if (!disk_read(dp, h, lba << SECTOR_SHIFT, aligned_buffer, (uint32_t)this_size))
        goto ErrorExit;

      overall_counter += this_size;

      if (NULL != message)
      {
        fprintf(stdout, "\r%s" CTRL_GREEN "%3.2f%%" CTRL_RESET, message, (((double)overall_counter) * 100.0) / ((double)overall_size));
        fflush(stdout);
      }

      lba += this_size >> SECTOR_SHIFT;

      sha3_Update(&ctx, aligned_buffer, (size_t)this_size);

      if (!file_write(f, aligned_buffer, (uint32_t)this_size))
        goto ErrorExit;

      to_be_transferred -= this_size;
    }

    brp = brp->next;
  }

  if (NULL != message)
  {
    fprintf(stdout, "\r%s       \r%s", message, message);
    fflush(stdout);
  }

  hash = (uint8_t*)sha3_Finalize(&ctx);

  memcpy(&header[0x30], hash, 32);

  if (!file_setpointer(f, 0))
    goto ErrorExit;

  if (!file_write(f, header, SECTOR_SIZE))
    goto ErrorExit;

  file_close(f,true/*do flush*/);

  free(buffer);

  return true;
}

static bool check_filler(const uint8_t* buffer, uint32_t size, uint8_t value)
{
  uint32_t          i;

  for (i = 0; i < size; i++)
    if (value != buffer[i])
      return false;

  return true;
}

bool check_backup_file(disk_ptr dp, DISK_HANDLE h, const char* backup_file, const char *message)
{
  uint8_t             sector[SECTOR_SIZE];
  FILE_HANDLE         f;
  backup_header       bh;
  backup_record       br;
  uint8_t            *buffer = NULL, *aligned_buffer, *aligned_buffer2;
  uint8_t            *buffer2 = NULL;
  uint64_t            i, to_be_transferred, lba, this_size, overall_size, overall_counter = 0;
  sha3_context        ctx;
  const uint8_t      *hash;
  uint8_t             orig_hash[32];

  if (NULL == dp || INVALID_DISK_HANDLE == h || NULL == backup_file)
    return false;

  memset(&ctx, 0, sizeof(ctx));
  sha3_Init(&ctx, 512);

  f = file_open(backup_file, true/*read-only*/);
  if (INVALID_FILE_HANDLE == f)
    return false;

  // read header

  if (!file_read(f, sector, SECTOR_SIZE))
  {
ErrorExit:
    file_close(f,false);
    if (NULL != buffer)
      free(buffer);
    if (NULL != buffer2)
      free(buffer2);
    return false;
  }

  overall_counter += SECTOR_SIZE;

  if (memcmp(&sector[0x0000], backup_signature, 16))
    goto ErrorExit;

  memcpy(orig_hash, &sector[0x0030], 32);
  memset(&sector[0x0030], 0x55, 32);

  if (!check_filler(&sector[0x0030], SECTOR_SIZE - 0x0030, 0x55))
    goto ErrorExit;

  sha3_Update(&ctx, sector, SECTOR_SIZE);

  bh.version = READ_BIG_ENDIAN32(sector, 0x0010);
  bh.first_record_ofs = READ_BIG_ENDIAN32(sector, 0x0014);

  if (BACKUP_VERSION != bh.version)
    goto ErrorExit;
  if (SECTOR_SIZE != bh.first_record_ofs)
    goto ErrorExit;

  bh.device_sectors = READ_BIG_ENDIAN64(sector, 0x0018);
  bh.num_records = READ_BIG_ENDIAN64(sector, 0x0020);
  overall_size = READ_BIG_ENDIAN64(sector, 0x0028);

  if (dp->device_sectors != bh.device_sectors)
    goto ErrorExit;

  // read and check records

  buffer = (uint8_t*)malloc(BACKUP_BUFFER_SIZE + SECTOR_SIZE);
  if (unlikely(NULL == buffer))
    goto ErrorExit;

  buffer2 = (uint8_t*)malloc(BACKUP_BUFFER_SIZE + SECTOR_SIZE);
  if (unlikely(NULL == buffer2))
    goto ErrorExit;

  aligned_buffer = (uint8_t*)((((uint64_t)buffer) + (SECTOR_SIZE - 1)) & (~(SECTOR_SIZE - 1)));

  aligned_buffer2 = (uint8_t*)((((uint64_t)buffer2) + (SECTOR_SIZE - 1)) & (~(SECTOR_SIZE - 1)));

  for (i = 0; i < bh.num_records; i++)
  {
    if (!file_read(f, sector, SECTOR_SIZE))
      goto ErrorExit;

    overall_counter += SECTOR_SIZE;

    sha3_Update(&ctx, sector, SECTOR_SIZE);

    if (!check_filler(&sector[0x0010], SECTOR_SIZE - 0x0010, 0xAA))
      goto ErrorExit;

    br.start_lba = READ_BIG_ENDIAN64(sector, 0x0000);
    br.num_lbas = READ_BIG_ENDIAN64(sector, 0x0008);

    if ((br.start_lba + br.num_lbas) > dp->device_sectors)
      goto ErrorExit;

    to_be_transferred = br.num_lbas << SECTOR_SHIFT;

    lba = br.start_lba;

    while (0 != to_be_transferred)
    {
      this_size = to_be_transferred > BACKUP_BUFFER_SIZE ? BACKUP_BUFFER_SIZE : to_be_transferred;

      if (INVALID_DISK_HANDLE != h)
      {
        if (!disk_read(dp, h, lba << SECTOR_SHIFT, aligned_buffer, (uint32_t)this_size))
          goto ErrorExit;
      }

      overall_counter += this_size;

      if (NULL != message)
      {
        fprintf(stdout, "\r%s" CTRL_GREEN "%3.2f%%" CTRL_RESET, message, (((double)overall_counter) * 100.0) / ((double)overall_size));
        fflush(stdout);
      }

      lba += this_size >> SECTOR_SHIFT;

      if (!file_read(f, aligned_buffer2, (uint32_t)this_size))
        goto ErrorExit;

      sha3_Update(&ctx, aligned_buffer2, (size_t)this_size);

      if (INVALID_DISK_HANDLE != h)
      {
        if (memcmp(aligned_buffer, aligned_buffer2, this_size))
          goto ErrorExit;
      }

      to_be_transferred -= this_size;
    }
  }

  if (NULL != message)
  {
    fprintf(stdout, "\r%s       \r%s", message, message);
    fflush(stdout);
  }

  file_close(f,false/*do not flush*/);

  free(buffer);
  free(buffer2);

  hash = (uint8_t*)sha3_Finalize(&ctx);

  return (!memcmp(hash, orig_hash, 32)) ? true : false;
}

bool restore_backup_file(disk_ptr dp, DISK_HANDLE h, const char* backup_file, const char *message)
{
  uint8_t             sector[SECTOR_SIZE];
  FILE_HANDLE         f;
  backup_header       bh;
  backup_record       br;
  uint8_t            *buffer = NULL, *aligned_buffer;
  uint64_t            i, to_be_transferred, lba, this_size, overall_size, overall_counter = 0;
  sha3_context        ctx;
  const uint8_t* hash;
  uint8_t             orig_hash[32];

  if (NULL == dp || INVALID_DISK_HANDLE == h || NULL == backup_file)
    return false;

  memset(&ctx, 0, sizeof(ctx));
  sha3_Init(&ctx, 512);

  f = file_open(backup_file, true/*read-only*/);
  if (INVALID_FILE_HANDLE == f)
    return false;

  // read header

  if (!file_read(f, sector, SECTOR_SIZE))
  {
ErrorExit:
    file_close(f, false);
    if (NULL != buffer)
      free(buffer);
    return false;
  }

  overall_counter += SECTOR_SIZE;

  if (memcmp(&sector[0x0000], backup_signature, 16))
    goto ErrorExit;

  memcpy(orig_hash, &sector[0x0030], 32);
  memset(&sector[0x0030], 0x55, 32);

  if (!check_filler(&sector[0x0030], SECTOR_SIZE - 0x0030, 0x55))
    goto ErrorExit;

  sha3_Update(&ctx, sector, SECTOR_SIZE);

  bh.version = READ_BIG_ENDIAN32(sector, 0x0010);
  bh.first_record_ofs = READ_BIG_ENDIAN32(sector, 0x0014);

  if (BACKUP_VERSION != bh.version)
    goto ErrorExit;
  if (SECTOR_SIZE != bh.first_record_ofs)
    goto ErrorExit;

  bh.device_sectors = READ_BIG_ENDIAN64(sector, 0x0018);
  bh.num_records = READ_BIG_ENDIAN64(sector, 0x0020);
  overall_size = READ_BIG_ENDIAN64(sector, 0x0028);

  if (dp->device_sectors != bh.device_sectors)
    goto ErrorExit;

  // read and check records

  buffer = (uint8_t*)malloc(BACKUP_BUFFER_SIZE + SECTOR_SIZE);
  if (unlikely(NULL == buffer))
    goto ErrorExit;

  aligned_buffer = (uint8_t*)((((uint64_t)buffer) + (SECTOR_SIZE - 1)) & (~(SECTOR_SIZE - 1)));

  for (i = 0; i < bh.num_records; i++)
  {
    if (!file_read(f, sector, SECTOR_SIZE))
      goto ErrorExit;

    overall_counter += SECTOR_SIZE;

    sha3_Update(&ctx, sector, SECTOR_SIZE);

    if (!check_filler(&sector[0x0010], SECTOR_SIZE - 0x0010, 0xAA))
      goto ErrorExit;

    br.start_lba = READ_BIG_ENDIAN64(sector, 0x0000);
    br.num_lbas = READ_BIG_ENDIAN64(sector, 0x0008);

    if ((br.start_lba + br.num_lbas) > dp->device_sectors)
      goto ErrorExit;

    to_be_transferred = br.num_lbas << SECTOR_SHIFT;

    lba = br.start_lba;

    while (0 != to_be_transferred)
    {
      this_size = to_be_transferred > BACKUP_BUFFER_SIZE ? BACKUP_BUFFER_SIZE : to_be_transferred;

      if (!file_read(f, aligned_buffer, (uint32_t)this_size))
        goto ErrorExit;

      overall_counter += this_size;

      if (NULL != message)
      {
        fprintf(stdout, "\r%s" CTRL_GREEN "%3.2f%%" CTRL_RESET, message, (((double)overall_counter) * 100.0) / ((double)overall_size));
        fflush(stdout);
      }

      sha3_Update(&ctx, aligned_buffer, (size_t)this_size);

      if (!disk_write(dp, h, lba << SECTOR_SHIFT, aligned_buffer, (uint32_t)this_size))
        goto ErrorExit;

      lba += this_size >> SECTOR_SHIFT;

      to_be_transferred -= this_size;
    }
  }

  if (NULL != message)
  {
    fprintf(stdout, "\r%s       \r%s", message, message);
    fflush(stdout);
  }

  file_close(f, false/*do not flush*/);

  free(buffer);

  hash = (uint8_t*)sha3_Finalize(&ctx);

  return (!memcmp(hash, orig_hash, 32)) ? true : false;
}
