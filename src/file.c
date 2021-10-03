/**
 * @file   file.c
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @brief  implementation of functions that manage (bare metal)
 *         file operations.
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

#ifdef _WINDOWS

FILE_HANDLE file_open(const char* filename, bool read_only)
{
  return read_only ?
    CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL/* | FILE_FLAG_NO_BUFFERING*/, NULL) :
    CreateFileA(filename, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL/* | FILE_FLAG_WRITE_THROUGH | FILE_FLAG_NO_BUFFERING*/, NULL);
}

void file_close(FILE_HANDLE f, bool do_flush)
{
  if (INVALID_FILE_HANDLE != f)
  {
    if (do_flush)
      FlushFileBuffers(f);
    CloseHandle(f);
  }
}

bool file_read(FILE_HANDLE f, void* buffer, uint32_t size)
{
  DWORD dwRead = 0;

  if ((!(ReadFile(f, buffer, size, &dwRead, NULL))) || (dwRead != size))
    return false;

  return true;
}

bool file_write(FILE_HANDLE f, const void* buffer, uint32_t size)
{
  DWORD dwWritten = 0;

  if ((!(WriteFile(f, buffer, size, &dwWritten, NULL))) || (dwWritten != size))
    return false;

  return true;
}

bool file_setpointer(FILE_HANDLE f, uint64_t pos)
{
  LARGE_INTEGER       distToMove, newFp;

  distToMove.QuadPart = pos;
  if (!SetFilePointerEx(f, distToMove, &newFp, FILE_BEGIN))
    return false;
  if (newFp.QuadPart != pos)
    return false;

  return true;
}

uint64_t file_get_size(FILE_HANDLE f)
{
  LARGE_INTEGER       fs;

  if (!GetFileSizeEx(f, &fs))
    return 0;

  return (uint64_t)fs.QuadPart;
}

#else // LINUX

FILE_HANDLE file_open(const char* filename, bool read_only)
{
  return read_only ? open(filename, O_RDONLY) : open(filename, O_CREAT | O_RDWR);
}

void file_close(FILE_HANDLE f, bool do_flush)
{
  if (INVALID_FILE_HANDLE != f)
  {
    if (do_flush)
      syncfs(f);
    close(f);
  }
}

bool file_read(FILE_HANDLE f, void* buffer, uint32_t size)
{
  int read_bytes = read(f, buffer, size);

  return ((-1 == read_bytes) || (((uint32_t)read_bytes) != size)) ? false : true;
}

bool file_write(FILE_HANDLE f, const void* buffer, uint32_t size)
{
  int written_bytes = write(f, buffer, size);

  return ((-1 == written_bytes) || (((uint32_t)written_bytes) != size)) ? false : true;
}

bool file_setpointer(FILE_HANDLE f, uint64_t pos)
{
  if (((long)pos) != lseek(f, (long)pos, SEEK_SET))
    return false;

  return true;
}

uint64_t file_get_size(FILE_HANDLE f)
{
  uint64_t size = (uint64_t)lseek(f, 0, SEEK_END);

  lseek(f, 0, SEEK_SET);

  return size;
}

#endif // !_WINDOWS

static uint8_t copy_buffer[1 << 20];

bool file_copy(const char* src_name, const char* dst_name)
{
  FILE_HANDLE       src = file_open(src_name, true/*read/only*/);
  FILE_HANDLE       dst = file_open(dst_name, false/*read/write*/);
  uint64_t          file_size, to_be_copied;

  if (INVALID_FILE_HANDLE == src || INVALID_FILE_HANDLE == dst)
  {
ErrorExit:
    if (INVALID_FILE_HANDLE != dst)
      file_close(dst, false);
    if (INVALID_FILE_HANDLE != src)
      file_close(src, false);
    (void)unlink(dst_name);
    return false;
  }

  file_size = file_get_size(src);

  while (0 != file_size)
  {
    to_be_copied = file_size > sizeof(copy_buffer) ? sizeof(copy_buffer) : file_size;
    if (!file_read(src, copy_buffer, (uint32_t)to_be_copied))
      goto ErrorExit;
    if (!file_write(dst, copy_buffer, (uint32_t)to_be_copied))
      goto ErrorExit;
    file_size -= to_be_copied;
  }

  file_close(dst, true);
  file_close(src, false);

  return true;
}
