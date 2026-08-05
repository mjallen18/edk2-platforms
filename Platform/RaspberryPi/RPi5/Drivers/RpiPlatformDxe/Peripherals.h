/** @file
 *
 *  Copyright (c) 2023-2024, Mario Bălănică <mariobalanica02@gmail.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef __RPI_PLATFORM_PERIPHERALS_H__
#define __RPI_PLATFORM_PERIPHERALS_H__

#include <Bcm2712PcieControllerSettings.h>

#define PCIE1_SETTINGS_ENABLED_DEFAULT         TRUE
//
// Gen 3 is beyond what the Raspberry Pi 5 is specified for on the PCIe
// connector, but it is stable in practice on this hardware and is what was
// configured by hand here, so default to it rather than making it a manual
// step after every firmware update. Drop to 2 in the UEFI menu (Device
// Manager -> Raspberry Pi Configuration -> PCIe) if a device is unhappy.
//
#define PCIE1_SETTINGS_MAX_LINK_SPEED_DEFAULT  3

#ifndef VFRCOMPILE
  #include <Protocol/Bcm2712PciePlatform.h>

EFI_STATUS
EFIAPI
SetupPeripherals (
  VOID
  );

VOID
EFIAPI
ApplyPeripheralVariables (
  VOID
  );

VOID
EFIAPI
SetupPeripheralVariables (
  VOID
  );

extern BCM2712_PCIE_PLATFORM_PROTOCOL  mPciePlatform;
#endif // VFRCOMPILE

#endif // __RPI_PLATFORM_PERIPHERALS_H__
