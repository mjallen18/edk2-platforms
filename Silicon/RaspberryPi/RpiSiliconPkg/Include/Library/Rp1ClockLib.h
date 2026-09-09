/** @file
 *
 *  Clock control for the RP1 south bridge.
 *
 *  RP1 brings its own clock tree up before the host sees it, so the primary
 *  job here is to report what the hardware is actually running at and to gate
 *  individual consumers on and off. Rates are derived by walking the tree from
 *  the crystal, not from a table of expected values.
 *
 *  Only the PLL_SYS branch and the clocks hanging off it are modelled; the
 *  audio and video PLLs and the general purpose clock sources are not.
 *  Requesting a rate that depends on one of those returns EFI_UNSUPPORTED
 *  rather than a guess.
 *
 *  Copyright (c) 2026, mjallen18 <matt.l.jallen@gmail.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef __RP1_CLOCK_LIB_H__
#define __RP1_CLOCK_LIB_H__

//
// RP1's crystal. Every modelled rate is derived from it.
//
#define RP1_XOSC_FREQUENCY  50000000ULL

typedef enum {
  Rp1ClockXosc = 0,
  Rp1ClockPllSysCore,
  Rp1ClockPllSys,
  Rp1ClockPllSysSec,
  Rp1ClockSys,
  Rp1ClockSlowSys,
  Rp1ClockEth,
  Rp1ClockEthTsu,
  Rp1ClockIdMax
} RP1_CLOCK_ID;

/**
  Report the rate a clock is currently running at.

  The rate is computed from the hardware's own divider and mux settings, so it
  reflects what RP1 was left in rather than what it is expected to be.

  @param  ClockId               The clock to query.
  @param  Rate                  Receives the rate in Hz.

  @retval EFI_SUCCESS           The rate was computed.
  @retval EFI_INVALID_PARAMETER ClockId is out of range, or Rate is NULL.
  @retval EFI_NOT_READY         The RP1 bus driver has not started yet.
  @retval EFI_UNSUPPORTED       The clock is sourced from a parent this
                                library does not model.
  @retval EFI_DEVICE_ERROR      The hardware holds a divider or mux setting
                                that cannot be decoded.

**/
EFI_STATUS
EFIAPI
Rp1ClockGetRate (
  IN  RP1_CLOCK_ID   ClockId,
  OUT UINT64         *Rate
  );

/**
  Report whether a clock's gate is open.

  @param  ClockId               The clock to query.
  @param  Enabled               Receives the gate state.

  @retval EFI_SUCCESS           The state was returned.
  @retval EFI_INVALID_PARAMETER ClockId is out of range, or Enabled is NULL.
  @retval EFI_NOT_READY         The RP1 bus driver has not started yet.
  @retval EFI_UNSUPPORTED       The clock has no gate of its own.

**/
EFI_STATUS
EFIAPI
Rp1ClockIsEnabled (
  IN  RP1_CLOCK_ID   ClockId,
  OUT BOOLEAN        *Enabled
  );

/**
  Open a clock's gate.

  @param  ClockId               The clock to enable.

  @retval EFI_SUCCESS           The gate was opened.
  @retval EFI_INVALID_PARAMETER ClockId is out of range.
  @retval EFI_NOT_READY         The RP1 bus driver has not started yet.
  @retval EFI_UNSUPPORTED       The clock has no gate of its own.

**/
EFI_STATUS
EFIAPI
Rp1ClockEnable (
  IN  RP1_CLOCK_ID   ClockId
  );

/**
  Close a clock's gate.

  @param  ClockId               The clock to disable.

  @retval EFI_SUCCESS           The gate was closed.
  @retval EFI_INVALID_PARAMETER ClockId is out of range.
  @retval EFI_NOT_READY         The RP1 bus driver has not started yet.
  @retval EFI_UNSUPPORTED       The clock has no gate of its own.

**/
EFI_STATUS
EFIAPI
Rp1ClockDisable (
  IN  RP1_CLOCK_ID   ClockId
  );

/**
  Select a clock's source and integer divider.

  This is deliberately low level: the caller states which mux input and which
  divider to use rather than a target frequency, because the useful settings
  are a small fixed set per board. The gate is left as it was found, so a
  caller retiming a clock that is already feeding a live consumer should
  disable it around the call.

  @param  ClockId               The clock to configure.
  @param  ParentIndex           The mux input to select.
  @param  Divider               The integer divider to program. Zero is
                                rejected rather than passed through as the
                                hardware's 2^16 encoding.

  @retval EFI_SUCCESS           The clock was configured.
  @retval EFI_INVALID_PARAMETER ClockId, ParentIndex or Divider is out of
                                range.
  @retval EFI_NOT_READY         The RP1 bus driver has not started yet.
  @retval EFI_UNSUPPORTED       The clock has no source mux of its own.

**/
EFI_STATUS
EFIAPI
Rp1ClockConfigure (
  IN  RP1_CLOCK_ID   ClockId,
  IN  UINT8          ParentIndex,
  IN  UINT32         Divider
  );

#endif // __RP1_CLOCK_LIB_H__
