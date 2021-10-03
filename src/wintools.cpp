#ifdef _WINDOWS

/**
 * Source code taken from: https://stackoverflow.com/questions/1453497/discover-if-user-has-admin-rights
 */

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

extern "C" bool IsUserAdmin(void)
{
  struct Data
  {
    PACL   pACL;
    PSID   psidAdmin;
    HANDLE hToken;
    HANDLE hImpersonationToken;
    PSECURITY_DESCRIPTOR     psdAdmin;
    Data() : pACL(NULL), psidAdmin(NULL), hToken(NULL),
      hImpersonationToken(NULL), psdAdmin(NULL)
    {}
    ~Data()
    {
      if (pACL)
        LocalFree(pACL);
      if (psdAdmin)
        LocalFree(psdAdmin);
      if (psidAdmin)
        FreeSid(psidAdmin);
      if (hImpersonationToken)
        CloseHandle(hImpersonationToken);
      if (hToken)
        CloseHandle(hToken);
    }
  } data;

  BOOL   fReturn = FALSE;
  DWORD  dwStatus;
  DWORD  dwAccessMask;
  DWORD  dwAccessDesired;
  DWORD  dwACLSize;
  DWORD  dwStructureSize = sizeof(PRIVILEGE_SET);

  PRIVILEGE_SET   ps;
  GENERIC_MAPPING GenericMapping;
  SID_IDENTIFIER_AUTHORITY SystemSidAuthority = SECURITY_NT_AUTHORITY;

  const DWORD ACCESS_READ = 1;
  const DWORD ACCESS_WRITE = 2;

  if (!OpenThreadToken(GetCurrentThread(), TOKEN_DUPLICATE | TOKEN_QUERY, TRUE, &data.hToken))
  {
    if (GetLastError() != ERROR_NO_TOKEN)
      return false;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY, &data.hToken))
      return false;
  }

  if (!DuplicateToken(data.hToken, SecurityImpersonation, &data.hImpersonationToken))
    return false;

  if (!AllocateAndInitializeSid(&SystemSidAuthority, 2,
    SECURITY_BUILTIN_DOMAIN_RID,
    DOMAIN_ALIAS_RID_ADMINS,
    0, 0, 0, 0, 0, 0, &data.psidAdmin))
    return false;

  data.psdAdmin = LocalAlloc(LPTR, SECURITY_DESCRIPTOR_MIN_LENGTH);
  if (data.psdAdmin == NULL)
    return false;

  if (!InitializeSecurityDescriptor(data.psdAdmin, SECURITY_DESCRIPTOR_REVISION))
    return false;

  // Compute size needed for the ACL.
  dwACLSize = sizeof(ACL) + sizeof(ACCESS_ALLOWED_ACE) + GetLengthSid(data.psidAdmin) - sizeof(DWORD);

  data.pACL = (PACL)LocalAlloc(LPTR, dwACLSize);
  if (data.pACL == NULL)
    return false;

  if (!InitializeAcl(data.pACL, dwACLSize, ACL_REVISION2))
    return false;

  dwAccessMask = ACCESS_READ | ACCESS_WRITE;

  if (!AddAccessAllowedAce(data.pACL, ACL_REVISION2, dwAccessMask, data.psidAdmin))
    return false;

  if (!SetSecurityDescriptorDacl(data.psdAdmin, TRUE, data.pACL, FALSE))
    return false;

  // AccessCheck validates a security descriptor somewhat; set the group
  // and owner so that enough of the security descriptor is filled out 
  // to make AccessCheck happy.

  SetSecurityDescriptorGroup(data.psdAdmin, data.psidAdmin, FALSE);
  SetSecurityDescriptorOwner(data.psdAdmin, data.psidAdmin, FALSE);

  if (!IsValidSecurityDescriptor(data.psdAdmin))
    return false;

  dwAccessDesired = ACCESS_READ;

  GenericMapping.GenericRead = ACCESS_READ;
  GenericMapping.GenericWrite = ACCESS_WRITE;
  GenericMapping.GenericExecute = 0;
  GenericMapping.GenericAll = ACCESS_READ | ACCESS_WRITE;

  if (!AccessCheck(data.psdAdmin, data.hImpersonationToken, dwAccessDesired,
    &GenericMapping, &ps, &dwStructureSize, &dwStatus,
    &fReturn))
  {
    return false;
  }

  return fReturn;
}


/******************************************************************************\
*       This is a part of the Microsoft Source Code Samples.
*       Copyright 1995 - 1997 Microsoft Corporation.
*       All rights reserved.
*       This source code is only intended as a supplement to
*       Microsoft Development Tools and/or WinHelp documentation.
*       See these sources for detailed information regarding the
*       Microsoft samples programs.
\******************************************************************************/

/*++
Copyright (c) 1997  Microsoft Corporation
Module Name:
    pipeex.c
Abstract:
    CreatePipe-like function that lets one or both handles be overlapped
Author:
    Dave Hart  Summer 1997
Revision History:
--*/

#include <windows.h>
#include <stdio.h>

static volatile long PipeSerialNumber;

extern "C" BOOL
APIENTRY
MyCreatePipeEx(
  OUT LPHANDLE lpReadPipe,
  OUT LPHANDLE lpWritePipe,
  IN LPSECURITY_ATTRIBUTES lpPipeAttributes,
  IN DWORD nSize,
  DWORD dwReadMode,
  DWORD dwWriteMode
)

/*++
Routine Description:
    The CreatePipeEx API is used to create an anonymous pipe I/O device.
    Unlike CreatePipe FILE_FLAG_OVERLAPPED may be specified for one or
    both handles.
    Two handles to the device are created.  One handle is opened for
    reading and the other is opened for writing.  These handles may be
    used in subsequent calls to ReadFile and WriteFile to transmit data
    through the pipe.
Arguments:
    lpReadPipe - Returns a handle to the read side of the pipe.  Data
        may be read from the pipe by specifying this handle value in a
        subsequent call to ReadFile.
    lpWritePipe - Returns a handle to the write side of the pipe.  Data
        may be written to the pipe by specifying this handle value in a
        subsequent call to WriteFile.
    lpPipeAttributes - An optional parameter that may be used to specify
        the attributes of the new pipe.  If the parameter is not
        specified, then the pipe is created without a security
        descriptor, and the resulting handles are not inherited on
        process creation.  Otherwise, the optional security attributes
        are used on the pipe, and the inherit handles flag effects both
        pipe handles.
    nSize - Supplies the requested buffer size for the pipe.  This is
        only a suggestion and is used by the operating system to
        calculate an appropriate buffering mechanism.  A value of zero
        indicates that the system is to choose the default buffering
        scheme.
Return Value:
    TRUE - The operation was successful.
    FALSE/NULL - The operation failed. Extended error status is available
        using GetLastError.
--*/

{
  HANDLE ReadPipeHandle, WritePipeHandle;
  DWORD dwError;
  UCHAR PipeNameBuffer[MAX_PATH];

  //
  // Only one valid OpenMode flag - FILE_FLAG_OVERLAPPED
  //

  if ((dwReadMode | dwWriteMode) & (~FILE_FLAG_OVERLAPPED)) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }

  //
  //  Set the default timeout to 120 seconds
  //

  if (nSize == 0) {
    nSize = 4096;
  }

  snprintf((char*)PipeNameBuffer,sizeof(PipeNameBuffer),
    "\\\\.\\Pipe\\RemoteExeAnon.%08x.%08x",
    GetCurrentProcessId(),
    InterlockedIncrement(&PipeSerialNumber)
  );

  ReadPipeHandle = CreateNamedPipeA(
    (const char *)PipeNameBuffer,
    PIPE_ACCESS_INBOUND | dwReadMode,
    PIPE_TYPE_BYTE | PIPE_WAIT,
    1,             // Number of pipes
    nSize,         // Out buffer size
    nSize,         // In buffer size
    120 * 1000,    // Timeout in ms
    lpPipeAttributes
  );

  if (!ReadPipeHandle) {
    return FALSE;
  }

  WritePipeHandle = CreateFileA(
    (const char*)PipeNameBuffer,
    GENERIC_WRITE,
    0,                         // No sharing
    lpPipeAttributes,
    OPEN_EXISTING,
    FILE_ATTRIBUTE_NORMAL | dwWriteMode,
    NULL                       // Template file
  );

  if (INVALID_HANDLE_VALUE == WritePipeHandle) {
    dwError = GetLastError();
    CloseHandle(ReadPipeHandle);
    SetLastError(dwError);
    return FALSE;
  }

  *lpReadPipe = ReadPipeHandle;
  *lpWritePipe = WritePipeHandle;
  return(TRUE);
}

#endif // _WINDOWS
