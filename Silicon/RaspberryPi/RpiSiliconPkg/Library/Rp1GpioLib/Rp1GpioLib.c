/** @file
 *
 *  GPIO access for the RP1 south bridge.
 *
 *  Copyright (c) 2026, mjallen18 <matt.l.jallen@gmail.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>
#include <IndustryStandard/Rp1Gpio.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/Rp1GpioLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/Rp1Bus.h>
#include <Rp1.h>

typedef struct {
  UINT8     FirstPin;
  UINT8     PinCount;
  UINT32    BankOffset;
} RP1_GPIO_BANK;

//
// The three banks are not equally sized, but they are contiguous and cover
// GPIO0..GPIO53.
//
STATIC CONST RP1_GPIO_BANK  mRp1GpioBanks[] = {
  {  0, 28, 0 * RP1_GPIO_BANK_STRIDE },
  { 28,  6, 1 * RP1_GPIO_BANK_STRIDE },
  { 34, 20, 2 * RP1_GPIO_BANK_STRIDE }
};

STATIC EFI_PHYSICAL_ADDRESS  mRp1PeripheralBase = 0;

#define RP1_GPIO_CTRL_ADDRESS(Base, Bank, BankPin)                  \
  ((Base) + RP1_IO_BANK0_BASE + (Bank)->BankOffset +                \
   ((BankPin) * RP1_GPIO_PIN_STRIDE) + RP1_GPIO_CTRL)

#define RP1_RIO_ADDRESS(Base, Bank, Register, Alias)                \
  ((Base) + RP1_SYS_RIO0_BASE + (Bank)->BankOffset +                \
   (Alias) + (Register))

#define RP1_PADS_ADDRESS(Base, Bank, BankPin)                       \
  ((Base) + RP1_PADS_BANK0_BASE + (Bank)->BankOffset +              \
   RP1_PADS_FIRST_PIN + ((BankPin) * RP1_PADS_PIN_STRIDE))

/**
  Resolve a GPIO number to its peripheral base, bank and in-bank index.

  @param  Pin                   The GPIO number.
  @param  Base                  Receives the RP1 peripheral base address.
  @param  Bank                  Receives the owning bank.
  @param  BankPin               Receives the pin's index within the bank.

  @retval EFI_SUCCESS           The pin was resolved.
  @retval EFI_INVALID_PARAMETER Pin is out of range.
  @retval EFI_NOT_READY         The RP1 bus driver has not started yet.
  @retval EFI_DEVICE_ERROR      The bus driver reported no peripheral window.

**/
STATIC
EFI_STATUS
Rp1GpioResolvePin (
  IN  UINT8                  Pin,
  OUT EFI_PHYSICAL_ADDRESS   *Base,
  OUT CONST RP1_GPIO_BANK    **Bank,
  OUT UINT8                  *BankPin
  )
{
  EFI_STATUS        Status;
  RP1_BUS_PROTOCOL  *Rp1Bus;
  UINTN             Index;

  if (mRp1PeripheralBase == 0) {
    //
    // The register window is a PCIe BAR, so it does not exist until the RP1
    // bus driver has claimed the endpoint.
    //
    Status = gBS->LocateProtocol (
                    &gRp1BusProtocolGuid,
                    NULL,
                    (VOID **)&Rp1Bus
                    );
    if (EFI_ERROR (Status)) {
      return EFI_NOT_READY;
    }

    mRp1PeripheralBase = Rp1Bus->GetPeripheralBase (Rp1Bus);
    if (mRp1PeripheralBase == 0) {
      DEBUG ((DEBUG_ERROR, "%a: RP1 reported a null peripheral base\n", __func__));
      return EFI_DEVICE_ERROR;
    }
  }

  for (Index = 0; Index < ARRAY_SIZE (mRp1GpioBanks); Index++) {
    if ((Pin >= mRp1GpioBanks[Index].FirstPin) &&
        ((Pin - mRp1GpioBanks[Index].FirstPin) < mRp1GpioBanks[Index].PinCount))
    {
      *Base    = mRp1PeripheralBase;
      *Bank    = &mRp1GpioBanks[Index];
      *BankPin = Pin - mRp1GpioBanks[Index].FirstPin;
      return EFI_SUCCESS;
    }
  }

  return EFI_INVALID_PARAMETER;
}

EFI_STATUS
EFIAPI
Rp1GpioSetFunction (
  IN  UINT8   Pin,
  IN  UINT8   Function
  )
{
  EFI_STATUS             Status;
  EFI_PHYSICAL_ADDRESS   Base;
  CONST RP1_GPIO_BANK    *Bank;
  UINT8                  BankPin;
  UINT32                 Ctrl;
  UINT32                 Pads;

  if ((Function >= RP1_GPIO_FUNCSEL_COUNT) &&
      (Function != RP1_GPIO_FUNCTION_NONE))
  {
    return EFI_INVALID_PARAMETER;
  }

  Status = Rp1GpioResolvePin (Pin, &Base, &Bank, &BankPin);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Let the pad drive and sample before muxing, otherwise the selected
  // peripheral ends up connected to a disabled pad.
  //
  Pads  = MmioRead32 (RP1_PADS_ADDRESS (Base, Bank, BankPin));
  Pads &= ~(UINT32)RP1_PADS_OUT_DISABLE;
  Pads |= RP1_PADS_IN_ENABLE;
  MmioWrite32 (RP1_PADS_ADDRESS (Base, Bank, BankPin), Pads);

  Ctrl  = MmioRead32 (RP1_GPIO_CTRL_ADDRESS (Base, Bank, BankPin));
  Ctrl &= ~(RP1_GPIO_CTRL_FUNCSEL_MASK |
            RP1_GPIO_CTRL_OUTOVER_MASK |
            RP1_GPIO_CTRL_OEOVER_MASK);

  if (Function == RP1_GPIO_FUNCTION_NONE) {
    Ctrl |= RP1_GPIO_FUNCSEL_NONE << RP1_GPIO_CTRL_FUNCSEL_SHIFT;
    Ctrl |= RP1_GPIO_OEOVER_DISABLE << RP1_GPIO_CTRL_OEOVER_SHIFT;
  } else {
    Ctrl |= (UINT32)Function << RP1_GPIO_CTRL_FUNCSEL_SHIFT;
    Ctrl |= RP1_GPIO_OUTOVER_PERIPHERAL << RP1_GPIO_CTRL_OUTOVER_SHIFT;
    Ctrl |= RP1_GPIO_OEOVER_PERIPHERAL << RP1_GPIO_CTRL_OEOVER_SHIFT;
  }

  MmioWrite32 (RP1_GPIO_CTRL_ADDRESS (Base, Bank, BankPin), Ctrl);

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
Rp1GpioGetFunction (
  IN  UINT8   Pin,
  OUT UINT8   *Function
  )
{
  EFI_STATUS             Status;
  EFI_PHYSICAL_ADDRESS   Base;
  CONST RP1_GPIO_BANK    *Bank;
  UINT8                  BankPin;
  UINT32                 Ctrl;
  UINT32                 FuncSel;

  if (Function == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Status = Rp1GpioResolvePin (Pin, &Base, &Bank, &BankPin);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Ctrl    = MmioRead32 (RP1_GPIO_CTRL_ADDRESS (Base, Bank, BankPin));
  FuncSel = (Ctrl & RP1_GPIO_CTRL_FUNCSEL_MASK) >> RP1_GPIO_CTRL_FUNCSEL_SHIFT;

  //
  // A pin whose output enable has been forced is not being driven by the
  // function its FUNCSEL names, so report it as disconnected.
  //
  if ((FuncSel >= RP1_GPIO_FUNCSEL_COUNT) ||
      (((Ctrl & RP1_GPIO_CTRL_OEOVER_MASK) >> RP1_GPIO_CTRL_OEOVER_SHIFT) !=
       RP1_GPIO_OEOVER_PERIPHERAL))
  {
    *Function = RP1_GPIO_FUNCTION_NONE;
  } else {
    *Function = (UINT8)FuncSel;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
Rp1GpioSetPull (
  IN  UINT8           Pin,
  IN  RP1_GPIO_PULL   Pull
  )
{
  EFI_STATUS             Status;
  EFI_PHYSICAL_ADDRESS   Base;
  CONST RP1_GPIO_BANK    *Bank;
  UINT8                  BankPin;
  UINT32                 Pads;
  UINT32                 Encoded;

  switch (Pull) {
    case Rp1GpioPullNone:
      Encoded = RP1_PADS_PULL_NONE;
      break;
    case Rp1GpioPullDown:
      Encoded = RP1_PADS_PULL_DOWN;
      break;
    case Rp1GpioPullUp:
      Encoded = RP1_PADS_PULL_UP;
      break;
    default:
      return EFI_INVALID_PARAMETER;
  }

  Status = Rp1GpioResolvePin (Pin, &Base, &Bank, &BankPin);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Pads  = MmioRead32 (RP1_PADS_ADDRESS (Base, Bank, BankPin));
  Pads &= ~RP1_PADS_PULL_MASK;
  Pads |= Encoded << RP1_PADS_PULL_SHIFT;
  MmioWrite32 (RP1_PADS_ADDRESS (Base, Bank, BankPin), Pads);

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
Rp1GpioGetPull (
  IN  UINT8           Pin,
  OUT RP1_GPIO_PULL   *Pull
  )
{
  EFI_STATUS             Status;
  EFI_PHYSICAL_ADDRESS   Base;
  CONST RP1_GPIO_BANK    *Bank;
  UINT8                  BankPin;
  UINT32                 Pads;

  if (Pull == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Status = Rp1GpioResolvePin (Pin, &Base, &Bank, &BankPin);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Pads = MmioRead32 (RP1_PADS_ADDRESS (Base, Bank, BankPin));

  switch ((Pads & RP1_PADS_PULL_MASK) >> RP1_PADS_PULL_SHIFT) {
    case RP1_PADS_PULL_NONE:
      *Pull = Rp1GpioPullNone;
      break;
    case RP1_PADS_PULL_DOWN:
      *Pull = Rp1GpioPullDown;
      break;
    case RP1_PADS_PULL_UP:
      *Pull = Rp1GpioPullUp;
      break;
    default:
      return EFI_DEVICE_ERROR;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
Rp1GpioSetDirection (
  IN  UINT8                Pin,
  IN  RP1_GPIO_DIRECTION   Direction
  )
{
  EFI_STATUS             Status;
  EFI_PHYSICAL_ADDRESS   Base;
  CONST RP1_GPIO_BANK    *Bank;
  UINT8                  BankPin;
  UINT32                 Alias;

  switch (Direction) {
    case Rp1GpioDirectionOutput:
      Alias = RP1_ATOMIC_SET;
      break;
    case Rp1GpioDirectionInput:
      Alias = RP1_ATOMIC_CLR;
      break;
    default:
      return EFI_INVALID_PARAMETER;
  }

  Status = Rp1GpioResolvePin (Pin, &Base, &Bank, &BankPin);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // The set and clear aliases make this a single write, so no other bit in
  // the bank can be disturbed by a concurrent update.
  //
  MmioWrite32 (
    RP1_RIO_ADDRESS (Base, Bank, RP1_RIO_OE, Alias),
    1u << BankPin
    );

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
Rp1GpioGetDirection (
  IN  UINT8                Pin,
  OUT RP1_GPIO_DIRECTION   *Direction
  )
{
  EFI_STATUS             Status;
  EFI_PHYSICAL_ADDRESS   Base;
  CONST RP1_GPIO_BANK    *Bank;
  UINT8                  BankPin;
  UINT32                 Oe;

  if (Direction == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Status = Rp1GpioResolvePin (Pin, &Base, &Bank, &BankPin);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Oe = MmioRead32 (RP1_RIO_ADDRESS (Base, Bank, RP1_RIO_OE, RP1_ATOMIC_RW));

  *Direction = ((Oe & (1u << BankPin)) != 0) ? Rp1GpioDirectionOutput
                                             : Rp1GpioDirectionInput;

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
Rp1GpioWrite (
  IN  UINT8     Pin,
  IN  BOOLEAN   Value
  )
{
  EFI_STATUS             Status;
  EFI_PHYSICAL_ADDRESS   Base;
  CONST RP1_GPIO_BANK    *Bank;
  UINT8                  BankPin;

  Status = Rp1GpioResolvePin (Pin, &Base, &Bank, &BankPin);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  MmioWrite32 (
    RP1_RIO_ADDRESS (
      Base,
      Bank,
      RP1_RIO_OUT,
      Value ? RP1_ATOMIC_SET : RP1_ATOMIC_CLR
      ),
    1u << BankPin
    );

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
Rp1GpioRead (
  IN  UINT8     Pin,
  OUT BOOLEAN   *Value
  )
{
  EFI_STATUS             Status;
  EFI_PHYSICAL_ADDRESS   Base;
  CONST RP1_GPIO_BANK    *Bank;
  UINT8                  BankPin;
  UINT32                 In;

  if (Value == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Status = Rp1GpioResolvePin (Pin, &Base, &Bank, &BankPin);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  In = MmioRead32 (RP1_RIO_ADDRESS (Base, Bank, RP1_RIO_IN, RP1_ATOMIC_RW));

  *Value = (In & (1u << BankPin)) != 0;

  return EFI_SUCCESS;
}
