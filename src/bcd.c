/**
 * @file   bcd.c
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @brief  implementation of the BCD store creation using the Microsoft WMI BCD provider.
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

#ifndef _WINDOWS

extern void format_guid(char* buffer, const uint8_t* guid, bool just_big_endian);

bool patch_device_partition_guids ( const char    *src_bcd_file,
                                    const char    *dst_bcd_file,
                                    const uint8_t *device_guid_src,
                                    const uint8_t *efi_partition_guid_src,
                                    const uint8_t *sysdrive_partition_guid_src,
                                    const uint8_t *winre_partition_guid_src,
                                    const uint8_t *device_guid_dst,
                                    const uint8_t *efi_partition_guid_dst,
                                    const uint8_t *sysdrive_partition_guid_dst,
                                    const uint8_t *winre_partition_guid_dst )
{
  FILE_HANDLE             f;
  uint64_t                fsize;
  uint8_t                *bcd_data = NULL;
  bool                    have_patched_efi = false;
  bool                    have_patched_sysdrive = false;
  bool                    have_patched_winre = false;
  static const uint8_t    patch_sig[16] = { 0x06,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x48,0x00,0x00,0x00,0x00,0x00,0x00,0x00 };
  static const uint8_t    zero_sig[16]  = { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 };
  static const uint8_t    mbr_sig[8]    = { 0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x00 };
  uint8_t                 work_guid[16];
  uint32_t                i;

  if (unlikely(NULL == dst_bcd_file || NULL == src_bcd_file || NULL == device_guid_src ||
               NULL == efi_partition_guid_src || NULL == sysdrive_partition_guid_src ||
               NULL == device_guid_dst || NULL == efi_partition_guid_dst ||
               NULL == sysdrive_partition_guid_dst ))
    return false;

  if (unlikely((NULL == winre_partition_guid_src && NULL != winre_partition_guid_dst) ||
               (NULL != winre_partition_guid_src && NULL == winre_partition_guid_dst)))
    return false;

  if ((NULL != winre_partition_guid_src) && (!memcmp(winre_partition_guid_src, zero_sig, 16)))
    winre_partition_guid_src = winre_partition_guid_dst = NULL;

  f = file_open(src_bcd_file, true/*read-only*/);

  if (INVALID_FILE_HANDLE == f)
    return false;

  fsize = file_get_size(f);
  if (unlikely( ((int64_t)fsize) <= 0 ))
  {
    file_close(f,false);
    return false;
  }

  bcd_data = (uint8_t*)malloc(fsize);
  if (unlikely(NULL == bcd_data))
  {
    file_close(f, false);
    return false;
  }

  if (!file_read(f, bcd_data,(uint32_t)fsize))
  {
    free(bcd_data);
    file_close(f, false);
    return false;
  }

  file_close(f, false);

  // look for all 0x48 size byte sequences:
  // uint64_t 0x6
  // uint64_t 0x48
  // partition GUID
  // uint64_t 0x0
  // disk GUID
  // 16 zeros

  for (i=0;i<=((uint32_t)fsize)-0x48;i++)
  {
    if (!memcmp(&bcd_data[i], patch_sig, 16))
    {
      if (!memcmp(&bcd_data[i+0x20], zero_sig, 8))
        memcpy(work_guid, &bcd_data[i+0x10], 16);
      else
      if (!memcmp(&bcd_data[i+0x20], mbr_sig, 8))
      {
        memset(work_guid, 0, 16);
        work_guid[0]  = bcd_data[i + 0x28];
        work_guid[1]  = bcd_data[i + 0x29];
        work_guid[2]  = bcd_data[i + 0x2A];
        work_guid[3]  = bcd_data[i + 0x2B];

        work_guid[8]  = bcd_data[i + 0x10];
        work_guid[9]  = bcd_data[i + 0x11];
        work_guid[10] = bcd_data[i + 0x12];
        work_guid[11] = bcd_data[i + 0x13];
        work_guid[12] = bcd_data[i + 0x14];
        work_guid[13] = bcd_data[i + 0x15];
        work_guid[14] = bcd_data[i + 0x16];
        work_guid[15] = bcd_data[i + 0x17];
      }
      else
        return false;

      if (!memcmp(&bcd_data[i+0x38], zero_sig, 16))
      {
        if (memcmp(&bcd_data[i + 0x28], device_guid_src, 16)) // this is an unknown device GUID -> abort
          return false;

        if (!memcmp(work_guid, efi_partition_guid_src, 16)) // EFI partition
        {
          have_patched_efi = true;
          memcpy(&bcd_data[i+0x28], device_guid_dst, 16);
          memcpy(&bcd_data[i+0x10], efi_partition_guid_dst, 16);
          bcd_data[i+0x24] = 0x00; // set type to GPT
        }
        else
        if (!memcmp(work_guid, sysdrive_partition_guid_src, 16)) // windows system drive partition
        {
          have_patched_sysdrive = true;
          memcpy(&bcd_data[i+0x28], device_guid_dst, 16);
          memcpy(&bcd_data[i+0x10], sysdrive_partition_guid_dst, 16);
          bcd_data[i+0x24] = 0x00; // set type to GPT
        }
        else
        if ((NULL != winre_partition_guid_src) && (!memcmp(work_guid, winre_partition_guid_src, 16))) // windows recovery environment partition
        {
          have_patched_winre = true;
          memcpy(&bcd_data[i+0x28], device_guid_dst, 16);
          memcpy(&bcd_data[i+0x10], winre_partition_guid_dst, 16);
          bcd_data[i+0x24] = 0x00; // set type to GPT
        }
        else
          return false; // invalid src GUID, bail out
      }
    }
  }

  if (NULL != winre_partition_guid_src)
  {
    if (!have_patched_winre)
    {
      free(bcd_data);
      return false;
    }
  }

  if (!(have_patched_efi && have_patched_sysdrive))
  {
    free(bcd_data);
    return false;
  }

  f = file_open(dst_bcd_file, false/*read-wrie*/);

  if (INVALID_FILE_HANDLE == f)
  {
    free(bcd_data);
    return false;
  }

  if (!file_write(f, bcd_data,(uint32_t)fsize))
  {
    free(bcd_data);
    file_close(f,false);
    (void)unlink(dst_bcd_file);
    return false;
  }

  file_close(fsize, true/*do flush*/);

  free(bcd_data);

  return true;
}

#else

static void DumpBcdElement(const wchar_t* prefix, IWbemClassObject* pBcdElement);

// TAKEN FROM https://github.com/microsoft/Windows-classic-samples/blob/main/Samples/Win7Samples/winbase/bootconfigurationdata/bcdsamplelib/Constants.cs
// 
// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved

static const uint32_t BCDE_VISTA_OS_ENTRY = 0x10200003;
static const uint32_t BCDE_LEGACY_OS_ENTRY = 0x10300006;

typedef enum BCDE_IMAGE_TYPE
{
	FIRMWARE_APPLICATION             = 0x1,
	BOOT_APPLICATION                 = 0x2,
	LEGACY_LOADER                    = 0x3,
	REALMODE_CODE                    = 0x4
} BCDE_IMAGE_TYPE;

typedef enum BCDE_APPLICATION_TYPE
{
  NO_APP_TYPE                   = 0x00000,
	FIRMWARE_BOOT_MANAGER         = 0x00001,
	WINDOWS_BOOT_MANAGER          = 0x00002,
	WINDOWS_BOOT_LOADER           = 0x00003,
	WINDOWS_RESUME_APPLICATION    = 0x00004,
	MEMORY_TESTER                 = 0x00005,
	LEGACY_NTLDR                  = 0x00006,
	LEGACY_SETUPLDR               = 0x00007,
	BOOT_SECTOR                   = 0x00008,
	STARTUP_MODULE                = 0x00009,
  GENERIC_APPLICATION           = 0x0000A,      ///< was missing
	RESERVED                      = 0xFFFFF
} BCDE_APPLICATION_TYPE;

typedef enum _BCDE_DISPLAY_MESSAGE
{
  DisplayMessage_Recovery = 3,
} BCDE_DISPLAY_MESSAGE;

// Known GUIDS
static const char GUID_WINDOWS_BOOTMGR[64]         = "{9dea862c-5cdd-4e70-acc1-f32b344d4795}";
static const char GUID_DEBUGGER_SETTINGS_GROUP[64] = "{4636856e-540f-4170-a130-a84776f4c654}";
static const char GUID_CURRENT_BOOT_ENTRY[64]      = "{fa926493-6f1c-4193-a414-58f0b2456d1e}";
static const char GUID_WINDOWS_LEGACY_NTLDR[64]    = "{466f5a88-0af2-4f76-9038-095b170dc21c}";

typedef enum BCDE_OBJECT_TYPE
{
  APPLICATION = 0x1,
  INHERITED   = 0x2,
  DEVICE      = 0x3
} BCDE_OBJECT_TYPE;

#define MAKE_BCD_OBJECT_NUMBER(imageType, applicationType)   ( (((uint32_t)APPLICATION)<<28) | ((((uint32_t)imageType)&0x0F)<<20) | (((uint32_t)applicationType)&0x000FFFFF) )

// END OF TAKEN FROM https://github.com/microsoft/Windows-classic-samples/blob/main/Samples/Win7Samples/winbase/bootconfigurationdata/bcdsamplelib/Constants.cs

// TAKEN OR DERIVED FROM MSDN LIBRARY DOCUMENATION, google for it...

typedef enum BcdBootMgrElementTypes 
{ 
  BcdBootMgrObjectList_DisplayOrder            = 0x24000001,
  BcdBootMgrObjectList_BootSequence            = 0x24000002,
  BcdBootMgrObject_DefaultObject               = 0x23000003,
  BcdBootMgrInteger_Timeout                    = 0x25000004,
  BcdBootMgrBoolean_AttemptResume              = 0x26000005,
  BcdBootMgrObject_ResumeObject                = 0x23000006,
  BcdBootMgrObjectList_ToolsDisplayOrder       = 0x24000010,
  BcdBootMgrBoolean_DisplayBootMenu            = 0x26000020,
  BcdBootMgrBoolean_NoErrorDisplay             = 0x26000021,
  BcdBootMgrDevice_BcdDevice                   = 0x21000022,
  BcdBootMgrString_BcdFilePath                 = 0x22000023,
  BcdBootMgrBoolean_ProcessCustomActionsFirst  = 0x26000028,
  BcdBootMgrIntegerList_CustomActionsList      = 0x27000030,
  BcdBootMgrBoolean_PersistBootSequence        = 0x26000031
} BcdBootMgrElementTypes;

typedef enum BcdDeviceObjectElementTypes 
{ 
  BcdDeviceInteger_RamdiskImageOffset            = 0x35000001,
  BcdDeviceInteger_TftpClientPort                = 0x35000002,
  BcdDeviceInteger_SdiDevice                     = 0x31000003,
  BcdDeviceInteger_SdiPath                       = 0x32000004,
  BcdDeviceInteger_RamdiskImageLength            = 0x35000005,
  BcdDeviceBoolean_RamdiskExportAsCd             = 0x36000006,
  BcdDeviceInteger_RamdiskTftpBlockSize          = 0x36000007,
  BcdDeviceInteger_RamdiskTftpWindowSize         = 0x36000008,
  BcdDeviceBoolean_RamdiskMulticastEnabled       = 0x36000009,
  BcdDeviceBoolean_RamdiskMulticastTftpFallback  = 0x3600000A,
  BcdDeviceBoolean_RamdiskTftpVarWindow          = 0x3600000B
} BcdDeviceObjectElementTypes;

typedef enum BcdLibrary_DebuggerType 
{ 
  DebuggerSerial  = 0,
  Debugger1394    = 1,
  DebuggerUsb     = 2,
  DebuggerNet     = 3,
  DebuggerLocal   = 4   // was missing
} BcdLibrary_DebuggerType;

typedef enum BcdLibrary_SafeBoot 
{ 
  SafemodeMinimal   = 0,
  SafemodeNetwork   = 1,
  SafemodeDsRepair  = 2
} BcdLibrary_SafeBoot;

typedef enum BcdLibraryElementTypes 
{ 
  BcdLibraryDevice_ApplicationDevice                  = 0x11000001,
  BcdLibraryString_ApplicationPath                    = 0x12000002,
  BcdLibraryString_Description                        = 0x12000004,
  BcdLibraryString_PreferredLocale                    = 0x12000005,
  BcdLibraryObjectList_InheritedObjects               = 0x14000006,
  BcdLibraryInteger_TruncatePhysicalMemory            = 0x15000007,
  BcdLibraryObjectList_RecoverySequence               = 0x14000008,
  BcdLibraryBoolean_AutoRecoveryEnabled               = 0x16000009,
  BcdLibraryIntegerList_BadMemoryList                 = 0x1700000a,
  BcdLibraryBoolean_AllowBadMemoryAccess              = 0x1600000b,
  BcdLibraryInteger_FirstMegabytePolicy               = 0x1500000c,
  BcdLibraryInteger_RelocatePhysicalMemory            = 0x1500000D,
  BcdLibraryInteger_AvoidLowPhysicalMemory            = 0x1500000E,
  BcdLibraryBoolean_DebuggerEnabled                   = 0x16000010,
  BcdLibraryInteger_DebuggerType                      = 0x15000011,
  BcdLibraryInteger_SerialDebuggerPortAddress         = 0x15000012,
  BcdLibraryInteger_SerialDebuggerPort                = 0x15000013,
  BcdLibraryInteger_SerialDebuggerBaudRate            = 0x15000014,
  BcdLibraryInteger_1394DebuggerChannel               = 0x15000015,
  BcdLibraryString_UsbDebuggerTargetName              = 0x12000016,
  BcdLibraryBoolean_DebuggerIgnoreUsermodeExceptions  = 0x16000017,
  BcdLibraryInteger_DebuggerStartPolicy               = 0x15000018,
  BcdLibraryString_DebuggerBusParameters              = 0x12000019,
  BcdLibraryInteger_DebuggerNetHostIP                 = 0x1500001A,
  BcdLibraryInteger_DebuggerNetPort                   = 0x1500001B,
  BcdLibraryBoolean_DebuggerNetDhcp                   = 0x1600001C,
  BcdLibraryString_DebuggerNetKey                     = 0x1200001D,
  BcdLibraryBoolean_EmsEnabled                        = 0x16000020,
  BcdLibraryInteger_EmsPort                           = 0x15000022,
  BcdLibraryInteger_EmsBaudRate                       = 0x15000023,
  BcdLibraryString_LoadOptionsString                  = 0x12000030,
  BcdLibraryBoolean_DisplayAdvancedOptions            = 0x16000040,
  BcdLibraryBoolean_DisplayOptionsEdit                = 0x16000041,
  BcdLibraryDevice_BsdLogDevice                       = 0x11000043,
  BcdLibraryString_BsdLogPath                         = 0x12000044,
  BcdLibraryBoolean_GraphicsModeDisabled              = 0x16000046,
  BcdLibraryInteger_ConfigAccessPolicy                = 0x15000047,
  BcdLibraryBoolean_DisableIntegrityChecks            = 0x16000048,
  BcdLibraryBoolean_AllowPrereleaseSignatures         = 0x16000049,
  BcdLibraryString_FontPath                           = 0x1200004A,
  BcdLibraryInteger_SiPolicy                          = 0x1500004B,
  BcdLibraryInteger_FveBandId                         = 0x1500004C,
  BcdLibraryBoolean_ConsoleExtendedInput              = 0x16000050,
  BcdLibraryInteger_GraphicsResolution                = 0x15000052,
  BcdLibraryBoolean_RestartOnFailure                  = 0x16000053,
  BcdLibraryBoolean_GraphicsForceHighestMode          = 0x16000054,
  BcdLibraryBoolean_IsolatedExecutionContext          = 0x16000060,
  BcdLibraryBoolean_BootUxDisable                     = 0x1600006C,
  BcdLibraryBoolean_BootShutdownDisabled              = 0x16000074,
  BcdLibraryIntegerList_AllowedInMemorySettings       = 0x17000077,
  BcdLibraryBoolean_ForceFipsCrypto                   = 0x16000079,

  BcdLibraryInteger_DisplayMessage                    = 0x15000065, // 64bit integer (5)
  BcdLibraryInteger_DisplayMessageOverride            = 0x15000066, // 64bit integer (5)
  BcdLibaryString_SystemRoot                          = 0x22000002, // null-terminated Unicode String
} BcdLibraryElementTypes;

typedef enum BcdMemDiagElementTypes 
{ 
  BcdMemDiagInteger_PassCount     = 0x25000001,
  BcdMemDiagInteger_FailureCount  = 0x25000003
} BcdMemDiagElementTypes;

typedef enum BcdOSLoader_NxPolicy 
{ 
  NxPolicyOptIn      = 0,
  NxPolicyOptOut     = 1,
  NxPolicyAlwaysOff  = 2,
  NxPolicyAlwaysOn   = 3
} BcdOSLoader_NxPolicy;

typedef enum BcdOSLoader_PAEPolicy 
{ 
  PaePolicyDefault       = 0,
  PaePolicyForceEnable   = 1,
  PaePolicyForceDisable  = 2
} BcdOSLoader_PAEPolicy;

typedef enum BcdOSLoaderElementTypes 
{ 
  BcdOSLoaderDevice_OSDevice                        = 0x21000001,
  BcdOSLoaderString_SystemRoot                      = 0x22000002,
  BcdOSLoaderObject_AssociatedResumeObject          = 0x23000003,
  BcdOSLoaderBoolean_DetectKernelAndHal             = 0x26000010,
  BcdOSLoaderString_KernelPath                      = 0x22000011,
  BcdOSLoaderString_HalPath                         = 0x22000012,
  BcdOSLoaderString_DbgTransportPath                = 0x22000013,
  BcdOSLoaderInteger_NxPolicy                       = 0x25000020,
  BcdOSLoaderInteger_PAEPolicy                      = 0x25000021,
  BcdOSLoaderBoolean_WinPEMode                      = 0x26000022,
  BcdOSLoaderBoolean_DisableCrashAutoReboot         = 0x26000024,
  BcdOSLoaderBoolean_UseLastGoodSettings            = 0x26000025,
  BcdOSLoaderBoolean_AllowPrereleaseSignatures      = 0x26000027,
  BcdOSLoaderBoolean_NoLowMemory                    = 0x26000030,
  BcdOSLoaderInteger_RemoveMemory                   = 0x25000031,
  BcdOSLoaderInteger_IncreaseUserVa                 = 0x25000032,
  BcdOSLoaderBoolean_UseVgaDriver                   = 0x26000040,
  BcdOSLoaderBoolean_DisableBootDisplay             = 0x26000041,
  BcdOSLoaderBoolean_DisableVesaBios                = 0x26000042,
  BcdOSLoaderBoolean_DisableVgaMode                 = 0x26000043,
  BcdOSLoaderInteger_ClusterModeAddressing          = 0x25000050,
  BcdOSLoaderBoolean_UsePhysicalDestination         = 0x26000051,
  BcdOSLoaderInteger_RestrictApicCluster            = 0x25000052,
  BcdOSLoaderBoolean_UseLegacyApicMode              = 0x26000054,
  BcdOSLoaderInteger_X2ApicPolicy                   = 0x25000055,
  BcdOSLoaderBoolean_UseBootProcessorOnly           = 0x26000060,
  BcdOSLoaderInteger_NumberOfProcessors             = 0x25000061,
  BcdOSLoaderBoolean_ForceMaximumProcessors         = 0x26000062,
  BcdOSLoaderBoolean_ProcessorConfigurationFlags    = 0x25000063,
  BcdOSLoaderBoolean_MaximizeGroupsCreated          = 0x26000064,
  BcdOSLoaderBoolean_ForceGroupAwareness            = 0x26000065,
  BcdOSLoaderInteger_GroupSize                      = 0x25000066,
  BcdOSLoaderInteger_UseFirmwarePciSettings         = 0x26000070,
  BcdOSLoaderInteger_MsiPolicy                      = 0x25000071,
  BcdOSLoaderInteger_SafeBoot                       = 0x25000080,
  BcdOSLoaderBoolean_SafeBootAlternateShell         = 0x26000081,
  BcdOSLoaderBoolean_BootLogInitialization          = 0x26000090,
  BcdOSLoaderBoolean_VerboseObjectLoadMode          = 0x26000091,
  BcdOSLoaderBoolean_KernelDebuggerEnabled          = 0x260000a0,
  BcdOSLoaderBoolean_DebuggerHalBreakpoint          = 0x260000a1,
  BcdOSLoaderBoolean_UsePlatformClock               = 0x260000A2,
  BcdOSLoaderBoolean_ForceLegacyPlatform            = 0x260000A3,
  BcdOSLoaderInteger_TscSyncPolicy                  = 0x250000A6,
  BcdOSLoaderBoolean_EmsEnabled                     = 0x260000b0,
  BcdOSLoaderInteger_DriverLoadFailurePolicy        = 0x250000c1,
  BcdOSLoaderInteger_BootMenuPolicy                 = 0x250000C2,
  BcdOSLoaderBoolean_AdvancedOptionsOneTime         = 0x260000C3,
  BcdOSLoaderInteger_BootStatusPolicy               = 0x250000E0,
  BcdOSLoaderBoolean_DisableElamDrivers             = 0x260000E1,
  BcdOSLoaderInteger_HypervisorLaunchType           = 0x250000F0,
  BcdOSLoaderBoolean_HypervisorDebuggerEnabled      = 0x260000F2,
  BcdOSLoaderInteger_HypervisorDebuggerType         = 0x250000F3,
  BcdOSLoaderInteger_HypervisorDebuggerPortNumber   = 0x250000F4,
  BcdOSLoaderInteger_HypervisorDebuggerBaudrate     = 0x250000F5,
  BcdOSLoaderInteger_HypervisorDebugger1394Channel  = 0x250000F6,
  BcdOSLoaderInteger_BootUxPolicy                   = 0x250000F7,
  BcdOSLoaderString_HypervisorDebuggerBusParams     = 0x220000F9,
  BcdOSLoaderInteger_HypervisorNumProc              = 0x250000FA,
  BcdOSLoaderInteger_HypervisorRootProcPerNode      = 0x250000FB,
  BcdOSLoaderBoolean_HypervisorUseLargeVTlb         = 0x260000FC,
  BcdOSLoaderInteger_HypervisorDebuggerNetHostIp    = 0x250000FD,
  BcdOSLoaderInteger_HypervisorDebuggerNetHostPort  = 0x250000FE,
  BcdOSLoaderInteger_TpmBootEntropyPolicy           = 0x25000100,
  BcdOSLoaderString_HypervisorDebuggerNetKey        = 0x22000110,
  BcdOSLoaderBoolean_HypervisorDebuggerNetDhcp      = 0x26000114,
  BcdOSLoaderInteger_HypervisorIommuPolicy          = 0x25000115,
  BcdOSLoaderInteger_XSaveDisable                   = 0x2500012b
} BcdOSLoaderElementTypes;

typedef enum _BcdResumeElementTypes 
{ 
  Reserved1                            = 0x21000001,
  Reserved2                            = 0x22000002,
  BcdResumeBoolean_UseCustomSettings   = 0x26000003,
  BcdResumeDevice_AssociatedOsDevice   = 0x21000005,
  BcdResumeBoolean_DebugOptionEnabled  = 0x26000006,
  BcdResumeInteger_BootMenuPolicy      = 0x25000008
} BcdResumeElementTypes;

typedef enum _DeviceTypes
{
  BootDevice                = 1,
  PartitionDevice           = 2,
  FileDevice                = 3,
  RamdiskDevice             = 4,
  UnknownDevice             = 5,
  QualifiedPartitionDevice  = 6,
  LocateDevice              = 7,
  LocateExDevice            = 8

} DeviceTypes;

// END OF TAKEN OR DERIVED FROM MSDN LIBRARY DOCUMENATION, google for it...

bool bcd_connect(bcdwmi_ptr bwp)
{
  HRESULT hr;
  BSTR        namespace, classname, classname2;

  if (NULL == bwp)
    return false;

  namespace  = SysAllocString(L"root\\wmi");
  classname  = SysAllocString(L"BcdStore");
  classname2 = SysAllocString(L"BcdObject");

  memset(bwp, 0, sizeof(bcdwmi));

  hr = CoCreateInstance(&CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, &IID_IWbemLocator, (LPVOID*)&bwp->pLoc);

  if (FAILED(hr))
  {
  ErrorExit:
    if (NULL != bwp->pBcdStoreClass)
      bwp->pBcdStoreClass->lpVtbl->Release(bwp->pBcdStoreClass);
    if (NULL != bwp->pBcdObjectClass)
      bwp->pBcdObjectClass->lpVtbl->Release(bwp->pBcdObjectClass);
    if (NULL != bwp->pSvc)
      bwp->pSvc->lpVtbl->Release(bwp->pSvc);
    if (NULL!= bwp->pLoc)
      bwp->pLoc->lpVtbl->Release(bwp->pLoc);
    memset(bwp, 0, sizeof(bcdwmi));
    SysFreeString(classname);
    SysFreeString(classname2);
    SysFreeString(namespace);
    return false;
  }

  hr = bwp->pLoc->lpVtbl->ConnectServer(bwp->pLoc, namespace, NULL, NULL, NULL, 0, NULL, NULL, &bwp->pSvc);
  if (FAILED(hr))
    goto ErrorExit;

  hr = CoSetProxyBlanket((IUnknown*) bwp->pSvc,RPC_C_AUTHN_WINNT,RPC_C_AUTHZ_NONE,NULL,RPC_C_AUTHN_LEVEL_CALL,RPC_C_IMP_LEVEL_IMPERSONATE,NULL,EOAC_NONE);
  if (FAILED(hr))
    goto ErrorExit;

  hr = bwp->pSvc->lpVtbl->GetObject(bwp->pSvc, classname, 0, NULL, &bwp->pBcdStoreClass, NULL);
  if (FAILED(hr))
    goto ErrorExit;

  hr = bwp->pSvc->lpVtbl->GetObject(bwp->pSvc, classname2, 0, NULL, &bwp->pBcdObjectClass, NULL);
  if (FAILED(hr))
    goto ErrorExit;

  SysFreeString(classname);
  SysFreeString(classname2);
  SysFreeString(namespace);

  return true;
}

void bcd_disconnect(bcdwmi_ptr bwp)
{
  if (NULL != bwp)
  {
    if (NULL != bwp->pBcdObjectClass)
      bwp->pBcdObjectClass->lpVtbl->Release(bwp->pBcdObjectClass);
    if (NULL != bwp->pBcdStoreClass)
      bwp->pBcdStoreClass->lpVtbl->Release(bwp->pBcdStoreClass);
    if (NULL != bwp->pSvc)
      bwp->pSvc->lpVtbl->Release(bwp->pSvc);
    if (NULL != bwp->pLoc)
      bwp->pLoc->lpVtbl->Release(bwp->pLoc);
    memset(bwp, 0, sizeof(bcdwmi));
  }
}

void bcd_closestore(bcdstore_ptr bsp)
{
  if (NULL != bsp)
  {
    VariantClear(&bsp->this_pointer);
    if (NULL != bsp->pBcdStore)
      bsp->pBcdStore->lpVtbl->Release(bsp->pBcdStore), bsp->pBcdStore = NULL;
  }
}

bool bcd_openstore(bcdwmi_ptr bwp, const char* store_filename, bcdstore_ptr bsp )
{
  HRESULT               hr;
  BSTR                  this_pointer_of_class, method_name, in_param_name, out_param_name, ret_val_name;
  IWbemClassObject     *pOpenStore = NULL;
  IWbemClassObject     *pInParam = NULL;
  IWbemClassObject     *pOutParam = NULL;
  VARIANT               v;
  wchar_t               store_filename_w[256];
  CIMTYPE               type;
  LONG                  flavor;

  if (NULL == bwp || NULL == bsp)
    return false;

  memset(bsp, 0, sizeof(bcdstore));
  VariantInit(&bsp->this_pointer);

  this_pointer_of_class = SysAllocString(L"\\\\.\\ROOT\\WMI:BcdStore"); // call a static method, not a method from an instance
  method_name = SysAllocString(L"OpenStore");
  in_param_name = SysAllocString(L"File");
  out_param_name = SysAllocString(L"Store");
  ret_val_name = SysAllocString(L"ReturnValue");

  if (S_OK != bwp->pBcdStoreClass->lpVtbl->GetMethod(bwp->pBcdStoreClass, method_name, 0, &pOpenStore, NULL)) // get the OpenStore method
  {
ErrorExit:
    if (NULL != pOpenStore)
      pOpenStore->lpVtbl->Release(pOpenStore);
    if (NULL != pInParam)
      pInParam->lpVtbl->Release(pInParam);
    if (NULL != pOutParam)
      pOutParam->lpVtbl->Release(pOutParam);
    SysFreeString(ret_val_name);
    SysFreeString(in_param_name);
    SysFreeString(out_param_name);
    SysFreeString(method_name);
    SysFreeString(this_pointer_of_class);

    if (NULL != bsp->pBcdStore)
      bsp->pBcdStore->lpVtbl->Release(bsp->pBcdStore);
    VariantClear(&bsp->this_pointer);
    memset(bsp, 0, sizeof(bcdstore));
    return false;
  }

  if (NULL == pOpenStore)
    goto ErrorExit;

  if (S_OK != pOpenStore->lpVtbl->SpawnInstance(pOpenStore, 0, &pInParam))
    goto ErrorExit;

  VariantInit(&v);
  memset(store_filename_w, 0, sizeof(store_filename_w));
  if (NULL != store_filename)
    MultiByteToWideChar(CP_UTF8, 0, store_filename, -1, store_filename_w, sizeof(store_filename_w) / sizeof(store_filename_w[0]));
  v.vt = VT_BSTR;
  v.bstrVal = SysAllocString(store_filename_w);

  if (S_OK != pInParam->lpVtbl->Put(pInParam, in_param_name, 0, &v, 0))
  {
    VariantClear(&v);
    goto ErrorExit;
  }
  VariantClear(&v);

  if (S_OK != bwp->pSvc->lpVtbl->ExecMethod(bwp->pSvc, this_pointer_of_class/*invoke static method*/, method_name, 0, NULL, pInParam, &pOutParam, NULL))
    goto ErrorExit;

  if (NULL == pOutParam)
    goto ErrorExit;

  VariantInit(&v);
  if (S_OK != pOutParam->lpVtbl->Get(pOutParam, ret_val_name, 0, &v, NULL, 0))
  {
    VariantClear(&v);
    goto ErrorExit;
  }

  if ((VT_BOOL != v.vt) || (!v.boolVal))
  {
    VariantClear(&v);
    goto ErrorExit;
  }

  VariantClear(&v);

  VariantInit(&v);
  hr = pOutParam->lpVtbl->Get(pOutParam, out_param_name, 0, &v, 0, 0);
  if (NULL != v.punkVal)
  {
    hr = v.punkVal->lpVtbl->QueryInterface(v.punkVal, &IID_IWbemClassObject, (void**)&bsp->pBcdStore);
    if (S_OK != hr)
    {
      VariantClear(&v);
      goto ErrorExit;
    }
  }
  VariantClear(&v);

  bsp->pBcdStore->lpVtbl->Get(bsp->pBcdStore, L"__RELPATH", 0, &bsp->this_pointer, &type, &flavor);

  if (NULL != pOpenStore)
    pOpenStore->lpVtbl->Release(pOpenStore);
  if (NULL != pInParam)
    pInParam->lpVtbl->Release(pInParam);
  if (NULL != pOutParam)
    pOutParam->lpVtbl->Release(pOutParam);
  SysFreeString(ret_val_name);
  SysFreeString(in_param_name);
  SysFreeString(out_param_name);
  SysFreeString(method_name);
  SysFreeString(this_pointer_of_class);

  bsp->bwp = bwp; // establish back pointer

  return true;
}

// boolean EnumerateObjects(  [in]  uint32    Type,  [out] BcdObject Objects[]);
void bcd_debug_dump_objects(bcdstore_ptr bsp)
{
  IWbemClassObject     *pEnumObjects  = NULL;
  IWbemClassObject     *pEnumElements = NULL;
  IWbemClassObject     *pInParam      = NULL;
  IWbemClassObject     *pOutParam     = NULL;
  VARIANT               v, v2;
  BSTR                  method_name, in_param_name, ret_val_name, out_param_name, mName, out_param_name2;

  if (NULL == bsp)
    return;

  method_name = SysAllocString(L"EnumerateObjects");
  in_param_name = SysAllocString(L"Type");
  ret_val_name = SysAllocString(L"ReturnValue");
  out_param_name = SysAllocString(L"Objects");
  out_param_name2 = SysAllocString(L"Elements");
  mName = SysAllocString(L"EnumerateElements");

  if (S_OK != bsp->bwp->pBcdStoreClass->lpVtbl->GetMethod(bsp->bwp->pBcdStoreClass, method_name, 0, &pEnumObjects, NULL))
    goto Exit;

  if (NULL == pEnumObjects)
    goto Exit;

  // prepare EnumerateElements

  if (S_OK != bsp->bwp->pBcdObjectClass->lpVtbl->GetMethod(bsp->bwp->pBcdObjectClass, mName, 0, NULL/*EnumerateElements has no input params.*/, &pEnumElements /*out signature */))
    goto Exit;

  if (NULL == pEnumElements)
    goto Exit;

  if (S_OK != pEnumObjects->lpVtbl->SpawnInstance(pEnumObjects, 0, &pInParam))
    goto Exit;

  VariantInit(&v);
  v.vt   = VT_I4; // this is automatically converted to a CIM_UINT32, there is no uint32_t in the variant!
  v.lVal = 0;     // no real type, just enumerate everything

  if (S_OK != pInParam->lpVtbl->Put(pInParam, in_param_name, 0, &v, 0)) // CIM_UINT32)) -> if you use this, you have to provide the integer in decimal as a BSTR, what the hell...
  {
    VariantClear(&v);
    goto Exit;
  }
  VariantClear(&v);

  // the most complicated way I've ever seen to invoke a method. Thanks.
  if (S_OK != bsp->bwp->pSvc->lpVtbl->ExecMethod(bsp->bwp->pSvc, bsp->this_pointer.bstrVal, method_name, 0, NULL, pInParam, &pOutParam, NULL))
    goto Exit;

  if (NULL == pOutParam)
    goto Exit;

  // this is how Microsoft checks for a Boolean return value, wow!
  
  VariantInit(&v);
  if (S_OK != pOutParam->lpVtbl->Get(pOutParam, ret_val_name, 0, &v, NULL, 0))
  {
    VariantClear(&v);
    goto Exit;
  }

  if ((VT_BOOL != v.vt) || (!v.boolVal))
  {
    VariantClear(&v);
    goto Exit;
  }
  VariantClear(&v);

  // another mess: get the array with objects as a 'safearray' whatever that may be... (what is an unsafe array?)

  VariantInit(&v);
  if (S_OK != pOutParam->lpVtbl->Get(pOutParam, out_param_name, 0, &v, 0, 0))
  {
Exit2:
    VariantClear(&v);
    goto Exit;
  }

  if ((VT_ARRAY | VT_UNKNOWN) != v.vt) // this means: Array of objects
    goto Exit2;

  SAFEARRAY *psa = V_ARRAY(&v);

  if (NULL == psa)
    goto Exit2;

  long lBound, uBound;
  SafeArrayGetLBound(psa, 1, &lBound);
  SafeArrayGetUBound(psa, 1, &uBound);
  long numElems = uBound - lBound + 1;

  IUnknown** rawArray;
  SafeArrayAccessData(psa, (void**)&rawArray);
  for (long i = 0; i < numElems; i++) 
  {
    // IUnknown* pElem = rawArray[i]; // pElem is a BcdObject
    IWbemClassObject *pBcdObject = ((IWbemClassObject*)rawArray[i]);
    CIMTYPE               type;
    LONG                  flavor;
    VARIANT               vElem;

    VariantInit(&vElem);
    pBcdObject->lpVtbl->Get(pBcdObject, L"Id", 0, &vElem, &type, &flavor);
    wprintf(L"Id = %s | ", vElem.bstrVal);
    VariantClear(&vElem);

    VariantInit(&vElem);
    pBcdObject->lpVtbl->Get(pBcdObject, L"Type", 0, &vElem, &type, &flavor);
    wprintf(L"Type = 0x%08X\n", (uint32_t)vElem.lVal);
    VariantClear(&vElem);

    VARIANT this_pointer;
    VariantInit(&this_pointer);
    pBcdObject->lpVtbl->Get(pBcdObject, L"__RELPATH", 0, &this_pointer, &type, &flavor);

    pInParam->lpVtbl->Release(pInParam), pInParam = NULL;
    pOutParam->lpVtbl->Release(pOutParam), pOutParam = NULL;

    if (S_OK != pEnumElements->lpVtbl->SpawnInstance(pEnumElements, 0, &pInParam))
      goto Exit;

    if (S_OK != bsp->bwp->pSvc->lpVtbl->ExecMethod(bsp->bwp->pSvc, this_pointer.bstrVal, mName, 0, NULL, pInParam, &pOutParam, NULL))
      goto Exit;

    if (NULL == pOutParam)
      goto Exit;

    VariantInit(&v);
    if (S_OK != pOutParam->lpVtbl->Get(pOutParam, ret_val_name, 0, &v, NULL, 0))
    {
      VariantClear(&v);
      goto Exit;
    }

    if ((VT_BOOL != v.vt) || (!v.boolVal))
    {
      VariantClear(&v);
      goto Exit;
    }
    VariantClear(&v);

    //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    
    VariantInit(&v2);
    if (S_OK != pOutParam->lpVtbl->Get(pOutParam, out_param_name2, 0, &v2, 0, 0))
    {
Exit3:
      VariantClear(&v2);
      goto Exit;
    }

    if ((VT_ARRAY | VT_UNKNOWN) != v2.vt) // this means: Array of objects
      goto Exit3;

    SAFEARRAY* psa2 = V_ARRAY(&v2);

    if (NULL == psa2)
      goto Exit3;

    long lBound2, uBound2;
    SafeArrayGetLBound(psa2, 1, &lBound2);
    SafeArrayGetUBound(psa2, 1, &uBound2);
    long numElems2 = uBound2 - lBound2 + 1;

    IUnknown** rawArray2;
    SafeArrayAccessData(psa2, (void**)&rawArray2);
    for (long j = 0; j < numElems2; j++)
    {
      // IUnknown* pElem = rawArray2[j]; // pElem js a BcdElements
      IWbemClassObject* pBcdElement = ((IWbemClassObject*)rawArray2[j]);

      DumpBcdElement(L"",pBcdElement);
    }

    SafeArrayUnaccessData(psa2);
    VariantClear(&v2);

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    VariantClear(&this_pointer); // this pointer of current BcdObject
  }
  SafeArrayUnaccessData(psa);

  VariantClear(&v);

Exit:
  if (NULL != pEnumObjects)
    pEnumObjects->lpVtbl->Release(pEnumObjects);
  if (NULL != pEnumElements)
    pEnumElements->lpVtbl->Release(pEnumElements);
  if (NULL != pInParam)
    pInParam->lpVtbl->Release(pInParam);
  if (NULL != pOutParam)
    pOutParam->lpVtbl->Release(pOutParam);
  SysFreeString(method_name);
  SysFreeString(in_param_name);
  SysFreeString(out_param_name);
  SysFreeString(ret_val_name);
  SysFreeString(mName);
}

bool bcd_createstore(bcdwmi_ptr bwp, const char* store_filename, bcdstore_ptr bsp)
{
  HRESULT               hr;
  BSTR                  this_pointer_of_class, method_name, in_param_name, out_param_name, ret_val_name;
  IWbemClassObject     *pCreateStore = NULL;
  IWbemClassObject     *pInParam     = NULL;
  IWbemClassObject     *pOutParam    = NULL;
  VARIANT               v;
  wchar_t               store_filename_w[256];
  CIMTYPE               type;
  LONG                  flavor;

  if (NULL == bwp || NULL == bsp || NULL == store_filename)
    return false;

  memset(bsp, 0, sizeof(bcdstore));
  VariantInit(&bsp->this_pointer);

  this_pointer_of_class = SysAllocString(L"\\\\.\\ROOT\\WMI:BcdStore"); // call a static method, not a method from an instance
  method_name = SysAllocString(L"CreateStore");
  in_param_name = SysAllocString(L"File");
  out_param_name = SysAllocString(L"Store");
  ret_val_name = SysAllocString(L"ReturnValue");

  if (S_OK != bwp->pBcdStoreClass->lpVtbl->GetMethod(bwp->pBcdStoreClass, method_name, 0, &pCreateStore, NULL)) // get the CreateStore method
  {
ErrorExit:
    if (NULL != pCreateStore)
      pCreateStore->lpVtbl->Release(pCreateStore);
    if (NULL != pInParam)
      pInParam->lpVtbl->Release(pInParam);
    if (NULL != pOutParam)
      pOutParam->lpVtbl->Release(pOutParam);
    SysFreeString(ret_val_name);
    SysFreeString(in_param_name);
    SysFreeString(out_param_name);
    SysFreeString(method_name);
    SysFreeString(this_pointer_of_class);

    if (NULL != bsp->pBcdStore)
      bsp->pBcdStore->lpVtbl->Release(bsp->pBcdStore);
    VariantClear(&bsp->this_pointer);
    memset(bsp, 0, sizeof(bcdstore));
    return false;
  }

  if (NULL == pCreateStore)
    goto ErrorExit;

  if (S_OK != pCreateStore->lpVtbl->SpawnInstance(pCreateStore, 0, &pInParam))
    goto ErrorExit;

  VariantInit(&v);
  memset(store_filename_w, 0, sizeof(store_filename_w));
  if (NULL != store_filename)
    MultiByteToWideChar(CP_UTF8, 0, store_filename, -1, store_filename_w, sizeof(store_filename_w) / sizeof(store_filename_w[0]));
  v.vt = VT_BSTR;
  v.bstrVal = SysAllocString(store_filename_w);

  if (S_OK != pInParam->lpVtbl->Put(pInParam, in_param_name, 0, &v, 0))
  {
    VariantClear(&v);
    goto ErrorExit;
  }
  VariantClear(&v);

  if (S_OK != bwp->pSvc->lpVtbl->ExecMethod(bwp->pSvc, this_pointer_of_class/*invoke static method*/, method_name, 0, NULL, pInParam, &pOutParam, NULL))
    goto ErrorExit;

  if (NULL == pOutParam)
    goto ErrorExit;

  VariantInit(&v);
  if (S_OK != pOutParam->lpVtbl->Get(pOutParam, ret_val_name, 0, &v, NULL, 0))
  {
    VariantClear(&v);
    goto ErrorExit;
  }

  if ((VT_BOOL != v.vt) || (!v.boolVal))
  {
    VariantClear(&v);
    goto ErrorExit;
  }

  VariantClear(&v);

  VariantInit(&v);
  hr = pOutParam->lpVtbl->Get(pOutParam, out_param_name, 0, &v, 0, 0);
  if (NULL != v.punkVal)
  {
    hr = v.punkVal->lpVtbl->QueryInterface(v.punkVal, &IID_IWbemClassObject, (void**)&bsp->pBcdStore);
    if (S_OK != hr)
    {
      VariantClear(&v);
      goto ErrorExit;
    }
  }
  VariantClear(&v);

  bsp->pBcdStore->lpVtbl->Get(bsp->pBcdStore, L"__RELPATH", 0, &bsp->this_pointer, &type, &flavor);

  if (NULL != pCreateStore)
    pCreateStore->lpVtbl->Release(pCreateStore);
  if (NULL != pInParam)
    pInParam->lpVtbl->Release(pInParam);
  if (NULL != pOutParam)
    pOutParam->lpVtbl->Release(pOutParam);
  SysFreeString(ret_val_name);
  SysFreeString(in_param_name);
  SysFreeString(out_param_name);
  SysFreeString(method_name);
  SysFreeString(this_pointer_of_class);

  bsp->bwp = bwp; // establish back pointer

  return true;
}

static bool format_guid_as_widestring(wchar_t* guid_str, GUID *guid, bool generate_guid)
{
  if (generate_guid)
  {
    if (S_OK != CoCreateGuid(guid))
      return false;
  }

  wsprintfW(guid_str, L"{%08x-%04x-%04x-%04x-%012I64x}",
    (uint32_t)guid->Data1,
    (uint32_t)guid->Data2,
    (uint32_t)guid->Data3,
    (((uint32_t)guid->Data4[0]) << 8) | ((uint32_t)guid->Data4[1]),
    (((uint64_t)guid->Data4[2]) << 40) | (((uint64_t)guid->Data4[3]) << 32) | (((uint64_t)guid->Data4[4]) << 24) |
    (((uint64_t)guid->Data4[5]) << 16) | (((uint64_t)guid->Data4[6]) << 8) | ((uint64_t)guid->Data4[7]));

  return true;
}

#define FREE_SYS_STRING(_s)         if (NULL != (_s)) SysFreeString(_s); _s = NULL
#define FREE_COM_PTR(_p)            if (NULL != (_p)) (_p)->lpVtbl->Release(_p); (_p) = NULL
#define GET_BCDOBJECT_METHOD_PTR(_fname) if (S_OK != bsp->bwp->pBcdObjectClass->lpVtbl->GetMethod(bsp->bwp->pBcdObjectClass, funcname_##_fname, 0, &p##_fname, NULL)) goto do_exit; if (NULL == p##_fname) goto do_exit

#define CREATE_BCD_OBJECT(_type,_id) \
  if (S_OK != pCreateObject->lpVtbl->SpawnInstance(pCreateObject, 0, &pInParam)) \
  goto do_exit; \
  VariantInit(&v); \
  v.vt = VT_I4; \
  v.intVal = (_type); \
  if (S_OK != pInParam->lpVtbl->Put(pInParam, in_paramname_Type, 0, &v, 0)) \
  { \
    VariantClear(&v); \
    goto do_exit; \
  } \
  VariantClear(&v); \
  v.vt = VT_BSTR; \
  v.bstrVal = SysAllocString(_id); \
  if (S_OK != pInParam->lpVtbl->Put(pInParam, in_paramname_Id, 0, &v, 0)) \
  { \
    VariantClear(&v); \
    goto do_exit; \
  } \
  VariantClear(&v); \
  if (S_OK != bsp->bwp->pSvc->lpVtbl->ExecMethod(bsp->bwp->pSvc, bsp->this_pointer.bstrVal, funcname_createobject, 0, NULL, pInParam, &pOutParam, NULL)) \
  goto do_exit; \
  if (NULL == pOutParam) \
  goto do_exit; \
  VariantInit(&v); \
  if (S_OK != pOutParam->lpVtbl->Get(pOutParam, ret_val_name, 0, &v, NULL, 0)) \
  { \
    VariantClear(&v); \
    goto do_exit; \
  } \
  if ((VT_BOOL != v.vt) || (!v.boolVal)) \
  { \
    VariantClear(&v); \
    goto do_exit; \
  } \
  VariantClear(&v); \
  VariantInit(&v); \
  if ((S_OK != pOutParam->lpVtbl->Get(pOutParam, out_paramname_Object, 0, &v, 0, 0)) || (VT_UNKNOWN != v.vt) || (NULL == v.punkVal)) \
  { \
    VariantClear(&v); \
    goto do_exit; \
  } \
  if (NULL != pBcdObject) \
  pBcdObject->lpVtbl->Release(pBcdObject), pBcdObject = NULL; \
  if (S_OK != v.punkVal->lpVtbl->QueryInterface(v.punkVal, &IID_IWbemClassObject, (void**)&pBcdObject)) \
  { \
    VariantClear(&v); \
    goto do_exit; \
  } \
  VariantClear(&v); \
  VariantInit(&obj_this_pointer); \
  pBcdObject->lpVtbl->Get(pBcdObject, L"__RELPATH", 0, &obj_this_pointer, &type, &flavor); \
  if (NULL != pInParam) \
    pInParam->lpVtbl->Release(pInParam), pInParam = NULL; \
  if (NULL != pOutParam) \
    pOutParam->lpVtbl->Release(pOutParam), pOutParam = NULL

#define RELEASE_CURRENT_BCD_OBJECT()  VariantClear(&obj_this_pointer);pBcdObject->lpVtbl->Release(pBcdObject);pBcdObject = NULL

#define BCD_SetBooleanElement(_type,_val) \
  if (S_OK != pSetBooleanElement->lpVtbl->SpawnInstance(pSetBooleanElement, 0, &pInParam)) \
  goto do_exit; \
  VariantInit(&v); \
  v.vt = VT_I4; \
  v.intVal = (_type); \
  if (S_OK != pInParam->lpVtbl->Put(pInParam, in_paramname_Type, 0, &v, 0)) \
  { \
    VariantClear(&v); \
    goto do_exit; \
  } \
  VariantClear(&v); \
  v.vt = VT_BOOL; \
  v.boolVal = (_val); \
  if (S_OK != pInParam->lpVtbl->Put(pInParam, in_paramname_Boolean, 0, &v, 0)) \
  { \
    VariantClear(&v); \
    goto do_exit; \
  } \
  VariantClear(&v); \
  if (S_OK != bsp->bwp->pSvc->lpVtbl->ExecMethod(bsp->bwp->pSvc, obj_this_pointer.bstrVal, funcname_SetBooleanElement, 0, NULL, pInParam, &pOutParam, NULL)) \
  goto do_exit; \
  if (NULL == pOutParam) \
  goto do_exit; \
  VariantInit(&v); \
  if (S_OK != pOutParam->lpVtbl->Get(pOutParam, ret_val_name, 0, &v, NULL, 0)) \
  { \
    VariantClear(&v); \
    goto do_exit; \
  } \
  if ((VT_BOOL != v.vt) || (!v.boolVal)) \
  { \
    VariantClear(&v); \
    goto do_exit; \
  } \
  VariantClear(&v)

#define BCD_SetIntegerElement(_type,_val) \
  if (S_OK != pSetIntegerElement->lpVtbl->SpawnInstance(pSetIntegerElement, 0, &pInParam)) \
    goto do_exit; \
  VariantInit(&v); \
  v.vt = VT_I4; \
  v.intVal = (_type); \
  if (S_OK != pInParam->lpVtbl->Put(pInParam, in_paramname_Type, 0, &v, 0)) \
  { \
    VariantClear(&v); \
    goto do_exit; \
  } \
  VariantClear(&v); \
  v.vt = VT_BSTR; \
  wsprintfW(uint64_wstr, L"%I64u", (uint64_t)(_val)); \
  v.bstrVal = SysAllocString(uint64_wstr); \
  if (S_OK != pInParam->lpVtbl->Put(pInParam, in_paramname_Integer, 0, &v, CIM_UINT64)) \
  { \
    VariantClear(&v); \
    goto do_exit; \
  } \
  VariantClear(&v); \
  if (S_OK != bsp->bwp->pSvc->lpVtbl->ExecMethod(bsp->bwp->pSvc, obj_this_pointer.bstrVal, funcname_SetIntegerElement, 0, NULL, pInParam, &pOutParam, NULL)) \
    goto do_exit; \
  if (NULL == pOutParam) \
    goto do_exit; \
  VariantInit(&v); \
  if (S_OK != pOutParam->lpVtbl->Get(pOutParam, ret_val_name, 0, &v, NULL, 0)) \
  { \
    VariantClear(&v); \
    goto do_exit; \
  } \
  if ((VT_BOOL != v.vt) || (!v.boolVal)) \
  { \
    VariantClear(&v); \
    goto do_exit; \
  } \
  VariantClear(&v)

#define BCD_SetDeviceElement(_type,_device_type,_additional_options) \
  if (S_OK != pSetDeviceElement->lpVtbl->SpawnInstance(pSetDeviceElement, 0, &pInParam)) \
    goto do_exit; \
  VariantInit(&v); \
  v.vt = VT_I4; \
  v.intVal = (_type); \
  if (S_OK != pInParam->lpVtbl->Put(pInParam, in_paramname_Type, 0, &v, 0)) \
  { \
    VariantClear(&v); \
    goto do_exit; \
  } \
  VariantClear(&v); \
  VariantInit(&v); \
  v.vt = VT_I4; \
  v.intVal = (_device_type); \
  if (S_OK != pInParam->lpVtbl->Put(pInParam, in_paramname_DeviceType, 0, &v, 0)) \
  { \
    VariantClear(&v); \
    goto do_exit; \
  } \
  VariantClear(&v); \
  VariantInit(&v); \
  v.vt = VT_BSTR; \
  v.bstrVal = SysAllocString(_additional_options); \
  if (S_OK != pInParam->lpVtbl->Put(pInParam, in_paramname_AdditionalOptions, 0, &v, 0)) \
  { \
    VariantClear(&v); \
    goto do_exit; \
  } \
  VariantClear(&v); \
  if (S_OK != bsp->bwp->pSvc->lpVtbl->ExecMethod(bsp->bwp->pSvc, obj_this_pointer.bstrVal, funcname_SetDeviceElement, 0, NULL, pInParam, &pOutParam, NULL)) \
    goto do_exit; \
  if (NULL == pOutParam) \
    goto do_exit; \
  VariantInit(&v); \
  if (S_OK != pOutParam->lpVtbl->Get(pOutParam, ret_val_name, 0, &v, NULL, 0)) \
  { \
    VariantClear(&v); \
    goto do_exit; \
  } \
  if ((VT_BOOL != v.vt) || (!v.boolVal)) \
  { \
    VariantClear(&v); \
    goto do_exit; \
  } \
  VariantClear(&v)

#define BCD_SetPartitionDeviceElement(_type,_device_type,_additional_options,_path) \
  if (S_OK != pSetPartitionDeviceElement->lpVtbl->SpawnInstance(pSetPartitionDeviceElement, 0, &pInParam)) \
    goto do_exit; \
  VariantInit(&v); \
  v.vt = VT_I4; \
  v.intVal = (_type); \
  if (S_OK != pInParam->lpVtbl->Put(pInParam, in_paramname_Type, 0, &v, 0)) \
  { \
    VariantClear(&v); \
    goto do_exit; \
  } \
  VariantClear(&v); \
  VariantInit(&v); \
  v.vt = VT_I4; \
  v.intVal = (_device_type); \
  if (S_OK != pInParam->lpVtbl->Put(pInParam, in_paramname_DeviceType, 0, &v, 0)) \
  { \
    VariantClear(&v); \
    goto do_exit; \
  } \
  VariantClear(&v); \
  VariantInit(&v); \
  v.vt = VT_BSTR; \
  v.bstrVal = SysAllocString(_additional_options); \
  if (S_OK != pInParam->lpVtbl->Put(pInParam, in_paramname_AdditionalOptions, 0, &v, 0)) \
  { \
    VariantClear(&v); \
    goto do_exit; \
  } \
  VariantClear(&v); \
  VariantInit(&v); \
  v.vt = VT_BSTR; \
  v.bstrVal = SysAllocString(_path); \
  if (S_OK != pInParam->lpVtbl->Put(pInParam, in_paramname_Path, 0, &v, 0)) \
  { \
    VariantClear(&v); \
    goto do_exit; \
  } \
  VariantClear(&v); \
  if (S_OK != bsp->bwp->pSvc->lpVtbl->ExecMethod(bsp->bwp->pSvc, obj_this_pointer.bstrVal, funcname_SetPartitionDeviceElement, 0, NULL, pInParam, &pOutParam, NULL)) \
    goto do_exit; \
  if (NULL == pOutParam) \
    goto do_exit; \
  VariantInit(&v); \
  if (S_OK != pOutParam->lpVtbl->Get(pOutParam, ret_val_name, 0, &v, NULL, 0)) \
  { \
    VariantClear(&v); \
    goto do_exit; \
  } \
  if ((VT_BOOL != v.vt) || (!v.boolVal)) \
  { \
    VariantClear(&v); \
    goto do_exit; \
  } \
  VariantClear(&v)

#define BCD_SetObjectElement(_type,_id) \
  if (S_OK != pSetObjectElement->lpVtbl->SpawnInstance(pSetObjectElement, 0, &pInParam)) \
    goto do_exit; \
  VariantInit(&v); \
  v.vt = VT_I4; \
  v.intVal = (_type); \
  if (S_OK != pInParam->lpVtbl->Put(pInParam, in_paramname_Type, 0, &v, 0)) \
  { \
    VariantClear(&v); \
    goto do_exit; \
  } \
  VariantClear(&v); \
  v.vt = VT_BSTR; \
  v.bstrVal = SysAllocString(_id); \
  if (S_OK != pInParam->lpVtbl->Put(pInParam, in_paramname_Id, 0, &v, 0)) \
  { \
    VariantClear(&v); \
    goto do_exit; \
  } \
  VariantClear(&v); \
  if (S_OK != bsp->bwp->pSvc->lpVtbl->ExecMethod(bsp->bwp->pSvc, obj_this_pointer.bstrVal, funcname_SetObjectElement, 0, NULL, pInParam, &pOutParam, NULL)) \
    goto do_exit; \
  if (NULL == pOutParam) \
    goto do_exit; \
  VariantInit(&v); \
  if (S_OK != pOutParam->lpVtbl->Get(pOutParam, ret_val_name, 0, &v, NULL, 0)) \
  { \
    VariantClear(&v); \
    goto do_exit; \
  } \
  if ((VT_BOOL != v.vt) || (!v.boolVal)) \
  { \
    VariantClear(&v); \
    goto do_exit; \
  } \
  VariantClear(&v)

#define BCD_SetStringElement(_type,_str) \
  if (S_OK != pSetStringElement->lpVtbl->SpawnInstance(pSetStringElement, 0, &pInParam)) \
    goto do_exit; \
  VariantInit(&v); \
  v.vt = VT_I4; \
  v.intVal = (_type); \
  if (S_OK != pInParam->lpVtbl->Put(pInParam, in_paramname_Type, 0, &v, 0)) \
  { \
    VariantClear(&v); \
    goto do_exit; \
  } \
  VariantClear(&v); \
  v.vt = VT_BSTR; \
  v.bstrVal = SysAllocString(_str); \
  if (S_OK != pInParam->lpVtbl->Put(pInParam, in_paramname_String, 0, &v, 0)) \
  { \
    VariantClear(&v); \
    goto do_exit; \
  } \
  VariantClear(&v); \
  if (S_OK != bsp->bwp->pSvc->lpVtbl->ExecMethod(bsp->bwp->pSvc, obj_this_pointer.bstrVal, funcname_SetStringElement, 0, NULL, pInParam, &pOutParam, NULL)) \
    goto do_exit; \
  if (NULL == pOutParam) \
    goto do_exit; \
  VariantInit(&v); \
  if (S_OK != pOutParam->lpVtbl->Get(pOutParam, ret_val_name, 0, &v, NULL, 0)) \
  { \
    VariantClear(&v); \
    goto do_exit; \
  } \
  if ((VT_BOOL != v.vt) || (!v.boolVal)) \
  { \
    VariantClear(&v); \
    goto do_exit; \
  } \
  VariantClear(&v)

#define BCD_SetObjectListElement(_type) \
  if (S_OK != pSetIntegerElement->lpVtbl->SpawnInstance(pSetObjectListElement, 0, &pInParam)) \
    goto do_exit; \
  VariantInit(&v); \
  v.vt = VT_I4; \
  v.intVal = (_type); \
  if (S_OK != pInParam->lpVtbl->Put(pInParam, in_paramname_Type, 0, &v, 0)) \
  { \
    VariantClear(&v); \
    goto do_exit; \
  } \
  VariantClear(&v); \
  if (S_OK != pInParam->lpVtbl->Put(pInParam, in_paramname_Ids, 0, &v_array, 0)) \
  { \
    VariantClear(&v_array); \
    goto do_exit; \
  } \
  VariantClear(&v_array); \
  if (S_OK != bsp->bwp->pSvc->lpVtbl->ExecMethod(bsp->bwp->pSvc, obj_this_pointer.bstrVal, funcname_SetObjectListElement, 0, NULL, pInParam, &pOutParam, NULL)) \
    goto do_exit; \
  if (NULL == pOutParam) \
    goto do_exit; \
  VariantInit(&v); \
  if (S_OK != pOutParam->lpVtbl->Get(pOutParam, ret_val_name, 0, &v, NULL, 0)) \
  { \
    VariantClear(&v); \
    goto do_exit; \
  } \
  if ((VT_BOOL != v.vt) || (!v.boolVal)) \
  { \
    VariantClear(&v); \
    goto do_exit; \
  } \
  VariantClear(&v)

#define BCD_SetFileDeviceElement(_type,_device_type,_additional_options,_path,_parent_device_type,_parent_additional_options,_parent_path) \
  if (S_OK != pSetFileDeviceElement->lpVtbl->SpawnInstance(pSetFileDeviceElement, 0, &pInParam)) \
    goto do_exit; \
  VariantInit(&v); \
  v.vt = VT_I4; \
  v.intVal = (_type); \
  if (S_OK != pInParam->lpVtbl->Put(pInParam, in_paramname_Type, 0, &v, 0)) \
  { \
    VariantClear(&v); \
    goto do_exit; \
  } \
  VariantClear(&v); \
  VariantInit(&v); \
  v.vt = VT_I4; \
  v.intVal = (_device_type); \
  if (S_OK != pInParam->lpVtbl->Put(pInParam, in_paramname_DeviceType, 0, &v, 0)) \
  { \
    VariantClear(&v); \
    goto do_exit; \
  } \
  VariantClear(&v); \
  VariantInit(&v); \
  v.vt = VT_BSTR; \
  v.bstrVal = SysAllocString(_additional_options); \
  if (S_OK != pInParam->lpVtbl->Put(pInParam, in_paramname_AdditionalOptions, 0, &v, 0)) \
  { \
    VariantClear(&v); \
    goto do_exit; \
  } \
  VariantClear(&v); \
  VariantInit(&v); \
  v.vt = VT_BSTR; \
  v.bstrVal = SysAllocString(_path); \
  if (S_OK != pInParam->lpVtbl->Put(pInParam, in_paramname_Path, 0, &v, 0)) \
  { \
    VariantClear(&v); \
    goto do_exit; \
  } \
  VariantClear(&v); \
  VariantClear(&v); \
  VariantInit(&v); \
  v.vt = VT_I4; \
  v.intVal = (_parent_device_type); \
  if (S_OK != pInParam->lpVtbl->Put(pInParam, in_paramname_ParentDeviceType, 0, &v, 0)) \
  { \
    VariantClear(&v); \
    goto do_exit; \
  } \
  VariantClear(&v); \
  VariantInit(&v); \
  v.vt = VT_BSTR; \
  v.bstrVal = SysAllocString(_parent_additional_options); \
  if (S_OK != pInParam->lpVtbl->Put(pInParam, in_paramname_ParentAdditionalOptions, 0, &v, 0)) \
  { \
    VariantClear(&v); \
    goto do_exit; \
  } \
  VariantClear(&v); \
  VariantInit(&v); \
  v.vt = VT_BSTR; \
  v.bstrVal = SysAllocString(_parent_path); \
  if (S_OK != pInParam->lpVtbl->Put(pInParam, in_paramname_ParentPath, 0, &v, 0)) \
  { \
    VariantClear(&v); \
    goto do_exit; \
  } \
  VariantClear(&v); \
  if (S_OK != bsp->bwp->pSvc->lpVtbl->ExecMethod(bsp->bwp->pSvc, obj_this_pointer.bstrVal, funcname_SetFileDeviceElement, 0, NULL, pInParam, &pOutParam, NULL)) \
    goto do_exit; \
  if (NULL == pOutParam) \
    goto do_exit; \
  VariantInit(&v); \
  if (S_OK != pOutParam->lpVtbl->Get(pOutParam, ret_val_name, 0, &v, NULL, 0)) \
  { \
    VariantClear(&v); \
    goto do_exit; \
  } \
  if ((VT_BOOL != v.vt) || (!v.boolVal)) \
  { \
    VariantClear(&v); \
    goto do_exit; \
  } \
  VariantClear(&v)

static void MyVariantClear(VARIANT * v)
{
  VariantClear(v);
  VariantInit(v);
}

#define VariantClear      MyVariantClear

/**********************************************************************************************//**
 * @fn  static bool CreateSafeArrayStrings(VARIANT* v, uint32_t numStrings, ...)
 *
 * @brief Creates safe array strings
 *
 * @author Ingo A. Kubbilun (www.devcorn.de)
 * @date   01.09.2021
 *
 * @param [in,out]  v           pointer to VARIANT that receives the SafeArray
 * @param           numStrings  number of strings to be added
 * @param           ...         Variable arguments: const wchar_t*, const wchar_t*, ...
 *
 * @returns true on success, false on error
 **************************************************************************************************/

static bool CreateSafeArrayStrings(VARIANT* v, uint32_t numStrings, ...)
{
  va_list                   ap;
  SAFEARRAYBOUND            saBound;
  SAFEARRAY                *psa;
  BSTR                     *ppStrings;
  uint32_t                  i;

  if (NULL == v || 0 == numStrings)
    return false;

  saBound.lLbound = 0;
  saBound.cElements = (ULONG) numStrings;

  psa = SafeArrayCreate(VT_BSTR, 1, &saBound);
  if (NULL == psa)
    return false;

  if (FAILED(SafeArrayAccessData(psa, (void**)&ppStrings)))
  {
    SafeArrayDestroy(psa);
    return false;
  }

  va_start(ap, numStrings);
  for (i = 0; i < numStrings; i++)
    ppStrings[i] = SysAllocString(va_arg(ap, const wchar_t*));
  va_end(ap);

  SafeArrayUnaccessData(psa);

  VariantClear(v);
  v->vt = VT_ARRAY | VT_BSTR; //  VT_SAFEARRAY;
  v->parray = psa;        // do not destroy psa because it is now part of the variant

  return true;
}

static void DumpBcdElement(const wchar_t *prefix, IWbemClassObject* pBcdElement)
{
  CIMTYPE               type;
  LONG                  flavor;
  VARIANT               v, v2;
  uint32_t              element_type;

  // get type member of BcdElement, which is always 'there'

  VariantInit(&v);
  VariantInit(&v2);
  pBcdElement->lpVtbl->Get(pBcdElement, L"Type", 0, &v, &type, &flavor);
  element_type = (uint32_t)v.lVal;
  wprintf(L"[%s]  Element Type = 0x%08X:\n", prefix, element_type);
  VariantClear(&v);

  // do not evaluate the element_type but just try to get everything we can get

  // 1.) try Boolean

  type = 0;
  flavor = 0;
  VariantInit(&v);
  if (S_OK == pBcdElement->lpVtbl->Get(pBcdElement, L"Boolean", 0, &v, &type, &flavor))
    wprintf(L"[%s]    Boolean = %s\n", prefix, v.boolVal ? L"Yes" : L"No");
  VariantClear(&v);

  // 2.) try Integer

  type = 0;
  flavor = 0;
  VariantInit(&v);
  if (S_OK == pBcdElement->lpVtbl->Get(pBcdElement, L"Integer", 0, &v, &type, &flavor))
  {
    if (CIM_UINT64 == type && VT_BSTR == v.vt)
    {
      uint64_t u = (uint64_t)wcstoul(v.bstrVal, NULL, 10);
      wprintf(L"[%s]    Integer (64bit) = %s = 0x%016I64X\n", prefix, v.bstrVal, u);
    }
  }
  VariantClear(&v);

  // 3.) try String

  type = 0;
  flavor = 0;
  VariantInit(&v);
  if (S_OK == pBcdElement->lpVtbl->Get(pBcdElement, L"String", 0, &v, &type, &flavor))
  {
    if (VT_BSTR == v.vt)
      wprintf(L"[%s]    String = '%s'\n", prefix, v.bstrVal);
  }
  VariantClear(&v);

  // 4.) try Id

  type = 0;
  flavor = 0;
  VariantInit(&v);
  if (S_OK == pBcdElement->lpVtbl->Get(pBcdElement, L"Id", 0, &v, &type, &flavor))
  {
    if (VT_BSTR == v.vt)
      wprintf(L"[%s]    Id = '%s'\n", prefix, v.bstrVal);
  }
  VariantClear(&v);

  // 4b) try Path

  type = 0;
  flavor = 0;
  VariantInit(&v);
  if (S_OK == pBcdElement->lpVtbl->Get(pBcdElement, L"Path", 0, &v, &type, &flavor))
  {
    if (VT_BSTR == v.vt)
      wprintf(L"[%s]    Path = '%s'\n", prefix, v.bstrVal);
  }
  VariantClear(&v);

  // 5.) try Ids (Array of Id)

  type = 0;
  flavor = 0;
  VariantInit(&v);
  if (S_OK == pBcdElement->lpVtbl->Get(pBcdElement, L"Ids", 0, &v, &type, &flavor))
  {
    if ((VT_ARRAY | VT_BSTR) == v.vt)
    {
      SAFEARRAY* psa = V_ARRAY(&v);
      if (NULL != psa)
      {
        long lBound2, uBound2;
        SafeArrayGetLBound(psa, 1, &lBound2);
        SafeArrayGetUBound(psa, 1, &uBound2);
        long numElems2 = uBound2 - lBound2 + 1;

        BSTR* rawArray;
        SafeArrayAccessData(psa, (void**)&rawArray);
        for (long j = 0; j < numElems2; j++)
        {
          wprintf(L"[%s]    Id[%2u] = '%s'\n", prefix, (uint32_t)j, rawArray[j]);
        }
        SafeArrayUnaccessData(psa);
      }
    }
  }
  VariantClear(&v);

  // 6.) try Integers (Array of BSTR)

  type = 0;
  flavor = 0;
  VariantInit(&v);
  if (S_OK == pBcdElement->lpVtbl->Get(pBcdElement, L"Integers", 0, &v, &type, &flavor))
  {
    if ((VT_ARRAY | VT_BSTR) == v.vt)
    {
      SAFEARRAY* psa = V_ARRAY(&v);
      if (NULL != psa)
      {
        long lBound2, uBound2;
        SafeArrayGetLBound(psa, 1, &lBound2);
        SafeArrayGetUBound(psa, 1, &uBound2);
        long numElems2 = uBound2 - lBound2 + 1;

        BSTR* rawArray;
        SafeArrayAccessData(psa, (void**)&rawArray);
        for (long j = 0; j < numElems2; j++)
        {
          uint64_t u = (uint64_t)wcstoul(rawArray[j], NULL, 10);
          wprintf(L"[%s]    Integers[%2u] (64bit) = %s = 0x%016I64X\n", prefix, (uint32_t)j, rawArray[j], u);
        }
        SafeArrayUnaccessData(psa);
      }
    }
  }
  VariantClear(&v);

  // 7.) try Device

  type = 0;
  flavor = 0;
  VariantInit(&v2);
  if (S_OK == pBcdElement->lpVtbl->Get(pBcdElement, L"Device", 0, &v2, &type, &flavor)) // BcdDeviceData
  {
    if (VT_UNKNOWN == v2.vt)
    {
      IWbemClassObject* pInnerBcdElement = (IWbemClassObject*)v2.punkVal;
      VariantInit(&v);
      if (S_OK == pInnerBcdElement->lpVtbl->Get(pInnerBcdElement, L"DeviceType", 0, &v, &type, &flavor))
      {
        if (VT_I4 == v.vt && CIM_UINT32 == type)
        wprintf(L"[%s]    BcdDeviceData : Device.DeviceType = Integer = %u / 0x%08X\n", prefix, v.lVal, v.lVal);
      }
      VariantClear(&v);
      VariantInit(&v);
      if (S_OK == pInnerBcdElement->lpVtbl->Get(pInnerBcdElement, L"AdditionalOptions", 0, &v, &type, &flavor))
      {
        if (VT_BSTR == v.vt)
          wprintf(L"[%s]    BcdDeviceData : Device.AdditionalOptions = String = '%s'\n", prefix, v.bstrVal);
      }
      VariantClear(&v);

      VariantInit(&v);
      if (S_OK == pInnerBcdElement->lpVtbl->Get(pInnerBcdElement, L"Path", 0, &v, &type, &flavor))
      {
        if (VT_BSTR == v.vt)
          wprintf(L"[%s]    BcdDeviceData : Path = '%s'\n", prefix, v.bstrVal);
      }
      VariantClear(&v);


      DumpBcdElement(L"BcdDeviceData", pInnerBcdElement);
    }
  }
  VariantClear(&v2);

  // 8.) try Parent

  type = 0;
  flavor = 0;
  VariantInit(&v2);
  if (S_OK == pBcdElement->lpVtbl->Get(pBcdElement, L"Parent", 0, &v2, &type, &flavor)) // BcdDeviceData
  {
    if (VT_UNKNOWN == v2.vt)
    {
      IWbemClassObject* pInnerBcdElement = (IWbemClassObject*)v2.punkVal;
      VariantInit(&v);
      if (S_OK == pInnerBcdElement->lpVtbl->Get(pInnerBcdElement, L"DeviceType", 0, &v, &type, &flavor))
      {
        if (VT_I4 == v.vt && CIM_UINT32 == type)
          wprintf(L"[%s]    [Parent]BcdDeviceData : Device.DeviceType = Integer = %u / 0x%08X\n", prefix, v.lVal, v.lVal);
      }
      VariantClear(&v);
      VariantInit(&v);
      if (S_OK == pInnerBcdElement->lpVtbl->Get(pInnerBcdElement, L"AdditionalOptions", 0, &v, &type, &flavor))
      {
        if (VT_BSTR == v.vt)
          wprintf(L"[%s]    [Parent]BcdDeviceData : Device.AdditionalOptions = String = '%s'\n", prefix, v.bstrVal);
      }
      VariantClear(&v);

      VariantInit(&v);
      if (S_OK == pInnerBcdElement->lpVtbl->Get(pInnerBcdElement, L"Path", 0, &v, &type, &flavor))
      {
        if (VT_BSTR == v.vt)
          wprintf(L"[%s]    [Parent]BcdDeviceData : Path = '%s'\n", prefix, v.bstrVal);
      }
      VariantClear(&v);


      DumpBcdElement(L"[Parent]BcdDeviceData", pInnerBcdElement);
    }
  }
  VariantClear(&v2);
}

/*
 * Message from 'the author':
 * --------------------------
 * This is probably the WORST C function you have ever seen. Although it performs
 * well allocating and cleaning up all this foobar COM / WMI / BCD stuff.
 * Why Microsoft implemented this BCD 'facility' via a WMI provider is completely
 * beyond me: A DLL with plain C function on its export interface would have done
 * the job by far easier for 'the developer'.
 * Anyway, nothing in the MSDN tells you what the 'this' pointer of an object
 * instance is. I had to find it out googling hours and hours and finally
 * found the __RELPATH b**ls**t you can find in the code below.
 * Everything that is stored in the Boot Configuration Data could have been
 * stored in a plain text file line-by-line. Linux's GRUB2 does this e.g. in its
 * grub.cfg.
 * But hey, why not let the developer write hundreds of lines of code to do
 * the same job?
 *
 * BTW, the BCD data is stored in so called 'binary registry hives'. You can
 * find some from-the-scratch code in github to read, parse, and write these
 * files (e.g. on Linux).
 * I decided to do the job using the official Microsoft APIs although the
 * MSDN lacks some pieces of information (which I tried to add here).
 */
bool bcd_create_objects_and_entries(bcdstore_ptr bsp,
  uint32_t efi_partition_no,
  uint32_t windows_partition_no,
  char windows_drive_letter,
  uint32_t recovery_partition,
  const char* locale)
{
  bool                                overall_result = false;
  BSTR                                funcname_createobject = NULL, funcname_SetBooleanElement = NULL, funcname_SetDeviceElement = NULL,
                                      funcname_SetFileDeviceElement = NULL, funcname_SetIntegerElement = NULL, funcname_SetIntegerListElement = NULL,
                                      funcname_SetObjectElement = NULL, funcname_SetObjectListElement = NULL, funcname_SetPartitionDeviceElement = NULL,
                                      funcname_SetPartitionDeviceElementWithFlags = NULL, funcname_SetQualifiedPartitionDeviceElement = NULL, funcname_SetStringElement = NULL,
                                      funcname_SetVhdDeviceElement = NULL, in_paramname_Type = NULL, in_paramname_Id = NULL, in_paramname_Ids = NULL, in_paramname_Boolean = NULL,
                                      in_paramname_DeviceType = NULL, in_paramname_String = NULL, in_paramname_AdditionalOptions = NULL, in_paramname_Path = NULL,
                                      in_paramname_Flags = NULL, in_paramname_Integer = NULL, in_paramname_Integers = NULL, in_paramname_ParentPath = NULL, in_paramname_ParentDeviceType = NULL,
                                      in_paramname_PartitionStyle = NULL, in_paramname_ParentAdditionalOptions = NULL, in_paramname_DiskSignature = NULL, in_paramname_PartitionIdentifier = NULL,
                                      in_paramname_CustomLocate = NULL, out_paramname_Object = NULL, ret_val_name = NULL;
  IWbemClassObject                   *pCreateObject = NULL;
  IWbemClassObject                   *pSetBooleanElement = NULL;
  IWbemClassObject                   *pSetDeviceElement = NULL;
  IWbemClassObject                   *pSetFileDeviceElement = NULL;
  IWbemClassObject                   *pSetIntegerElement = NULL;
  IWbemClassObject                   *pSetIntegerListElement = NULL;
  IWbemClassObject                   *pSetObjectElement = NULL;
  IWbemClassObject                   *pSetObjectListElement = NULL;
  IWbemClassObject                   *pSetPartitionDeviceElement = NULL;
  IWbemClassObject                   *pSetPartitionDeviceElementWithFlags = NULL;
  IWbemClassObject                   *pSetQualifiedPartitionDeviceElement = NULL;
  IWbemClassObject                   *pSetStringElement = NULL;
  IWbemClassObject                   *pSetVhdDeviceElement = NULL;
  IWbemClassObject                   *pInParam = NULL;
  IWbemClassObject                   *pOutParam = NULL;
  IWbemClassObject                   *pBcdObject = NULL;
  VARIANT                             v, v_array;
  CIMTYPE                             type;
  LONG                                flavor;
  VARIANT                             obj_this_pointer;
  wchar_t                             uint64_wstr[32], w_str[256];
  GUID                                guid_WindowsResume, guid_Windows10, guid_RecoverySequence, guid_DeviceOptionsRecovery;
  wchar_t                             guid_WindowsResume_Str[64], guid_Windows10_Str[64], guid_RecoverySequence_Str[64], guid_DeviceOptionsRecovery_Str[64];

  if (NULL == bsp || NULL == locale)
    return false;

  VariantInit(&v);
  VariantInit(&v_array);
  VariantInit(&obj_this_pointer);

  // [1] get shitloads of OLE strings...

  funcname_createobject = SysAllocString(L"CreateObject"); // in BcdStore: boolean CreateObject( [in] string Id, [in]  uint32 Type, [out] BcdObject Object );
  funcname_SetBooleanElement = SysAllocString(L"SetBooleanElement"); // boolean SetBooleanElement( [in] uint32  Type, [in] boolean Boolean );
  funcname_SetDeviceElement = SysAllocString(L"SetDeviceElement"); // boolean SetDeviceElement( [in] uint32 Type, [in] uint32 DeviceType, [in] string AdditionalOptions );
  funcname_SetFileDeviceElement = SysAllocString(L"SetFileDeviceElement"); // boolean SetFileDeviceElement( [in] uint32 Type, [in] uint32 DeviceType, [in] string AdditionalOptions, [in] string Path, [in] uint32 ParentDeviceType, [in] string ParentAdditionalOptions, [in] string ParentPath );
  funcname_SetIntegerElement = SysAllocString(L"SetIntegerElement"); // boolean SetIntegerElement( [in] uint32 Type, [in] uint64 Integer );
  funcname_SetIntegerListElement = SysAllocString(L"SetIntegerListElement"); // boolean SetIntegerListElement( [in] uint32 Type, [in] uint64 Integers[] );
  funcname_SetObjectElement = SysAllocString(L"SetObjectElement"); // boolean SetObjectElement( [in] uint32 Type, [in] string Id );
  funcname_SetObjectListElement = SysAllocString(L"SetObjectListElement"); // boolean SetObjectListElement( [in] uint32 Type, [in] string Ids[] );
  funcname_SetPartitionDeviceElement = SysAllocString(L"SetPartitionDeviceElement"); // boolean SetPartitionDeviceElement( [in] uint32 Type, [in] uint32 DeviceType, [in] string AdditionalOptions, [in] string Path );
  funcname_SetPartitionDeviceElementWithFlags = SysAllocString(L"SetPartitionDeviceElementWithFlags"); //boolean SetPartitionDeviceElementWithFlags( [in] uint32 Type, [in] uint32 DeviceType, [in] string AdditionalOptions, [in] string Path, [in] uint32 Flags );
  funcname_SetQualifiedPartitionDeviceElement = SysAllocString(L"SetQualifiedPartitionDeviceElement"); //boolean SetQualifiedPartitionDeviceElement( [in] ULONG Type, [in] ULONG  PartitionStyle, [in] PCWSTR DiskSignature, [in] PCWSTR PartitionIdentifier );
  funcname_SetStringElement = SysAllocString(L"SetStringElement"); // boolean SetStringElement( [in] uint32 Type, [in] string String );
  funcname_SetVhdDeviceElement = SysAllocString(L"SetVhdDeviceElement"); //boolean SetVhdDeviceElement( [in] ULONG  Type, [in] PCWSTR Path, [in] ULONG  ParentDeviceType, [in] PCWSTR ParentAdditionalOptions, [in] PCWSTR ParentPath, [in] ULONG  CustomLocate );
  in_paramname_Type = SysAllocString(L"Type");
  in_paramname_Id = SysAllocString(L"Id");
  in_paramname_Ids = SysAllocString(L"Ids");
  in_paramname_Boolean = SysAllocString(L"Boolean");
  in_paramname_DeviceType = SysAllocString(L"DeviceType");
  in_paramname_String = SysAllocString(L"String");
  in_paramname_AdditionalOptions = SysAllocString(L"AdditionalOptions");
  in_paramname_Path = SysAllocString(L"Path");
  in_paramname_Flags = SysAllocString(L"Flags");
  in_paramname_Integer = SysAllocString(L"Integer");
  in_paramname_Integers = SysAllocString(L"Integers");
  in_paramname_ParentPath = SysAllocString(L"ParentPath");
  in_paramname_ParentDeviceType = SysAllocString(L"ParentDeviceType");
  in_paramname_PartitionStyle = SysAllocString(L"PartitionStyle");
  in_paramname_ParentAdditionalOptions = SysAllocString(L"ParentAdditionalOptions");
  in_paramname_DiskSignature = SysAllocString(L"DiskSignature");
  in_paramname_PartitionIdentifier = SysAllocString(L"PartitionIdentifier");
  in_paramname_CustomLocate = SysAllocString(L"CustomLocate");
  out_paramname_Object = SysAllocString(L"Object");
  ret_val_name = SysAllocString(L"ReturnValue");

  if (NULL == funcname_createobject || NULL == funcname_SetBooleanElement || NULL == funcname_SetDeviceElement ||
    NULL == funcname_SetFileDeviceElement || NULL == funcname_SetIntegerElement || NULL == funcname_SetIntegerListElement ||
    NULL == funcname_SetObjectElement || NULL == funcname_SetObjectListElement || NULL == funcname_SetPartitionDeviceElement ||
    NULL == funcname_SetPartitionDeviceElementWithFlags || NULL == funcname_SetQualifiedPartitionDeviceElement || NULL == funcname_SetStringElement ||
    NULL == funcname_SetVhdDeviceElement || NULL == in_paramname_Type || NULL == in_paramname_Id || NULL == in_paramname_Ids || NULL == in_paramname_Boolean ||
    NULL == in_paramname_DeviceType || NULL == in_paramname_String || NULL == in_paramname_AdditionalOptions || NULL == in_paramname_Path ||
    NULL == in_paramname_Flags || NULL == in_paramname_Integer || NULL == in_paramname_Integers || NULL == in_paramname_ParentPath || NULL == in_paramname_ParentDeviceType ||
    NULL == in_paramname_PartitionStyle || NULL == in_paramname_ParentAdditionalOptions || NULL == in_paramname_DiskSignature || NULL == in_paramname_PartitionIdentifier ||
    NULL == in_paramname_CustomLocate || NULL == out_paramname_Object || NULL == ret_val_name)
    goto do_exit;

  // [2] get shitloads of function (method) pointers from the CLASSES (NOT OBJECTS!!!)

  // get CreateObject method from class (BcdStore)
  if (S_OK != bsp->bwp->pBcdStoreClass->lpVtbl->GetMethod(bsp->bwp->pBcdStoreClass, funcname_createobject, 0, &pCreateObject, NULL))
    goto do_exit;
  if (NULL == pCreateObject)
    goto do_exit;

  // get SetBooleanElement method from class (BcdObject)

  GET_BCDOBJECT_METHOD_PTR(SetBooleanElement);
  GET_BCDOBJECT_METHOD_PTR(SetDeviceElement);
  GET_BCDOBJECT_METHOD_PTR(SetFileDeviceElement);
  GET_BCDOBJECT_METHOD_PTR(SetIntegerElement);
  GET_BCDOBJECT_METHOD_PTR(SetIntegerListElement);
  GET_BCDOBJECT_METHOD_PTR(SetObjectElement);
  GET_BCDOBJECT_METHOD_PTR(SetObjectListElement);
  GET_BCDOBJECT_METHOD_PTR(SetPartitionDeviceElement);
  GET_BCDOBJECT_METHOD_PTR(SetPartitionDeviceElementWithFlags);
  GET_BCDOBJECT_METHOD_PTR(SetQualifiedPartitionDeviceElement);
  GET_BCDOBJECT_METHOD_PTR(SetStringElement);
  GET_BCDOBJECT_METHOD_PTR(SetVhdDeviceElement);

  // Create/derive GUIDs

  if (!format_guid_as_widestring(guid_WindowsResume_Str, &guid_WindowsResume, true/*generate new GUID*/))
    goto do_exit;
  memcpy(&guid_Windows10, &guid_WindowsResume, sizeof(GUID));
  guid_Windows10.Data1++;
  memcpy(&guid_RecoverySequence, &guid_Windows10, sizeof(GUID));
  guid_RecoverySequence.Data1++;
  memcpy(&guid_DeviceOptionsRecovery, &guid_RecoverySequence, sizeof(GUID));
  guid_DeviceOptionsRecovery.Data1++;
  if (!format_guid_as_widestring(guid_Windows10_Str, &guid_Windows10, false/*do not generate, just format*/))
    goto do_exit;
  if (!format_guid_as_widestring(guid_RecoverySequence_Str, &guid_RecoverySequence, false/*do not generate, just format*/))
    goto do_exit;
  if (!format_guid_as_widestring(guid_DeviceOptionsRecovery_Str, &guid_DeviceOptionsRecovery, false/*do not generate, just format*/))
    goto do_exit;

  // Create "EMS settings"

  CREATE_BCD_OBJECT(0x20100000, L"{0ce4991b-e6b3-4b16-b23c-5e0d9250e5d9}");                             // {emssettings}
  BCD_SetBooleanElement(BcdLibraryBoolean_EmsEnabled, FALSE);
  RELEASE_CURRENT_BCD_OBJECT();

  // Create "RAM defects"

  CREATE_BCD_OBJECT(0x20100000, L"{5189b25c-5558-4bf2-bca4-289b11bd29e2}");                             // {badmemory}
  RELEASE_CURRENT_BCD_OBJECT();

  // Create "Debugger settings"

  CREATE_BCD_OBJECT(0x20100000, L"{4636856e-540f-4170-a130-a84776f4c654}");                             // {dbgsettings}
  BCD_SetIntegerElement(BcdLibraryInteger_DebuggerType, DebuggerLocal);
  RELEASE_CURRENT_BCD_OBJECT();

  // Create "Hypervisor settings"
  
  CREATE_BCD_OBJECT(0x20200003, L"{7ff607e0-4395-11db-b0de-0800200c9a66}");                             // {hypervisorsettings}
  BCD_SetIntegerElement(BcdOSLoaderInteger_HypervisorDebuggerType, DebuggerSerial);
  BCD_SetIntegerElement(BcdOSLoaderInteger_HypervisorDebuggerPortNumber, 1);
  BCD_SetIntegerElement(BcdOSLoaderInteger_HypervisorDebuggerBaudrate, 115200);
  RELEASE_CURRENT_BCD_OBJECT();

  // Create "Global settings"

  CREATE_BCD_OBJECT(0x20100000, L"{7ea2e1ac-2e61-4728-aaa3-896d9d0a9f0e}");                             // {globalsettings}
  if (!CreateSafeArrayStrings(&v_array, 3, L"{4636856e-540f-4170-a130-a84776f4c654}",                   // {dbgsettings}
                                           L"{0ce4991b-e6b3-4b16-b23c-5e0d9250e5d9}",                   // {emssettings}
                                           L"{5189b25c-5558-4bf2-bca4-289b11bd29e2}"))                  // {badmemory}
    goto do_exit;
  BCD_SetObjectListElement(BcdLibraryObjectList_InheritedObjects);
  RELEASE_CURRENT_BCD_OBJECT();

  // Create "Windows memory test program"

  CREATE_BCD_OBJECT(0x10200005, L"{b2721d73-1db4-4c62-bf78-c548a880142d}");                             // {memdiag}
  wsprintfW(w_str, L"\\Device\\HarddiskVolume%u", efi_partition_no);
  BCD_SetPartitionDeviceElement(BcdLibraryDevice_ApplicationDevice, PartitionDevice, L"", w_str);
  BCD_SetStringElement(BcdLibraryString_ApplicationPath, L"\\efi\\microsoft\\boot\\memtest.efi");
  BCD_SetStringElement(BcdLibraryString_Description, L"Windows memory diagnosis");
  MultiByteToWideChar(CP_UTF8, 0, locale, -1, w_str, sizeof(w_str));
  BCD_SetStringElement(BcdLibraryString_PreferredLocale, w_str);
  if (!CreateSafeArrayStrings(&v_array, 1, L"{7ea2e1ac-2e61-4728-aaa3-896d9d0a9f0e}"))                  // {globalsettings}
    goto do_exit;
  BCD_SetObjectListElement(BcdLibraryObjectList_InheritedObjects);
  BCD_SetBooleanElement(BcdLibraryBoolean_AllowBadMemoryAccess, TRUE);
  RELEASE_CURRENT_BCD_OBJECT();

  // Create "resumeloader settings"

  CREATE_BCD_OBJECT(0x20200004, L"{1afa9c49-16ab-4a5c-901b-212802da9460}");                             // {resumeloadersettings}
  if (!CreateSafeArrayStrings(&v_array, 1, L"{7ea2e1ac-2e61-4728-aaa3-896d9d0a9f0e}"))                  // {globalsettings}
    goto do_exit;
  BCD_SetObjectListElement(BcdLibraryObjectList_InheritedObjects);
  RELEASE_CURRENT_BCD_OBJECT();

  // Create "bootloader settings"

  CREATE_BCD_OBJECT(0x20200003, L"{6efb52bf-1766-41db-a6b3-0ee5eff72bd7}");                             // {bootloadersettings}
  if (!CreateSafeArrayStrings(&v_array, 2, L"{7ea2e1ac-2e61-4728-aaa3-896d9d0a9f0e}",                   // {globalsettings}
                                           L"{7ff607e0-4395-11db-b0de-0800200c9a66}"))                  // {hypervisorsettings}
    goto do_exit;
  BCD_SetObjectListElement(BcdLibraryObjectList_InheritedObjects);
  RELEASE_CURRENT_BCD_OBJECT();

  if (0 != recovery_partition)
  {
    // Create "Device options"

    CREATE_BCD_OBJECT(0x30000000, guid_DeviceOptionsRecovery_Str);
    BCD_SetStringElement(BcdLibraryString_Description, L"Windows Recovery");
    wsprintfW(w_str, L"\\Device\\HarddiskVolume%u", recovery_partition);
    BCD_SetPartitionDeviceElement(BcdDeviceInteger_SdiDevice, PartitionDevice, L"", w_str);
    BCD_SetStringElement(BcdDeviceInteger_SdiPath, L"\\Recovery\\WindowsRE\\boot.sdi");
    RELEASE_CURRENT_BCD_OBJECT();

    // Create "recovery sequence"

    CREATE_BCD_OBJECT(0x10200003, guid_RecoverySequence_Str);
    BCD_SetFileDeviceElement(BcdLibraryDevice_ApplicationDevice, RamdiskDevice, guid_DeviceOptionsRecovery_Str, L"\\Recovery\\WindowsRE\\Winre.wim",
      PartitionDevice, L"", w_str);
    BCD_SetFileDeviceElement(BcdOSLoaderDevice_OSDevice, RamdiskDevice, guid_DeviceOptionsRecovery_Str, L"\\Recovery\\WindowsRE\\Winre.wim",
      PartitionDevice, L"", w_str);
    BCD_SetStringElement(BcdLibraryString_ApplicationPath, L"\\windows\\system32\\winload.efi");
    BCD_SetStringElement(BcdLibraryString_Description, L"Windows Recovery Environment");
    MultiByteToWideChar(CP_UTF8, 0, locale, -1, w_str, sizeof(w_str));
    BCD_SetStringElement(BcdLibraryString_PreferredLocale, w_str);
    if (!CreateSafeArrayStrings(&v_array, 1, L"{6efb52bf-1766-41db-a6b3-0ee5eff72bd7}"))                  // {bootloadersettings}
      goto do_exit;
    BCD_SetObjectListElement(BcdLibraryObjectList_InheritedObjects);
    BCD_SetIntegerElement(BcdLibraryInteger_DisplayMessage, DisplayMessage_Recovery);
    BCD_SetStringElement(BcdOSLoaderString_SystemRoot, L"\\windows");
    BCD_SetIntegerElement(BcdOSLoaderInteger_NxPolicy, NxPolicyOptIn);
    BCD_SetIntegerElement(BcdOSLoaderInteger_BootMenuPolicy, 1);
    BCD_SetBooleanElement(BcdOSLoaderBoolean_WinPEMode, TRUE);
    BCD_SetBooleanElement(0x46000010, TRUE); // absolutely no clue what this is!?!?!?
    RELEASE_CURRENT_BCD_OBJECT();
  }

  // Create "Windows Resume" (hibernation)

  CREATE_BCD_OBJECT(0x10200004, guid_WindowsResume_Str);
  wsprintfW(w_str, L"\\Device\\HarddiskVolume%u", windows_partition_no);
  BCD_SetPartitionDeviceElement(BcdLibraryDevice_ApplicationDevice, PartitionDevice, L"", w_str);
  BCD_SetStringElement(BcdLibraryString_ApplicationPath, L"\\WINDOWS\\system32\\winresume.efi");
  BCD_SetStringElement(BcdLibraryString_Description, L"Windows Resume Application");
  MultiByteToWideChar(CP_UTF8, 0, locale, -1, w_str, sizeof(w_str));
  BCD_SetStringElement(BcdLibraryString_PreferredLocale, w_str);
  if (!CreateSafeArrayStrings(&v_array, 1, L"{1afa9c49-16ab-4a5c-901b-212802da9460}"))    //  {resumeloadersettings}
    goto do_exit;
  BCD_SetObjectListElement(BcdLibraryObjectList_InheritedObjects);
  if (!CreateSafeArrayStrings(&v_array, 1, guid_RecoverySequence_Str))    //  our recovery sequence
    goto do_exit;
  BCD_SetObjectListElement(BcdLibraryObjectList_RecoverySequence);
  BCD_SetBooleanElement(BcdLibraryBoolean_AutoRecoveryEnabled, TRUE);
  BCD_SetBooleanElement(BcdLibraryBoolean_IsolatedExecutionContext, TRUE);
  BCD_SetIntegerElement(BcdLibraryIntegerList_AllowedInMemorySettings, 0x15000075);
  wsprintfW(w_str, L"\\Device\\HarddiskVolume%u", windows_partition_no);
  BCD_SetPartitionDeviceElement(BcdOSLoaderDevice_OSDevice, PartitionDevice, L"", w_str);
  BCD_SetStringElement(BcdOSLoaderString_SystemRoot, L"\\hiberfil.sys");
  BCD_SetIntegerElement(BcdResumeInteger_BootMenuPolicy, 1/*standard*/);
  BCD_SetBooleanElement(BcdResumeBoolean_DebugOptionEnabled, FALSE);
  RELEASE_CURRENT_BCD_OBJECT();

  // Create "Windows 10" start entry

  CREATE_BCD_OBJECT(0x10200003, guid_Windows10_Str);
  wsprintfW(w_str, L"\\Device\\HarddiskVolume%u", windows_partition_no);
  BCD_SetPartitionDeviceElement(BcdLibraryDevice_ApplicationDevice, PartitionDevice, L"", w_str);
  BCD_SetStringElement(BcdLibraryString_ApplicationPath, L"\\WINDOWS\\system32\\winload.efi");
  BCD_SetStringElement(BcdLibraryString_Description, L"Windows 10");
  MultiByteToWideChar(CP_UTF8, 0, locale, -1, w_str, sizeof(w_str));
  BCD_SetStringElement(BcdLibraryString_PreferredLocale, w_str);
  if (!CreateSafeArrayStrings(&v_array, 1, L"{6efb52bf-1766-41db-a6b3-0ee5eff72bd7}"))    //  {bootloadersettings}
    goto do_exit;
  BCD_SetObjectListElement(BcdLibraryObjectList_InheritedObjects);
  if (!CreateSafeArrayStrings(&v_array, 1, guid_RecoverySequence_Str))    //  our recovery sequence
    goto do_exit;
  BCD_SetObjectListElement(BcdLibraryObjectList_RecoverySequence);
  BCD_SetIntegerElement(BcdLibraryInteger_DisplayMessageOverride, 3); // display message override: recovery
  BCD_SetBooleanElement(BcdLibraryBoolean_AutoRecoveryEnabled, TRUE);
  BCD_SetBooleanElement(BcdLibraryBoolean_IsolatedExecutionContext, TRUE);
  BCD_SetIntegerElement(BcdLibraryIntegerList_AllowedInMemorySettings, 0x15000075);
  wsprintfW(w_str, L"\\Device\\HarddiskVolume%u", windows_partition_no);
  BCD_SetPartitionDeviceElement(BcdOSLoaderDevice_OSDevice, PartitionDevice, L"", w_str);
  BCD_SetStringElement(BcdOSLoaderString_SystemRoot, L"\\WINDOWS");
  if (!CreateSafeArrayStrings(&v_array, 1, guid_WindowsResume_Str))    //  our resume object
    goto do_exit;
  BCD_SetObjectListElement(BcdOSLoaderObject_AssociatedResumeObject);
  BCD_SetIntegerElement(BcdOSLoaderInteger_NxPolicy, NxPolicyOptIn);
  BCD_SetIntegerElement(BcdOSLoaderInteger_BootMenuPolicy, 1/*standard*/);
  RELEASE_CURRENT_BCD_OBJECT();

  // Create "Windows Boot Manager" entry
  
  CREATE_BCD_OBJECT(0x10100002, L"{9dea862c-5cdd-4e70-acc1-f32b344d4795}");
  wsprintfW(w_str, L"\\Device\\HarddiskVolume%u", efi_partition_no);
  BCD_SetPartitionDeviceElement(BcdLibraryDevice_ApplicationDevice, PartitionDevice, L"", w_str);
  BCD_SetStringElement(BcdLibraryString_ApplicationPath, L"\\EFI\\MICROSOFT\\BOOT\\BOOTMGFW.EFI");
  BCD_SetStringElement(BcdLibraryString_Description, L"Windows Boot Manager");
  MultiByteToWideChar(CP_UTF8, 0, locale, -1, w_str, sizeof(w_str));
  BCD_SetStringElement(BcdLibraryString_PreferredLocale, w_str);
  if (!CreateSafeArrayStrings(&v_array, 1, L"{7ea2e1ac-2e61-4728-aaa3-896d9d0a9f0e}"))                  // {globalsettings}
    goto do_exit;
  BCD_SetObjectListElement(BcdLibraryObjectList_InheritedObjects);
  BCD_SetObjectElement(BcdBootMgrObject_DefaultObject, guid_Windows10_Str);               // our Windows 10 start menu entry
  BCD_SetObjectElement(BcdBootMgrObject_ResumeObject, guid_WindowsResume_Str);            // our resume object
  if (!CreateSafeArrayStrings(&v_array, 1, guid_Windows10_Str))                  // {globalsettings}
    goto do_exit;
  BCD_SetObjectListElement(BcdBootMgrObjectList_DisplayOrder);
  if (!CreateSafeArrayStrings(&v_array, 1, L"{b2721d73-1db4-4c62-bf78-c548a880142d}"))                  // {memdiag}
    goto do_exit;
  BCD_SetObjectListElement(BcdBootMgrObjectList_ToolsDisplayOrder);
  BCD_SetIntegerElement(BcdBootMgrInteger_Timeout, 30/*30 seconds*/);
  RELEASE_CURRENT_BCD_OBJECT();

  ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  
  overall_result = true;

do_exit:

  VariantClear(&obj_this_pointer);
  VariantClear(&v);
  VariantClear(&v_array);

  FREE_COM_PTR(pCreateObject);
  FREE_COM_PTR(pSetBooleanElement);
  FREE_COM_PTR(pSetDeviceElement);
  FREE_COM_PTR(pSetFileDeviceElement);
  FREE_COM_PTR(pSetIntegerElement);
  FREE_COM_PTR(pSetIntegerListElement);
  FREE_COM_PTR(pSetObjectElement);
  FREE_COM_PTR(pSetObjectListElement);
  FREE_COM_PTR(pSetPartitionDeviceElement);
  FREE_COM_PTR(pSetPartitionDeviceElementWithFlags);
  FREE_COM_PTR(pSetQualifiedPartitionDeviceElement);
  FREE_COM_PTR(pSetStringElement);
  FREE_COM_PTR(pSetVhdDeviceElement);
  FREE_COM_PTR(pInParam);
  FREE_COM_PTR(pOutParam);
  FREE_COM_PTR(pBcdObject);
  FREE_SYS_STRING(funcname_createobject );
  FREE_SYS_STRING(funcname_SetBooleanElement );
  FREE_SYS_STRING(funcname_SetDeviceElement );
  FREE_SYS_STRING(funcname_SetFileDeviceElement );
  FREE_SYS_STRING(funcname_SetIntegerElement );
  FREE_SYS_STRING(funcname_SetIntegerListElement );
  FREE_SYS_STRING(funcname_SetObjectElement );
  FREE_SYS_STRING(funcname_SetObjectListElement );
  FREE_SYS_STRING(funcname_SetPartitionDeviceElement );
  FREE_SYS_STRING(funcname_SetPartitionDeviceElementWithFlags );
  FREE_SYS_STRING(funcname_SetQualifiedPartitionDeviceElement );
  FREE_SYS_STRING(funcname_SetStringElement );
  FREE_SYS_STRING(funcname_SetVhdDeviceElement );
  FREE_SYS_STRING(in_paramname_Type );
  FREE_SYS_STRING(in_paramname_Id );
  FREE_SYS_STRING(in_paramname_Ids );
  FREE_SYS_STRING(in_paramname_Boolean );
  FREE_SYS_STRING(in_paramname_DeviceType );
  FREE_SYS_STRING(in_paramname_String );
  FREE_SYS_STRING(in_paramname_AdditionalOptions );
  FREE_SYS_STRING(in_paramname_Path );
  FREE_SYS_STRING(in_paramname_Flags );
  FREE_SYS_STRING(in_paramname_Integer );
  FREE_SYS_STRING(in_paramname_Integers );
  FREE_SYS_STRING(in_paramname_ParentPath );
  FREE_SYS_STRING(in_paramname_ParentDeviceType );
  FREE_SYS_STRING(in_paramname_PartitionStyle );
  FREE_SYS_STRING(in_paramname_ParentAdditionalOptions );
  FREE_SYS_STRING(in_paramname_DiskSignature );
  FREE_SYS_STRING(in_paramname_PartitionIdentifier );
  FREE_SYS_STRING(in_paramname_CustomLocate );
  FREE_SYS_STRING(out_paramname_Object );
  FREE_SYS_STRING(ret_val_name );

  return overall_result;
}

#endif // _WINDOWS

/*

Windows-Start-Manager
---------------------
Bezeichner              {bootmgr}
device                  unknown
path                    \EFI\MICROSOFT\BOOT\BOOTMGFW.EFI
description             Windows Boot Manager
locale                  de-DE
inherit                 {globalsettings}
default                 {default}
resumeobject            {75d876ee-6a3d-11ea-8d71-809133f4eea6}
displayorder            {default}
                        {75d876ed-6a3d-11ea-8d71-809133f4eea6}
toolsdisplayorder       {memdiag}
timeout                 30

Windows-Start-Manager
---------------------
Bezeichner              {9dea862c-5cdd-4e70-acc1-f32b344d4795}
device                  unknown
path                    \EFI\MICROSOFT\BOOT\BOOTMGFW.EFI
description             Windows Boot Manager
locale                  de-DE
inherit                 {7ea2e1ac-2e61-4728-aaa3-896d9d0a9f0e}
default                 {75d876ef-6a3d-11ea-8d71-809133f4eea6}
resumeobject            {75d876ee-6a3d-11ea-8d71-809133f4eea6}
displayorder            {75d876ef-6a3d-11ea-8d71-809133f4eea6}
                        {75d876ed-6a3d-11ea-8d71-809133f4eea6}
toolsdisplayorder       {b2721d73-1db4-4c62-bf78-c548a880142d}
timeout                 30

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Windows-Startladeprogramm
-------------------------
Bezeichner              {default}
device                  unknown
path                    \WINDOWS\system32\winload.efi
description             Windows 10
locale                  de-DE
inherit                 {bootloadersettings}
recoverysequence        {75d876f1-6a3d-11ea-8d71-809133f4eea6}
displaymessageoverride  Recovery
recoveryenabled         Yes
isolatedcontext         Yes
allowedinmemorysettings 0x15000075
osdevice                unknown
systemroot              \WINDOWS
resumeobject            {75d876ee-6a3d-11ea-8d71-809133f4eea6}
nx                      OptIn
bootmenupolicy          Standard

Windows-Startladeprogramm
-------------------------
Bezeichner              {75d876ef-6a3d-11ea-8d71-809133f4eea6}
device                  unknown
path                    \WINDOWS\system32\winload.efi
description             Windows 10
locale                  de-DE
inherit                 {6efb52bf-1766-41db-a6b3-0ee5eff72bd7}
recoverysequence        {75d876f1-6a3d-11ea-8d71-809133f4eea6}
displaymessageoverride  Recovery
recoveryenabled         Yes
isolatedcontext         Yes
allowedinmemorysettings 0x15000075
osdevice                unknown
systemroot              \WINDOWS
resumeobject            {75d876ee-6a3d-11ea-8d71-809133f4eea6}
nx                      OptIn
bootmenupolicy          Standard

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Windows-Startladeprogramm     ***FINISHED***   (Windows RE)
-------------------------
Bezeichner              {75d876f1-6a3d-11ea-8d71-809133f4eea6}
device                  ramdisk=[unknown]\Recovery\WindowsRE\Winre.wim,{75d876f2-6a3d-11ea-8d71-809133f4eea6}
path                    \windows\system32\winload.efi
description             Windows Recovery Environment
locale                  de-DE
inherit                 {bootloadersettings}
displaymessage          Recovery
osdevice                ramdisk=[unknown]\Recovery\WindowsRE\Winre.wim,{75d876f2-6a3d-11ea-8d71-809133f4eea6}
systemroot              \windows
nx                      OptIn
bootmenupolicy          Standard
winpe                   Yes

Windows-Startladeprogramm
-------------------------
Bezeichner              {75d876f1-6a3d-11ea-8d71-809133f4eea6}
device                  ramdisk=[unknown]\Recovery\WindowsRE\Winre.wim,{75d876f2-6a3d-11ea-8d71-809133f4eea6}
path                    \windows\system32\winload.efi
description             Windows Recovery Environment
locale                  de-DE
inherit                 {6efb52bf-1766-41db-a6b3-0ee5eff72bd7}
displaymessage          Recovery
osdevice                ramdisk=[unknown]\Recovery\WindowsRE\Winre.wim,{75d876f2-6a3d-11ea-8d71-809133f4eea6}
systemroot              \windows
nx                      OptIn
bootmenupolicy          Standard
winpe                   Yes
custom:46000010         Yes

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Wiederaufnahme aus dem Ruhezustand
----------------------------------
Bezeichner              {75d876ee-6a3d-11ea-8d71-809133f4eea6}
device                  unknown
path                    \WINDOWS\system32\winresume.efi
description             Windows Resume Application
locale                  de-DE
inherit                 {resumeloadersettings}
recoverysequence        {75d876f1-6a3d-11ea-8d71-809133f4eea6}
recoveryenabled         Yes
isolatedcontext         Yes
allowedinmemorysettings 0x15000075
filedevice              unknown
filepath                \hiberfil.sys
bootmenupolicy          Standard
debugoptionenabled      No

Wiederaufnahme aus dem Ruhezustand
----------------------------------
Bezeichner              {75d876ee-6a3d-11ea-8d71-809133f4eea6}
device                  unknown
path                    \WINDOWS\system32\winresume.efi
description             Windows Resume Application
locale                  de-DE
inherit                 {1afa9c49-16ab-4a5c-901b-212802da9460}
recoverysequence        {75d876f1-6a3d-11ea-8d71-809133f4eea6}
recoveryenabled         Yes
isolatedcontext         Yes
allowedinmemorysettings 0x15000075
filedevice              unknown
filepath                \hiberfil.sys
bootmenupolicy          Standard
debugoptionenabled      No

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Windows-Speichertestprogramm    ***FINISHED***
----------------------------
Bezeichner              {memdiag}
device                  unknown
path                    \EFI\Microsoft\Boot\memtest.efi
description             Windows-Speicherdiagnose
locale                  de-DE
inherit                 {globalsettings}
badmemoryaccess         Yes

Windows-Speichertestprogramm
----------------------------
Bezeichner              {b2721d73-1db4-4c62-bf78-c548a880142d}
device                  unknown
path                    \EFI\Microsoft\Boot\memtest.efi
description             Windows-Speicherdiagnose
locale                  de-DE
inherit                 {7ea2e1ac-2e61-4728-aaa3-896d9d0a9f0e}
badmemoryaccess         Yes

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

EMS-Einstellungen       ***FINISHED***
-----------------
Bezeichner              {emssettings}
bootems                 No

EMS-Einstellungen
-----------------
Bezeichner              {0ce4991b-e6b3-4b16-b23c-5e0d9250e5d9}
bootems                 No

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Debuggereinstellungen   ***FINISHED***
---------------------
Bezeichner              {dbgsettings}
debugtype               Local

Debuggereinstellungen
---------------------
Bezeichner              {4636856e-540f-4170-a130-a84776f4c654}
debugtype               Local

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

RAM-Defekte             ***FINISHED***
-----------
Bezeichner              {badmemory}

RAM-Defekte
-----------
Bezeichner              {5189b25c-5558-4bf2-bca4-289b11bd29e2}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Globale Einstellungen   ***FINISHED***
---------------------
Bezeichner              {globalsettings}
inherit                 {dbgsettings}
                        {emssettings}
                        {badmemory}

Globale Einstellungen
---------------------
Bezeichner              {7ea2e1ac-2e61-4728-aaa3-896d9d0a9f0e}
inherit                 {4636856e-540f-4170-a130-a84776f4c654}
                        {0ce4991b-e6b3-4b16-b23c-5e0d9250e5d9}
                        {5189b25c-5558-4bf2-bca4-289b11bd29e2}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Startladeprogramm-Einstellungen   ***FINISHED***
-------------------------------
Bezeichner              {bootloadersettings}
inherit                 {globalsettings}
                        {hypervisorsettings}

Startladeprogramm-Einstellungen
-------------------------------
Bezeichner              {6efb52bf-1766-41db-a6b3-0ee5eff72bd7}
inherit                 {7ea2e1ac-2e61-4728-aaa3-896d9d0a9f0e}
                        {7ff607e0-4395-11db-b0de-0800200c9a66}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Hypervisoreinstellungen ***FINISHED***
-----------------------
Bezeichner              {hypervisorsettings}
hypervisordebugtype     Serial
hypervisordebugport     1
hypervisorbaudrate      115200

Hypervisoreinstellungen
-----------------------
Bezeichner              {7ff607e0-4395-11db-b0de-0800200c9a66}
hypervisordebugtype     Serial
hypervisordebugport     1
hypervisorbaudrate      115200

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Einstellungen zur Ladeprogrammfortsetzung       ***FINISHED***
-----------------------------------------
Bezeichner              {resumeloadersettings}
inherit                 {globalsettings}

Einstellungen zur Ladeprogrammfortsetzung
-----------------------------------------
Bezeichner              {1afa9c49-16ab-4a5c-901b-212802da9460}
inherit                 {7ea2e1ac-2e61-4728-aaa3-896d9d0a9f0e}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Ger�teoptionen          ***FINISHED*** (wird von WinRE referenziert als AdditionalOptions)
--------------
Bezeichner              {75d876f2-6a3d-11ea-8d71-809133f4eea6}
description             Windows Recovery
ramdisksdidevice        unknown
ramdisksdipath          \Recovery\WindowsRE\boot.sdi

Ger�teoptionen
--------------
Bezeichner              {75d876f2-6a3d-11ea-8d71-809133f4eea6}
description             Windows Recovery
ramdisksdidevice        unknown
ramdisksdipath          \Recovery\WindowsRE\boot.sdi

*/
