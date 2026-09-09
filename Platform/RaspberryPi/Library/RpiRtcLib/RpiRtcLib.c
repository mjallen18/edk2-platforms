/** @file
 *
 *  Copyright (c) 2023, Mario Bălănică <mariobalanica02@gmail.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <PiDxe.h>

#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/RealTimeClockLib.h>
#include <Library/TimeBaseLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeLib.h>

#include <Protocol/RpiFirmware.h>

STATIC RASPBERRY_PI_FIRMWARE_PROTOCOL *mFwProtocol;

STATIC CONST CHAR16 mTimeZoneVariableName[] = L"RtcTimeZone";
STATIC CONST CHAR16 mDaylightVariableName[] = L"RtcDaylight";

//
// The VideoCore RTC counts plain seconds and has no notion of a time zone, so
// the local offset lives in non-volatile storage instead. It is cached here
// because GetTime() may not read its own output buffer to recover it.
//
STATIC INT16  mTimeZone = EFI_UNSPECIFIED_TIMEZONE;
STATIC UINT8  mDaylight = 0;

STATIC
VOID
EFIAPI
OffsetTimeZoneEpoch (
  IN      INT16       TimeZone,
  IN      UINT8       Daylight,
  IN OUT  UINT32      *EpochSeconds,
  IN      BOOLEAN     Add
  )
{
  //
  // Adjust for the correct time zone
  // The timezone setting also reflects the DST setting of the clock
  //
  if (TimeZone != EFI_UNSPECIFIED_TIMEZONE) {
    *EpochSeconds += (Add ? 1 : -1) * TimeZone * SEC_PER_MIN;
  } else if ((Daylight & EFI_TIME_IN_DAYLIGHT) == EFI_TIME_IN_DAYLIGHT) {
    // Convert to adjusted time, i.e. spring forwards one hour
    *EpochSeconds += (Add ? 1 : -1) * SEC_PER_HOUR;
  }
}

/**
  Load the stored time zone and daylight settings into the local cache.

  A missing or corrupt variable simply leaves the default (UTC) in place.

**/
STATIC
VOID
EFIAPI
LoadTimeSettings (
  VOID
  )
{
  EFI_STATUS  Status;
  UINTN       Size;
  INT16       TimeZone;
  UINT8       Daylight;

  Size = sizeof (TimeZone);
  Status = EfiGetVariable ((CHAR16 *)mTimeZoneVariableName,
             &gEfiCallerIdGuid, NULL, &Size, (VOID *)&TimeZone);
  if (!EFI_ERROR (Status) && (Size == sizeof (TimeZone))
      && IsValidTimeZone (TimeZone)) {
    mTimeZone = TimeZone;
  }

  Size = sizeof (Daylight);
  Status = EfiGetVariable ((CHAR16 *)mDaylightVariableName,
             &gEfiCallerIdGuid, NULL, &Size, (VOID *)&Daylight);
  if (!EFI_ERROR (Status) && (Size == sizeof (Daylight))
      && IsValidDaylight (Daylight)) {
    mDaylight = Daylight;
  }
}

/**
  Persist the time zone and daylight settings and update the local cache.

  @param  TimeZone              The time zone to store.
  @param  Daylight              The daylight setting to store.

  @retval EFI_SUCCESS           The settings were stored.
  @retval Others                The settings could not be stored.

**/
STATIC
EFI_STATUS
EFIAPI
SaveTimeSettings (
  IN  INT16   TimeZone,
  IN  UINT8   Daylight
  )
{
  EFI_STATUS  Status;

  //
  // Update the cache first: the clock has already been written using this
  // offset, so reads must agree with it even if the variable store fails.
  //
  mTimeZone = TimeZone;
  mDaylight = Daylight;

  Status = EfiSetVariable ((CHAR16 *)mTimeZoneVariableName,
             &gEfiCallerIdGuid,
             EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS |
             EFI_VARIABLE_RUNTIME_ACCESS,
             sizeof (TimeZone), (VOID *)&TimeZone);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to store %s. Status=%r\n",
            __func__, mTimeZoneVariableName, Status));
    return Status;
  }

  Status = EfiSetVariable ((CHAR16 *)mDaylightVariableName,
             &gEfiCallerIdGuid,
             EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS |
             EFI_VARIABLE_RUNTIME_ACCESS,
             sizeof (Daylight), (VOID *)&Daylight);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: Failed to store %s. Status=%r\n",
            __func__, mDaylightVariableName, Status));
    return Status;
  }

  return EFI_SUCCESS;
}

/**
  Returns the current time and date information, and the time-keeping capabilities
  of the hardware platform.

  @param  Time                   A pointer to storage to receive a snapshot of the current time.
  @param  Capabilities           An optional pointer to a buffer to receive the real time clock
                                 device's capabilities.

  @retval EFI_SUCCESS            The operation completed successfully.
  @retval EFI_INVALID_PARAMETER  Time is NULL.
  @retval EFI_DEVICE_ERROR       The time could not be retrieved due to hardware error.
  @retval EFI_SECURITY_VIOLATION The time could not be retrieved due to an authentication failure.

**/
EFI_STATUS
EFIAPI
LibGetTime (
  OUT EFI_TIME               *Time,
  OUT EFI_TIME_CAPABILITIES  *Capabilities
  )
{
  EFI_STATUS  Status;
  UINT32      EpochSeconds;

  if (Time == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Status = mFwProtocol->GetRtc (RpiRtcTime, &EpochSeconds);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  OffsetTimeZoneEpoch (mTimeZone, mDaylight, &EpochSeconds, TRUE);

  EpochToEfiTime (EpochSeconds, Time);

  //
  // EpochToEfiTime leaves these untouched.
  //
  Time->TimeZone = mTimeZone;
  Time->Daylight = mDaylight;

  if (Capabilities != NULL) {
    Capabilities->Resolution = 1;     // 1 Hz
    Capabilities->Accuracy   = 0;     // Unknown
    Capabilities->SetsToZero = FALSE;
  }

  return EFI_SUCCESS;
}

/**
  Sets the current local time and date information.

  @param  Time                  A pointer to the current time.

  @retval EFI_SUCCESS           The operation completed successfully.
  @retval EFI_INVALID_PARAMETER A time field is out of range.
  @retval EFI_DEVICE_ERROR      The time could not be set due to hardware error.

**/
EFI_STATUS
EFIAPI
LibSetTime (
  IN  EFI_TIME  *Time
  )
{
  EFI_STATUS  Status;
  UINT32      EpochSeconds;

  if (Time == NULL || !IsTimeValid (Time)) {
    return EFI_INVALID_PARAMETER;
  }

  EpochSeconds = (UINT32)EfiTimeToEpoch (Time);

  OffsetTimeZoneEpoch (Time->TimeZone, Time->Daylight, &EpochSeconds, FALSE);

  Status = mFwProtocol->SetRtc (RpiRtcTime, EpochSeconds);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Only commit the offset once the clock itself has been updated, so that a
  // failed write cannot leave the two disagreeing.
  //
  return SaveTimeSettings (Time->TimeZone, Time->Daylight);
}

/**
  Returns the current wakeup alarm clock setting.

  @param  Enabled               Indicates if the alarm is currently enabled or disabled.
  @param  Pending               Indicates if the alarm signal is pending and requires acknowledgement.
  @param  Time                  The current alarm setting.

  @retval EFI_SUCCESS           The alarm settings were returned.
  @retval EFI_INVALID_PARAMETER Any parameter is NULL.
  @retval EFI_DEVICE_ERROR      The wakeup time could not be retrieved due to a hardware error.

**/
EFI_STATUS
EFIAPI
LibGetWakeupTime (
  OUT BOOLEAN   *Enabled,
  OUT BOOLEAN   *Pending,
  OUT EFI_TIME  *Time
  )
{
  EFI_STATUS  Status;
  UINT32      EpochSeconds;
  UINT32      EnableVal;
  UINT32      PendingVal;

  if (Time == NULL || Enabled == NULL || Pending == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Status = mFwProtocol->GetRtc (RpiRtcAlarmEnable, &EnableVal);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = mFwProtocol->GetRtc (RpiRtcAlarmPending, &PendingVal);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  *Enabled = EnableVal;
  *Pending = PendingVal;

  if (*Pending) {
    // Acknowledge alarm
    Status = mFwProtocol->SetRtc (RpiRtcAlarmPending, TRUE);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  Status = mFwProtocol->GetRtc (RpiRtcAlarm, &EpochSeconds);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  OffsetTimeZoneEpoch (mTimeZone, mDaylight, &EpochSeconds, TRUE);

  EpochToEfiTime (EpochSeconds, Time);

  Time->TimeZone = mTimeZone;
  Time->Daylight = mDaylight;

  return EFI_SUCCESS;
}

/**
  Sets the system wakeup alarm clock time.

  @param  Enabled               Enable or disable the wakeup alarm.
  @param  Time                  If Enable is TRUE, the time to set the wakeup alarm for.

  @retval EFI_SUCCESS           If Enable is TRUE, then the wakeup alarm was enabled. If
                                Enable is FALSE, then the wakeup alarm was disabled.
  @retval EFI_INVALID_PARAMETER A time field is out of range.
  @retval EFI_DEVICE_ERROR      The wakeup time could not be set due to a hardware error.
  @retval EFI_UNSUPPORTED       A wakeup timer is not supported on this platform.

**/
EFI_STATUS
EFIAPI
LibSetWakeupTime (
  IN BOOLEAN    Enabled,
  OUT EFI_TIME  *Time
  )
{
  EFI_STATUS  Status;
  UINT32      EpochSeconds;

  if (Enabled) {
    if (Time == NULL || !IsTimeValid (Time)) {
      return EFI_INVALID_PARAMETER;
    }

    EpochSeconds = (UINT32)EfiTimeToEpoch (Time);

    OffsetTimeZoneEpoch (Time->TimeZone, Time->Daylight, &EpochSeconds, FALSE);

    Status = mFwProtocol->SetRtc (RpiRtcAlarm, EpochSeconds);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  Status = mFwProtocol->SetRtc (RpiRtcAlarmEnable, Enabled);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return EFI_SUCCESS;
}

/**
  Fixup internal data so that EFI can be call in virtual mode.
  Call the passed in Child Notify event and convert any pointers in
  lib to virtual mode.

  @param[in]    Event   The Event that is being processed
  @param[in]    Context Event Context

**/
STATIC
VOID
EFIAPI
VirtualAddressChangeNotify (
  IN EFI_EVENT        Event,
  IN VOID             *Context
  )
{
  EfiConvertPointer (0x0, (VOID **)&mFwProtocol);
}

/**
  This is the declaration of an EFI image entry point. This can be the entry point to an application
  written to this specification, an EFI boot service driver, or an EFI runtime driver.

  @param  ImageHandle           Handle that identifies the loaded image.
  @param  SystemTable           System Table for this image.

  @retval EFI_SUCCESS           The operation completed successfully.

**/
EFI_STATUS
EFIAPI
LibRtcInitialize (
  IN EFI_HANDLE                 ImageHandle,
  IN EFI_SYSTEM_TABLE           *SystemTable
  )
{
  EFI_STATUS  Status;
  EFI_TIME    Time;
  EFI_EVENT   VirtualAddressChangeEvent;

  Status = gBS->LocateProtocol (
                  &gRaspberryPiFirmwareProtocolGuid,
                  NULL,
                  (VOID **)&mFwProtocol);
  ASSERT_EFI_ERROR (Status);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_NOTIFY,
                  VirtualAddressChangeNotify,
                  NULL,
                  &gEfiEventVirtualAddressChangeGuid,
                  &VirtualAddressChangeEvent);
  ASSERT_EFI_ERROR (Status);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  LoadTimeSettings ();

  //
  // Initial RTC time starts off at Epoch = 0, which is out
  // of UEFI's bounds. Update it to the firmware build time.
  //
  ZeroMem (&Time, sizeof (Time));
  Status = LibGetTime (&Time, NULL);
  if (EFI_ERROR(Status)) {
    return Status;
  }

  if (!IsTimeValid (&Time)) {
    EpochToEfiTime (BUILD_EPOCH, &Time);
    Time.TimeZone = mTimeZone;
    Time.Daylight = mDaylight;
    Status = LibSetTime (&Time);
    if (EFI_ERROR(Status)) {
      return Status;
    }
  }

  return EFI_SUCCESS;
}
