/** @file
 *
 *  Copyright (c) 2023-2024, Mario Bălănică <mariobalanica02@gmail.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>
#include <Guid/RpiPlatformFormSetGuid.h>
#include <IndustryStandard/RpiMbox.h>
#include <Library/BoardInfoLib.h>
#include <Library/BoardRevisionHelperLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/HiiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/RpiFirmware.h>

#include "ConfigTable.h"
#include "Peripherals.h"

UINT32 mBoardRevisionCode;
UINT64 mSystemMemorySize;

extern UINT8 RpiPlatformDxeHiiBin[];
extern UINT8 RpiPlatformDxeStrings[];

typedef struct {
  VENDOR_DEVICE_PATH VendorDevicePath;
  EFI_DEVICE_PATH_PROTOCOL End;
} HII_VENDOR_DEVICE_PATH;

STATIC HII_VENDOR_DEVICE_PATH mVendorDevicePath = {
  {
    {
      HARDWARE_DEVICE_PATH,
      HW_VENDOR_DP,
      {
        (UINT8)(sizeof (VENDOR_DEVICE_PATH)),
        (UINT8)((sizeof (VENDOR_DEVICE_PATH)) >> 8)
      }
    },
    RPI_PLATFORM_FORMSET_GUID
  },
  {
    END_DEVICE_PATH_TYPE,
    END_ENTIRE_DEVICE_PATH_SUBTYPE,
    {
      (UINT8)(END_DEVICE_PATH_LENGTH),
      (UINT8)((END_DEVICE_PATH_LENGTH) >> 8)
    }
  }
};

STATIC
EFI_STATUS
EFIAPI
InstallHiiPages (
  VOID
  )
{
  EFI_STATUS        Status;
  EFI_HII_HANDLE    HiiHandle;
  EFI_HANDLE        DriverHandle;

  DriverHandle = NULL;
  Status = gBS->InstallMultipleProtocolInterfaces (&DriverHandle,
                  &gEfiDevicePathProtocolGuid,
                  &mVendorDevicePath,
                  NULL);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  HiiHandle = HiiAddPackages (&gRpiPlatformFormSetGuid,
                DriverHandle,
                RpiPlatformDxeStrings,
                RpiPlatformDxeHiiBin,
                NULL);

  if (HiiHandle == NULL) {
    gBS->UninstallMultipleProtocolInterfaces (DriverHandle,
           &gEfiDevicePathProtocolGuid,
           &mVendorDevicePath,
           NULL);
    return EFI_OUT_OF_RESOURCES;
  }
  return EFI_SUCCESS;
}

STATIC
VOID
EFIAPI
SetupVariables (
  VOID
  )
{
  SetupConfigTableVariables ();
  SetupPeripheralVariables ();
}

STATIC
VOID
EFIAPI
ApplyVariables (
  VOID
  )
{
  ApplyConfigTableVariables ();
  ApplyPeripheralVariables ();
}

STATIC
VOID
SetCpuClockToMax (
  VOID
  )
{
  EFI_STATUS                      Status;
  RASPBERRY_PI_FIRMWARE_PROTOCOL  *Firmware;
  UINT32                          Rate;

  Status = gBS->LocateProtocol (
                  &gRaspberryPiFirmwareProtocolGuid,
                  NULL,
                  (VOID **)&Firmware
                  );
  if (EFI_ERROR (Status)) {
    return;
  }

  //
  // The VideoCore hands over with the cores at their minimum clock (1.5 GHz)
  // and expects the OS to scale them. Linux does that over this mailbox on a
  // devicetree boot, but nothing does under ACPI, so run at the configured
  // maximum (arm_freq in config.txt).
  //
  Status = Firmware->GetMaxClockRate (RPI_MBOX_CLOCK_RATE_ARM, &Rate);
  if (!EFI_ERROR (Status) && (Rate != 0)) {
    Status = Firmware->SetClockRate (RPI_MBOX_CLOCK_RATE_ARM, Rate, TRUE);
  }

  DEBUG ((DEBUG_INFO, "%a: Set ARM clock to maximum: %r\n", __func__, Status));
}

EFI_STATUS
EFIAPI
RpiPlatformDxeEntryPoint (
  IN  EFI_HANDLE          ImageHandle,
  IN  EFI_SYSTEM_TABLE    *SystemTable
  )
{
  EFI_STATUS Status;

  Status = BoardInfoGetRevisionCode (&mBoardRevisionCode);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to get board revision. Status=%r\n",
            __func__, Status));
    ASSERT (FALSE);
  }

  mSystemMemorySize = BoardRevisionGetMemorySize (mBoardRevisionCode);

  SetupVariables ();
  ApplyVariables ();
  SetCpuClockToMax ();

  SetupPeripherals ();

  Status = InstallHiiPages ();
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Couldn't install HII pages. Status=%r\n", __func__, Status));
  }

  return EFI_SUCCESS;
}
