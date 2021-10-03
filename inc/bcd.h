/**
 * @file   bcd.h
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @brief  declaration of various structures and functions that manage
 *         the Boot Configuration Data WMI provider.
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

#ifndef _INC_BCD_H_
#define _INC_BCD_H_

#include <part-y.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _WINDOWS

/**********************************************************************************************//**
 * @fn  bool patch_device_partition_guids ( const char *src_bcd_file, const char *dst_bcd_file, const uint8_t *device_guid_src, const uint8_t *efi_partition_guid_src, const uint8_t *sysdrive_partition_guid_src, const uint8_t *winre_partition_guid_src, const uint8_t *device_guid_dst, const uint8_t *efi_partition_guid_dst, const uint8_t *sysdrive_partition_guid_dst, const uint8_t *winre_partition_guid_dst );
 *
 * @brief Patches the partition GUIDs and disk drive GUIDs in the generated BCD store just by
 *        performing a binary search and replace operation
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param src_bcd_file                The source BCD store file to be processed.
 * @param dst_bcd_file                The target BCD store file (patched) to be created.
 * @param device_guid_src             The old (MBR-style) GUID of the device.
 * @param efi_partition_guid_src      The old (MBR-style) GUID of the EFI partition (ESP)
 * @param sysdrive_partition_guid_src The old (MBR-style) GUID of the Windows system partition.
 * @param winre_partition_guid_src    The old (MBR-style) GUID of the Windows Recovery Environment
 *                                    partition (OPTIONAL, may be NULL).
 * @param device_guid_dst             The new (GPT-style) GUID of the device.
 * @param efi_partition_guid_dst      The new (GPT-style) GUID of the EFI partition (ESP)
 * @param sysdrive_partition_guid_dst The new (GPT-style) GUID of the Windows system partition.
 * @param winre_partition_guid_dst    The new (GPT-style) GUID of the Windows Recovery Environment
 *                                    partition (OPTIONAL, may be NULL).
 *
 * @returns true on success, false on error; either both Windows RE GUIDs may be NULL or != NULL.
 **************************************************************************************************/

bool patch_device_partition_guids ( const char    *src_bcd_file,
                                    const char    *dst_bcd_file,
                                    const uint8_t *device_guid_src,
                                    const uint8_t *efi_partition_guid_src,
                                    const uint8_t *sysdrive_partition_guid_src,
                                    const uint8_t *winre_partition_guid_src,
                                    const uint8_t *device_guid_dst,
                                    const uint8_t *efi_partition_guid_dst,
                                    const uint8_t *sysdrive_partition_guid_dst,
                                    const uint8_t *winre_partition_guid_dst );

#else

typedef struct _bcdwmi                  bcdwmi, * bcdwmi_ptr;
typedef struct _bcdstore                bcdstore, * bcdstore_ptr;

struct _bcdwmi
{
  IWbemLocator                         *pLoc;
  IWbemServices                        *pSvc;
  IWbemClassObject                     *pBcdStoreClass;
  IWbemClassObject                     *pBcdObjectClass;
};

struct _bcdstore
{
  bcdwmi_ptr                            bwp;
  IWbemClassObject                     *pBcdStore;
  VARIANT                               this_pointer;
};

/**********************************************************************************************//**
 * @fn  bool bcd_connect( bcdwmi_ptr bwp );
 *
 * @brief Connects to the BCD WMI stuff.
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param bwp pointer to an uninitialized structure on entry; on success, this is filled with meaningful data
 *
 * @returns true on success, false on error.
 **************************************************************************************************/

bool bcd_connect( bcdwmi_ptr bwp );

/**********************************************************************************************//**
 * @fn  void bcd_disconnect( bcdwmi_ptr bwp );
 *
 * @brief Disconnects from the BCD WMI provider and frees all resources associated with it.
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param bwp pointer to a BCD WMI structure. Everything IN the structure is cleaned up, not the
 *            pointer itself (is not freed).
 **************************************************************************************************/

void bcd_disconnect( bcdwmi_ptr bwp );

/**********************************************************************************************//**
 * @fn  bool bcd_openstore(bcdwmi_ptr bwp, const char* store_filename, bcdstore_ptr bsp);
 *
 * @brief Opens an existing BCD store
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param bwp             pointer to the BCD WMI structure
 * @param store_filename  filename of the store file. Pass NULL here to open the system store,
 *                        which can be dangerous if you want to modify things (machine may not boot
 *                        anymore, keep this in mind!)
 * @param bsp             pointer to a bcdstore structure (uninitialized on entry). On success, this
 *                        is filled with meaningful pointers and data.
 *
 * @returns Null if it fails, else a pointer to an IWbemClassObject.
 **************************************************************************************************/

bool bcd_openstore(bcdwmi_ptr bwp, const char* store_filename, bcdstore_ptr bsp);

/**********************************************************************************************//**
 * @fn  void bcd_closestore(bcdstore_ptr bsp);
 *
 * @brief Frees all resources associated with a BCD store.
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param bsp pointer to the BCD store structure to be freed. The pointer itself is not freed, only
 *            the data and pointers in the structure.
 **************************************************************************************************/

void bcd_closestore(bcdstore_ptr bsp);

/**********************************************************************************************//**
 * @fn  bool bcd_createstore(bcdwmi_ptr bwp, const char* store_filename, bcdstore_ptr bsp);
 *
 * @brief Creates a new BCD store
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param bwp             pointer to the BCD WMI structure
 * @param store_filename  filename of the store file to be create (!= NULL)
 * @param bsp             pointer to a bcdstore structure (uninitialized on entry). On success, this
 *                        is filled with meaningful pointers and data.
 *
 * @returns Null if it fails, else a pointer to an IWbemClassObject (which is the new BCD store).
 **************************************************************************************************/

bool bcd_createstore(bcdwmi_ptr bwp, const char* store_filename, bcdstore_ptr bsp);

/**********************************************************************************************//**
 * @fn  void bcd_debug_dump_objects(bcdstore_ptr bsp);
 *
 * @brief For debugging only; this is sometimes THE ONLY WAY to find out what is in this 'mess' of
 *        BCD store...
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param bsp pointer to created / opened BCD store.
 **************************************************************************************************/

void bcd_debug_dump_objects(bcdstore_ptr bsp);

/**********************************************************************************************//**
 * @fn  bool bcd_create_objects_and_entries(bcdstore_ptr bsp, uint32_t efi_partition_no, uint32_t windows_partition_no, char windows_drive_letter, uint32_t recovery_partition, const char *locale );
 *
 * @brief Creates all BCD objects and entries based on the parameters of this function
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param bsp                   pointer to the BCD store (must have been created using bcd_createstore)
 * @param efi_partition_no      the GPT EFI partition no., which is always 1
 * @param windows_partition_no  the GPT partition no. of the Windows system drive
 * @param windows_drive_letter  the drive letter of the Windows system drive
 * @param recovery_partition    0 (no recovery) or the GPT partition no. of the Windows RE
 * @param locale                locale string, e.g. "en-US", "de-DE", etc.
 *
 * @returns True if it succeeds, false if it fails.
 **************************************************************************************************/

bool bcd_create_objects_and_entries(bcdstore_ptr bsp,
                        uint32_t efi_partition_no,            
                        uint32_t windows_partition_no,        
                        char windows_drive_letter,
                        uint32_t recovery_partition,
                        const char *locale );

#endif // _WINDOWS

#ifdef __cplusplus
}
#endif

#endif // _INC_BCD_H_
