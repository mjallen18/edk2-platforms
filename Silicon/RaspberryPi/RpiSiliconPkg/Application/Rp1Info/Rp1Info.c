/** @file
 *
 *  Reports the state RP1 was left in by earlier firmware, and drives a header
 *  pin so that GPIO can be checked with a meter.
 *
 *  Both RP1 libraries read registers behind a PCIe BAR that only exists once
 *  Rp1BusDxe has claimed the endpoint, so this doubles as an end to end check
 *  that the bus driver, the BAR mapping and the register offsets all agree.
 *
 *  Copyright (c) 2026, mjallen18 <matt.l.jallen@gmail.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/Rp1ClockLib.h>
#include <Library/Rp1GpioLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/ShellParameters.h>

//
// GPIO0..GPIO27 are the 40-pin header. Everything above is wired to on-board
// functions, so it is dumped only on request and never driven.
//
#define RP1_HEADER_PIN_COUNT  28

/**
  Print one clock's rate, gate state and selected source.

  Every field is reported independently, so a clock whose rate cannot be
  decoded still shows which input it is taking.

  @param  ClockId               The clock to report.

**/
STATIC
VOID
DumpClock (
  IN  RP1_CLOCK_ID  ClockId
  )
{
  EFI_STATUS    Status;
  CONST CHAR8   *Name;
  CONST CHAR8   *ParentName;
  UINT64        Rate;
  BOOLEAN       Enabled;
  RP1_CLOCK_ID  Parent;
  UINT8         ParentIndex;

  Status = Rp1ClockGetName (ClockId, &Name);
  if (EFI_ERROR (Status)) {
    return;
  }

  Print (L"  %-13a ", Name);

  Status = Rp1ClockGetRate (ClockId, &Rate);
  if (EFI_ERROR (Status)) {
    Print (L"%17r ", Status);
  } else {
    Print (L"%,17ld ", Rate);
  }

  Status = Rp1ClockIsEnabled (ClockId, &Enabled);
  if (Status == EFI_UNSUPPORTED) {
    //
    // PLLs and the crystal have no gate of their own.
    //
    Print (L"%-8s", L"-");
  } else if (EFI_ERROR (Status)) {
    Print (L"%-8r", Status);
  } else {
    Print (L"%-8s", Enabled ? L"on" : L"off");
  }

  Status = Rp1ClockGetSource (ClockId, &ParentIndex, &Parent);
  if (Status == EFI_UNSUPPORTED) {
    Print (L"-");
  } else if (EFI_ERROR (Status)) {
    Print (L"%r", Status);
  } else if (Parent >= Rp1ClockIdMax) {
    Print (L"input %u (not modelled)", ParentIndex);
  } else if (!EFI_ERROR (Rp1ClockGetName (Parent, &ParentName))) {
    Print (L"%a", ParentName);
  }

  Print (L"\n");
}

/**
  Print the modelled part of RP1's clock tree.

  @retval EFI_SUCCESS           The tree was printed.
  @retval Others                RP1 could not be reached at all.

**/
STATIC
EFI_STATUS
DumpClocks (
  VOID
  )
{
  EFI_STATUS    Status;
  RP1_CLOCK_ID  ClockId;
  UINT64        Rate;

  //
  // The crystal is a constant, so failing to read it means the peripheral
  // window itself is unreachable and every other clock would repeat the same
  // error.
  //
  Status = Rp1ClockGetRate (Rp1ClockXosc, &Rate);
  if (EFI_ERROR (Status)) {
    Print (L"RP1 clocks unavailable: %r\n", Status);
    if (Status == EFI_NOT_READY) {
      Print (L"  Rp1BusDxe has not claimed the endpoint.\n");
    }

    return Status;
  }

  Print (L"RP1 clocks\n");
  Print (L"  %-13s %17s %-8s%s\n", L"name", L"rate (Hz)", L"gate", L"source");

  for (ClockId = 0; ClockId < Rp1ClockIdMax; ClockId++) {
    DumpClock (ClockId);
  }

  Print (L"\n");
  return EFI_SUCCESS;
}

/**
  Print the mux, direction, level and bias of a range of pins.

  @param  PinCount              Number of pins to report, starting at GPIO0.

**/
STATIC
VOID
DumpGpios (
  IN  UINT8  PinCount
  )
{
  EFI_STATUS          Status;
  UINT8               Pin;
  UINT8               Function;
  RP1_GPIO_DIRECTION  Direction;
  RP1_GPIO_PULL       Pull;
  BOOLEAN             Level;

  Print (L"RP1 GPIO (0..%u)\n", PinCount - 1);
  Print (L"  %-5s %-12s %-6s %-6s %s\n", L"pin", L"function", L"dir", L"level", L"pull");

  for (Pin = 0; Pin < PinCount; Pin++) {
    Status = Rp1GpioGetFunction (Pin, &Function);
    if (EFI_ERROR (Status)) {
      Print (L"  %-5u %r\n", Pin, Status);
      continue;
    }

    Print (L"  %-5u ", Pin);

    if (Function == RP1_GPIO_FUNCTION_NONE) {
      Print (L"%-12s", L"none");
    } else if (Function == RP1_GPIO_FUNCTION_SYS_RIO) {
      Print (L"%-12s", L"sys_rio");
    } else {
      Print (L"alt%-9u", Function);
    }

    //
    // Direction only means anything under RIO control, but the input
    // synchroniser feeds RIO regardless of the mux, so the level is always a
    // true reading of the pad.
    //
    Status = Rp1GpioGetDirection (Pin, &Direction);
    if (EFI_ERROR (Status)) {
      Print (L"%-6r", Status);
    } else if (Function != RP1_GPIO_FUNCTION_SYS_RIO) {
      Print (L"%-6s", L"-");
    } else {
      Print (L"%-6s", Direction == Rp1GpioDirectionOutput ? L"out" : L"in");
    }

    Status = Rp1GpioRead (Pin, &Level);
    if (EFI_ERROR (Status)) {
      Print (L"%-6r", Status);
    } else {
      Print (L"%-6u", Level ? 1 : 0);
    }

    Status = Rp1GpioGetPull (Pin, &Pull);
    if (EFI_ERROR (Status)) {
      Print (L"%r", Status);
    } else if (Pull == Rp1GpioPullUp) {
      Print (L"up");
    } else if (Pull == Rp1GpioPullDown) {
      Print (L"down");
    } else {
      Print (L"none");
    }

    Print (L"\n");
  }
}

/**
  Take a header pin under software control and drive it.

  The value is staged in RIO and the pin turned into an output before the mux
  is switched, so the pad never briefly drives a stale level.

  @param  Pin                   The GPIO to drive, restricted to the header.
  @param  Value                 The level to drive.

  @retval EFI_SUCCESS           The pin is driving.
  @retval Others                The pin could not be driven.

**/
STATIC
EFI_STATUS
DriveGpio (
  IN  UINT8    Pin,
  IN  BOOLEAN  Value
  )
{
  EFI_STATUS  Status;
  BOOLEAN     Level;

  if (Pin >= RP1_HEADER_PIN_COUNT) {
    Print (
      L"Refusing to drive GPIO%u: only the header pins 0..%u are safe to take\n",
      Pin,
      RP1_HEADER_PIN_COUNT - 1
      );
    return EFI_INVALID_PARAMETER;
  }

  Status = Rp1GpioWrite (Pin, Value);
  if (EFI_ERROR (Status)) {
    Print (L"Rp1GpioWrite: %r\n", Status);
    return Status;
  }

  Status = Rp1GpioSetDirection (Pin, Rp1GpioDirectionOutput);
  if (EFI_ERROR (Status)) {
    Print (L"Rp1GpioSetDirection: %r\n", Status);
    return Status;
  }

  Status = Rp1GpioSetFunction (Pin, RP1_GPIO_FUNCTION_SYS_RIO);
  if (EFI_ERROR (Status)) {
    Print (L"Rp1GpioSetFunction: %r\n", Status);
    return Status;
  }

  Print (L"GPIO%u driving %u", Pin, Value ? 1 : 0);

  //
  // Reading back proves the pad followed, which is what distinguishes a
  // working mux from a register write that went nowhere.
  //
  Status = Rp1GpioRead (Pin, &Level);
  if (!EFI_ERROR (Status)) {
    Print (L", reads back %u", Level ? 1 : 0);
  }

  Print (L"\n");
  return EFI_SUCCESS;
}

/**
  Convert an argument that must be a plain decimal number.

  StrDecimalToUintn () reports a non-numeric string as zero, which for a
  command that drives a pin would silently act on GPIO0.

  @param  String                The argument to convert.
  @param  Value                 Receives the converted number.

  @retval TRUE                  String was one or more decimal digits.
  @retval FALSE                 String was empty or held anything else.

**/
STATIC
BOOLEAN
ParseNumber (
  IN  CONST CHAR16  *String,
  OUT UINTN         *Value
  )
{
  CONST CHAR16  *Walk;

  if ((String == NULL) || (*String == L'\0')) {
    return FALSE;
  }

  for (Walk = String; *Walk != L'\0'; Walk++) {
    if ((*Walk < L'0') || (*Walk > L'9')) {
      return FALSE;
    }
  }

  *Value = StrDecimalToUintn (String);
  return TRUE;
}

/**
  Print usage.

**/
STATIC
VOID
Usage (
  VOID
  )
{
  Print (L"Usage:\n");
  Print (L"  Rp1Info.efi              Dump clocks and the header GPIOs\n");
  Print (L"  Rp1Info.efi all          Dump clocks and all %u GPIOs\n", RP1_GPIO_PIN_COUNT);
  Print (L"  Rp1Info.efi set <pin> <0|1>\n");
  Print (L"                           Drive a header pin under software control\n");
}

/**
  Entry point.

  @param  ImageHandle           This image.
  @param  SystemTable           The system table.

  @retval EFI_SUCCESS           The requested action completed.
  @retval Others                RP1 was unreachable or the arguments were bad.

**/
EFI_STATUS
EFIAPI
UefiMain (
  IN  EFI_HANDLE        ImageHandle,
  IN  EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS                     Status;
  EFI_SHELL_PARAMETERS_PROTOCOL  *Parameters;
  UINTN                          Argc;
  CHAR16                         **Argv;
  UINTN                          Pin;
  UINTN                          Value;

  Argc = 0;
  Argv = NULL;

  //
  // Absent when the image was not launched from the shell, which just means
  // there are no arguments to read.
  //
  Status = gBS->HandleProtocol (
                  ImageHandle,
                  &gEfiShellParametersProtocolGuid,
                  (VOID **)&Parameters
                  );
  if (!EFI_ERROR (Status)) {
    Argc = Parameters->Argc;
    Argv = Parameters->Argv;
  }

  if ((Argc >= 2) && (StrCmp (Argv[1], L"set") == 0)) {
    if (Argc != 4) {
      Usage ();
      return EFI_INVALID_PARAMETER;
    }

    if (!ParseNumber (Argv[2], &Pin) || !ParseNumber (Argv[3], &Value)) {
      Usage ();
      return EFI_INVALID_PARAMETER;
    }

    if ((Pin >= RP1_GPIO_PIN_COUNT) || (Value > 1)) {
      Usage ();
      return EFI_INVALID_PARAMETER;
    }

    return DriveGpio ((UINT8)Pin, (BOOLEAN)(Value != 0));
  }

  if ((Argc >= 2) && (StrCmp (Argv[1], L"all") != 0)) {
    Usage ();
    return EFI_INVALID_PARAMETER;
  }

  Status = DumpClocks ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  DumpGpios (
    (Argc >= 2) ? RP1_GPIO_PIN_COUNT : RP1_HEADER_PIN_COUNT
    );

  return EFI_SUCCESS;
}
