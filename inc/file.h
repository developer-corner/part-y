/**
 * @file   file.h
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @brief  declaration of various structures and functions that manage
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

#ifndef _INC_FILE_H_
#define _INC_FILE_H_

#include <part-y.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WINDOWS

typedef HANDLE FILE_HANDLE;
#define INVALID_FILE_HANDLE         INVALID_HANDLE_VALUE

#else

typedef int FILE_HANDLE;
#define INVALID_FILE_HANDLE         -1

#endif

/**********************************************************************************************//**
 * @fn  FILE_HANDLE file_open(const char* filename, bool read_only);
 *
 * @brief Opens a file for just reading or reading+writing, respectively.
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param filename  Filename of the file.
 * @param read_only true to open it read-only.
 *
 * @returns INVALID_FILE_HANDLE or the handle to the opened (or created) file.
 **************************************************************************************************/

FILE_HANDLE file_open(const char* filename, bool read_only);

/**********************************************************************************************//**
 * @fn  void file_close(FILE_HANDLE f, bool do_flush);
 *
 * @brief Closes the file
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param f         file handle
 * @param do_flush  true to flush the file before closing.
 **************************************************************************************************/

void file_close(FILE_HANDLE f, bool do_flush);

/**********************************************************************************************//**
 * @fn  bool file_read(FILE_HANDLE f, void* buffer, uint32_t size);
 *
 * @brief Reads data from a file
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param           f       file handle
 * @param [in,out]  buffer  pointer to data buffer
 * @param           size    number of byte to be read
 *
 * @returns true (success), false otherwise. The current file pointer is used to carry out the op.
 **************************************************************************************************/

bool file_read(FILE_HANDLE f, void* buffer, uint32_t size);

/**********************************************************************************************//**
 * @fn  bool file_write(FILE_HANDLE f, const void* buffer, uint32_t size);
 *
 * @brief Writes data into a file
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param f       file handle
 * @param buffer  pointer to data buffer containing the data to be written
 * @param size    size of the data in bytes
 *
 * @returns true (success), false otherwise. The current file pointer is used to carry out the op.
 **************************************************************************************************/

bool file_write(FILE_HANDLE f, const void* buffer, uint32_t size);

/**********************************************************************************************//**
 * @fn  bool file_setpointer(FILE_HANDLE f, uint64_t pos);
 *
 * @brief Sets the file pointer to a specific position (always from the beginning of the file)
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param f   file handle
 * @param pos file positition to be seeked
 *
 * @returns true on success, false otherwise.
 **************************************************************************************************/

bool file_setpointer(FILE_HANDLE f, uint64_t pos);

/**********************************************************************************************//**
 * @fn  uint64_t file_get_size(FILE_HANDLE f);
 *
 * @brief Retrieves the file size
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param f file handle
 *
 * @returns size of the file in bytes (64bit).
 **************************************************************************************************/

uint64_t file_get_size(FILE_HANDLE f);

/**********************************************************************************************//**
 * @fn  bool file_copy(const char* src_name, const char* dst_name);
 *
 * @brief Copies a file, overwrites the target, flushes the target concluding the copy operation
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param src_name  file name of the source file
 * @param dst_name  file name of the destination file
 *
 * @returns true on success, false otherwise
 **************************************************************************************************/

bool file_copy(const char* src_name, const char* dst_name);

#ifdef __cplusplus
}
#endif

#endif // _INC_FILE_H_
