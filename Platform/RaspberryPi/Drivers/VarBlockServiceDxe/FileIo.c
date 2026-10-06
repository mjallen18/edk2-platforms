/** @file
 *
 *  Copyright (c) 2018, Andrei Warkentin <andrey.warkentin@gmail.com>
 *  Copyright (c) 2007-2009, Intel Corporation. All rights reserved.
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include "VarBlockService.h"
#include <Library/BaseMemoryLib.h>
#include <Pi/PiFirmwareFile.h>

// Compare in bounded chunks, without allocating during reset-time saving.
EFI_STATUS
FileVerify (
  IN EFI_FILE_PROTOCOL *File,
  IN UINTN             Offset,
  IN UINTN             Buffer,
  IN UINTN             Size
  )
{
  EFI_STATUS Status;
  UINT8      ReadBuffer[4096];
  UINTN      Count;
  UINTN      Requested;

  Status = File->SetPosition (File, Offset);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  while (Size != 0) {
    Requested = MIN (Size, sizeof (ReadBuffer));
    Count = Requested;
    Status = File->Read (File, &Count, ReadBuffer);
    if (EFI_ERROR (Status)) {
      return Status;
    }
    if (Count != Requested || CompareMem (ReadBuffer, (VOID *)Buffer, Count) != 0) {
      return EFI_DEVICE_ERROR;
    }
    Size -= Count;
    Buffer += Count;
  }
  return EFI_SUCCESS;
}


// The SEC file executes in place in RAM. Its mailbox reply buffer and library
// globals are modified before DXE, so comparing its payload to disk rejects the
// very firmware we booted. Match the FV/SEC headers and all bytes after SEC,
// including the immutable compressed DXE image. Never write any of these bytes.
// This is a target-identity check, not cryptographic firmware authentication.
STATIC
EFI_STATUS
FileVerifyFirmware (
  IN EFI_FILE_PROTOCOL *File
  )
{
  EFI_FIRMWARE_VOLUME_HEADER *Fv;
  EFI_FFS_FILE_HEADER        *Sec;
  EFI_FFS_FILE_HEADER        *Dxe;
  EFI_STATUS                 Status;
  UINTN                      FvSize;
  UINTN                      FileOffset;
  UINTN                      SecOffset;
  UINTN                      SecSize;
  UINTN                      TailOffset;
  UINTN                      DxeOffset;
  UINTN                      DxeSize;

  Fv = (EFI_FIRMWARE_VOLUME_HEADER *)(UINTN)FixedPcdGet64 (PcdFvBaseAddress);
  FvSize = FixedPcdGet32 (PcdFvSize);
  FileOffset = (UINTN)(FixedPcdGet64 (PcdFvBaseAddress) - FixedPcdGet64 (PcdFdBaseAddress));

  // Only accept the pinned RPi5 layout: ordinary SEC followed by FV_IMAGE.
  // Bound every length before pointer arithmetic; fail closed on other layouts.
  if (FvSize < sizeof (*Fv) || Fv->Signature != EFI_FVH_SIGNATURE ||
      Fv->Revision != EFI_FVH_REVISION || Fv->FvLength != FvSize ||
      Fv->ExtHeaderOffset != 0 || Fv->HeaderLength < sizeof (*Fv) ||
      Fv->HeaderLength > FvSize) {
    return EFI_VOLUME_CORRUPTED;
  }
  SecOffset = ALIGN_VALUE ((UINTN)Fv->HeaderLength, 8);
  if (SecOffset > FvSize || FvSize - SecOffset < sizeof (*Sec)) {
    return EFI_VOLUME_CORRUPTED;
  }
  Sec = (EFI_FFS_FILE_HEADER *)((UINT8 *)Fv + SecOffset);
  SecSize = (UINTN)Sec->Size[0] | ((UINTN)Sec->Size[1] << 8) | ((UINTN)Sec->Size[2] << 16);
  if (Sec->Type != EFI_FV_FILETYPE_SECURITY_CORE ||
      (Sec->Attributes & FFS_ATTRIB_LARGE_FILE) != 0 ||
      SecSize <= sizeof (*Sec) || SecSize > FvSize - SecOffset) {
    return EFI_VOLUME_CORRUPTED;
  }
  TailOffset = SecOffset + SecSize;
  DxeOffset = ALIGN_VALUE (TailOffset, 8);
  if (DxeOffset > FvSize || FvSize - DxeOffset < sizeof (*Dxe)) {
    return EFI_VOLUME_CORRUPTED;
  }
  Dxe = (EFI_FFS_FILE_HEADER *)((UINT8 *)Fv + DxeOffset);
  DxeSize = (UINTN)Dxe->Size[0] | ((UINTN)Dxe->Size[1] << 8) | ((UINTN)Dxe->Size[2] << 16);
  if (Dxe->Type != EFI_FV_FILETYPE_FIRMWARE_VOLUME_IMAGE ||
      (Dxe->Attributes & FFS_ATTRIB_LARGE_FILE) != 0 ||
      DxeSize <= sizeof (*Dxe) || DxeSize > FvSize - DxeOffset) {
    return EFI_VOLUME_CORRUPTED;
  }
  Status = FileVerify (File, FileOffset, (UINTN)Fv, SecOffset + sizeof (*Sec));
  if (EFI_ERROR (Status)) {
    return Status;
  }
  return FileVerify (File, FileOffset + TailOffset, (UINTN)Fv + TailOffset,
             FvSize - TailOffset);
}


EFI_STATUS
FileWrite (
  IN EFI_FILE_PROTOCOL *File,
  IN UINTN Offset,
  IN UINTN Buffer,
  IN UINTN Size
  )
{
  EFI_STATUS Status;
  UINTN      Written;

  Status = File->SetPosition (File, Offset);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Written = Size;
  Status = File->Write (File, &Written, (VOID*)Buffer);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  if (Written != Size) {
    return EFI_DEVICE_ERROR;
  }
  Status = File->Flush (File);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  return FileVerify (File, Offset, Buffer, Size);
}


EFI_STATUS
FileClose (
  IN  EFI_FILE_PROTOCOL *File
  )
{
  return File->Close (File);
}


EFI_STATUS
FileOpen (
  IN  EFI_DEVICE_PATH_PROTOCOL *Device,
  IN  CHAR16 *MappedFile,
  OUT EFI_FILE_PROTOCOL **File,
  IN  UINT64 OpenMode
  )
{
  EFI_HANDLE                        Handle;
  EFI_FILE_HANDLE                   Root;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL   *Volume;
  EFI_STATUS                        Status;

  *File = NULL;

  Status = gBS->LocateDevicePath (
                  &gEfiSimpleFileSystemProtocolGuid,
                  &Device,
                  &Handle
                );

  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->HandleProtocol (
                  Handle,
                  &gEfiSimpleFileSystemProtocolGuid,
                  (VOID**)&Volume
                );
  ASSERT_EFI_ERROR (Status);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Open the root directory of the volume
  //
  Root = NULL;
  Status = Volume->OpenVolume (
                     Volume,
                     &Root
                   );
  ASSERT_EFI_ERROR (Status);
  ASSERT (Root != NULL);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  if (Root == NULL) {
    return EFI_DEVICE_ERROR;
  }

  //
  // Open file
  //
  Status = Root->Open (
                   Root,
                   File,
                   MappedFile,
                   OpenMode,
                   0
                 );
  if (EFI_ERROR (Status)) {
    *File = NULL;
  }

  //
  // Close the Root directory
  //
  Root->Close (Root);
  return Status;
}


EFI_STATUS
CheckStore (
  IN  EFI_HANDLE SimpleFileSystemHandle,
  OUT EFI_DEVICE_PATH_PROTOCOL **Device
  )
{
  EFI_STATUS Status;
  EFI_BLOCK_IO_PROTOCOL *BlkIo;
  EFI_FILE_PROTOCOL *File;
  UINT64 FileSize;
  EFI_STATUS CloseStatus;

  *Device = NULL;
  Status = gBS->HandleProtocol (
                  SimpleFileSystemHandle,
                  &gEfiBlockIoProtocolGuid,
                  (VOID*)&BlkIo
                );

  if (EFI_ERROR (Status)) {
    goto ErrHandle;
  }
  if (!BlkIo->Media->MediaPresent) {
    DEBUG ((DEBUG_ERROR, "FwhMappedFile: Media not present!\n"));
    Status = EFI_NO_MEDIA;
    goto ErrHandle;
  }
  if (BlkIo->Media->ReadOnly) {
    DEBUG ((DEBUG_ERROR, "FwhMappedFile: Media is read-only!\n"));
    Status = EFI_ACCESS_DENIED;
    goto ErrHandle;
  }

  Status = FileOpen (DevicePathFromHandle (SimpleFileSystemHandle),
             mFvInstance->MappedFile, &File,
             EFI_FILE_MODE_READ);
  if (EFI_ERROR (Status)) {
    goto ErrHandle;
  }

  // A same-named file on another disk is not enough. Match the stable firmware
  // contents and original variable store before selecting a writable target.
  Status = File->SetPosition (File, MAX_UINT64);
  if (!EFI_ERROR (Status)) {
    Status = File->GetPosition (File, &FileSize);
  }
  // GenFds may omit the reserved tail after the last populated FD region.
  // Require all variable bytes, not padding that is not part of the payload.
  if (!EFI_ERROR (Status) &&
      (FileSize < (UINT64)mFvInstance->Offset + mFvInstance->FvLength ||
       FileSize > FixedPcdGet32 (PcdFdSize))) {
    Status = EFI_VOLUME_CORRUPTED;
  }
  if (!EFI_ERROR (Status)) {
    Status = FileVerifyFirmware (File);
  }
  if (!EFI_ERROR (Status)) {
    Status = VerifyBootVariableStore (File);
  }
  CloseStatus = FileClose (File);
  if (!EFI_ERROR (Status)) {
    Status = CloseStatus;
  }
  if (EFI_ERROR (Status)) {
    goto ErrHandle;
  }
  *Device = DuplicateDevicePath (DevicePathFromHandle (SimpleFileSystemHandle));

  ASSERT (*Device != NULL);
  if (*Device == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
  }

ErrHandle:
  return Status;
}


EFI_STATUS
CheckStoreExists (
  IN  EFI_DEVICE_PATH_PROTOCOL *Device
  )
{
  EFI_HANDLE Handle;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Volume;
  EFI_STATUS Status;

  Status = gBS->LocateDevicePath (
                  &gEfiSimpleFileSystemProtocolGuid,
                  &Device,
                  &Handle
                );

  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->HandleProtocol (
                  Handle,
                  &gEfiSimpleFileSystemProtocolGuid,
                  (VOID**)&Volume
                );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return EFI_SUCCESS;
}
