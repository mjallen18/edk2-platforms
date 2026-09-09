/** @file
 *
 *  GPIO access for the RP1 south bridge.
 *
 *  RP1's registers live behind a PCIe BAR, so unlike the SoC's own GPIO
 *  library every call here can legitimately fail before Rp1BusDxe has started.
 *  The accessors therefore return status rather than a sentinel value.
 *
 *  Copyright (c) 2026, mjallen18 <matt.l.jallen@gmail.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef __RP1_GPIO_LIB_H__
#define __RP1_GPIO_LIB_H__

//
// GPIO0..GPIO27 are the 40-pin header bank; the rest are wired to on-board
// functions.
//
#define RP1_GPIO_PIN_COUNT         54

//
// Values accepted by Rp1GpioSetFunction (). 0..8 select a peripheral,
// RP1_GPIO_FUNCTION_SYS_RIO selects software control through Rp1GpioRead ()
// and Rp1GpioWrite (), and RP1_GPIO_FUNCTION_NONE disconnects the pin.
//
#define RP1_GPIO_FUNCTION_SYS_RIO  5
#define RP1_GPIO_FUNCTION_NONE     9

typedef enum {
  Rp1GpioPullNone = 0,
  Rp1GpioPullDown,
  Rp1GpioPullUp
} RP1_GPIO_PULL;

typedef enum {
  Rp1GpioDirectionInput = 0,
  Rp1GpioDirectionOutput
} RP1_GPIO_DIRECTION;

/**
  Select the function driving a pin.

  Enables the pad in both directions before muxing, so that the selected
  peripheral is not connected to a disabled pad.

  @param  Pin                   The GPIO number.
  @param  Function              0..8, RP1_GPIO_FUNCTION_SYS_RIO or
                                RP1_GPIO_FUNCTION_NONE.

  @retval EFI_SUCCESS           The function was selected.
  @retval EFI_INVALID_PARAMETER Pin or Function is out of range.
  @retval EFI_NOT_READY         The RP1 bus driver has not started yet.

**/
EFI_STATUS
EFIAPI
Rp1GpioSetFunction (
  IN  UINT8   Pin,
  IN  UINT8   Function
  );

/**
  Return the function currently driving a pin.

  @param  Pin                   The GPIO number.
  @param  Function              Receives the function, or
                                RP1_GPIO_FUNCTION_NONE if the pin is
                                disconnected.

  @retval EFI_SUCCESS           The function was returned.
  @retval EFI_INVALID_PARAMETER Pin is out of range, or Function is NULL.
  @retval EFI_NOT_READY         The RP1 bus driver has not started yet.

**/
EFI_STATUS
EFIAPI
Rp1GpioGetFunction (
  IN  UINT8   Pin,
  OUT UINT8   *Function
  );

/**
  Set the bias applied to a pin's pad.

  @param  Pin                   The GPIO number.
  @param  Pull                  The bias to apply.

  @retval EFI_SUCCESS           The bias was applied.
  @retval EFI_INVALID_PARAMETER Pin or Pull is out of range.
  @retval EFI_NOT_READY         The RP1 bus driver has not started yet.

**/
EFI_STATUS
EFIAPI
Rp1GpioSetPull (
  IN  UINT8           Pin,
  IN  RP1_GPIO_PULL   Pull
  );

/**
  Return the bias applied to a pin's pad.

  @param  Pin                   The GPIO number.
  @param  Pull                  Receives the bias.

  @retval EFI_SUCCESS           The bias was returned.
  @retval EFI_INVALID_PARAMETER Pin is out of range, or Pull is NULL.
  @retval EFI_NOT_READY         The RP1 bus driver has not started yet.
  @retval EFI_DEVICE_ERROR      The pad reports a reserved bias encoding.

**/
EFI_STATUS
EFIAPI
Rp1GpioGetPull (
  IN  UINT8           Pin,
  OUT RP1_GPIO_PULL   *Pull
  );

/**
  Set the direction of a pin under RIO control.

  Only meaningful once the pin has been given to the RIO block with
  Rp1GpioSetFunction (Pin, RP1_GPIO_FUNCTION_SYS_RIO).

  @param  Pin                   The GPIO number.
  @param  Direction             The direction to select.

  @retval EFI_SUCCESS           The direction was selected.
  @retval EFI_INVALID_PARAMETER Pin or Direction is out of range.
  @retval EFI_NOT_READY         The RP1 bus driver has not started yet.

**/
EFI_STATUS
EFIAPI
Rp1GpioSetDirection (
  IN  UINT8                Pin,
  IN  RP1_GPIO_DIRECTION   Direction
  );

/**
  Return the direction of a pin under RIO control.

  @param  Pin                   The GPIO number.
  @param  Direction             Receives the direction.

  @retval EFI_SUCCESS           The direction was returned.
  @retval EFI_INVALID_PARAMETER Pin is out of range, or Direction is NULL.
  @retval EFI_NOT_READY         The RP1 bus driver has not started yet.

**/
EFI_STATUS
EFIAPI
Rp1GpioGetDirection (
  IN  UINT8                Pin,
  OUT RP1_GPIO_DIRECTION   *Direction
  );

/**
  Drive a pin under RIO control.

  @param  Pin                   The GPIO number.
  @param  Value                 TRUE to drive high, FALSE to drive low.

  @retval EFI_SUCCESS           The pin was driven.
  @retval EFI_INVALID_PARAMETER Pin is out of range.
  @retval EFI_NOT_READY         The RP1 bus driver has not started yet.

**/
EFI_STATUS
EFIAPI
Rp1GpioWrite (
  IN  UINT8     Pin,
  IN  BOOLEAN   Value
  );

/**
  Sample a pin.

  @param  Pin                   The GPIO number.
  @param  Value                 Receives the sampled level.

  @retval EFI_SUCCESS           The pin was sampled.
  @retval EFI_INVALID_PARAMETER Pin is out of range, or Value is NULL.
  @retval EFI_NOT_READY         The RP1 bus driver has not started yet.

**/
EFI_STATUS
EFIAPI
Rp1GpioRead (
  IN  UINT8     Pin,
  OUT BOOLEAN   *Value
  );

#endif // __RP1_GPIO_LIB_H__
