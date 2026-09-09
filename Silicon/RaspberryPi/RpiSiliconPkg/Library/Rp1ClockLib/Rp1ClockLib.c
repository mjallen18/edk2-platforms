/** @file
 *
 *  Clock control for the RP1 south bridge.
 *
 *  Copyright (c) 2026, mjallen18 <matt.l.jallen@gmail.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#include <Uefi.h>
#include <IndustryStandard/Rp1Clocks.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/Rp1ClockLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/Rp1Bus.h>
#include <Rp1.h>

typedef enum {
  Rp1ClockKindFixed,
  Rp1ClockKindPllCore,
  Rp1ClockKindPllPrimary,
  Rp1ClockKindPllSecondary,
  Rp1ClockKindLeaf
} RP1_CLOCK_KIND;

typedef struct {
  CONST CHAR8           *Name;
  RP1_CLOCK_KIND        Kind;
  UINT64                FixedRate;
  //
  // PllCore: CS. PllPrimary: PRIM. PllSecondary: SEC. Leaf: CTRL.
  //
  UINT32                CtrlReg;
  UINT32                FbDivIntReg;
  UINT32                FbDivFracReg;
  UINT32                DivIntReg;
  UINT32                DivFracReg;
  //
  // Fixed source, for everything whose parent is not selected by a mux.
  //
  RP1_CLOCK_ID          Source;
  //
  // Leaf mux inputs. Rp1ClockIdMax marks an input this library does not
  // model, so that an unexpected setting is reported rather than mistaken
  // for a known parent.
  //
  CONST RP1_CLOCK_ID    *Parents;
  UINT8                 ParentCount;
  //
  // Non-zero selects the standard mux and gives its width; zero selects the
  // auxiliary mux.
  //
  UINT32                SrcMask;
} RP1_CLOCK_DESC;

STATIC CONST RP1_CLOCK_ID  mClkSysParents[] = {
  Rp1ClockXosc,
  Rp1ClockIdMax,
  Rp1ClockPllSys
};

STATIC CONST RP1_CLOCK_ID  mClkSlowSysParents[] = {
  Rp1ClockXosc
};

//
// pll_video_sec and clksrc_gp0..gp5 make up the remaining inputs of both
// ethernet muxes and are outside what this library models.
//
STATIC CONST RP1_CLOCK_ID  mClkEthParents[] = {
  Rp1ClockPllSysSec,
  Rp1ClockPllSys,
  Rp1ClockIdMax,
  Rp1ClockIdMax,
  Rp1ClockIdMax,
  Rp1ClockIdMax,
  Rp1ClockIdMax,
  Rp1ClockIdMax,
  Rp1ClockIdMax
};

STATIC CONST RP1_CLOCK_ID  mClkEthTsuParents[] = {
  Rp1ClockXosc,
  Rp1ClockIdMax,
  Rp1ClockIdMax,
  Rp1ClockIdMax,
  Rp1ClockIdMax,
  Rp1ClockIdMax,
  Rp1ClockIdMax,
  Rp1ClockIdMax
};

STATIC CONST RP1_CLOCK_DESC  mRp1Clocks[Rp1ClockIdMax] = {
  [Rp1ClockXosc] = {
    .Name      = "xosc",
    .Kind      = Rp1ClockKindFixed,
    .FixedRate = RP1_XOSC_FREQUENCY
  },
  [Rp1ClockPllSysCore] = {
    .Name         = "pll_sys_core",
    .Kind         = Rp1ClockKindPllCore,
    .CtrlReg      = RP1_PLL_SYS_CS,
    .FbDivIntReg  = RP1_PLL_SYS_FBDIV_INT,
    .FbDivFracReg = RP1_PLL_SYS_FBDIV_FRAC,
    .Source       = Rp1ClockXosc
  },
  [Rp1ClockPllSys] = {
    .Name    = "pll_sys",
    .Kind    = Rp1ClockKindPllPrimary,
    .CtrlReg = RP1_PLL_SYS_PRIM,
    .Source  = Rp1ClockPllSysCore
  },
  [Rp1ClockPllSysSec] = {
    .Name    = "pll_sys_sec",
    .Kind    = Rp1ClockKindPllSecondary,
    .CtrlReg = RP1_PLL_SYS_SEC,
    .Source  = Rp1ClockPllSysCore
  },
  [Rp1ClockSys] = {
    .Name        = "clk_sys",
    .Kind        = Rp1ClockKindLeaf,
    .CtrlReg     = RP1_CLK_SYS_CTRL,
    .DivIntReg   = RP1_CLK_SYS_DIV_INT,
    .Parents     = mClkSysParents,
    .ParentCount = ARRAY_SIZE (mClkSysParents),
    .SrcMask     = 0x3
  },
  [Rp1ClockSlowSys] = {
    .Name        = "clk_slow_sys",
    .Kind        = Rp1ClockKindLeaf,
    .CtrlReg     = RP1_CLK_SLOW_SYS_CTRL,
    .DivIntReg   = RP1_CLK_SLOW_SYS_DIV_INT,
    .Parents     = mClkSlowSysParents,
    .ParentCount = ARRAY_SIZE (mClkSlowSysParents),
    .SrcMask     = 0x1
  },
  [Rp1ClockEth] = {
    .Name        = "clk_eth",
    .Kind        = Rp1ClockKindLeaf,
    .CtrlReg     = RP1_CLK_ETH_CTRL,
    .DivIntReg   = RP1_CLK_ETH_DIV_INT,
    .Parents     = mClkEthParents,
    .ParentCount = ARRAY_SIZE (mClkEthParents)
  },
  [Rp1ClockEthTsu] = {
    .Name        = "clk_eth_tsu",
    .Kind        = Rp1ClockKindLeaf,
    .CtrlReg     = RP1_CLK_ETH_TSU_CTRL,
    .DivIntReg   = RP1_CLK_ETH_TSU_DIV_INT,
    .Parents     = mClkEthTsuParents,
    .ParentCount = ARRAY_SIZE (mClkEthTsuParents)
  }
};

STATIC EFI_PHYSICAL_ADDRESS  mRp1PeripheralBase = 0;

//
// Deep enough for xosc -> pll core -> pll output -> leaf, with headroom.
//
#define RP1_CLOCK_MAX_DEPTH  8

#define RP1_CLOCKS_REG(Base, Offset)  \
  ((Base) + RP1_CLOCKS_MAIN_BASE + (Offset))

/**
  Resolve the RP1 peripheral window, locating the bus driver on first use.

  @param  Base                  Receives the peripheral base address.

  @retval EFI_SUCCESS           The window was resolved.
  @retval EFI_NOT_READY         The RP1 bus driver has not started yet.
  @retval EFI_DEVICE_ERROR      The bus driver reported no peripheral window.

**/
STATIC
EFI_STATUS
Rp1ClockGetBase (
  OUT EFI_PHYSICAL_ADDRESS  *Base
  )
{
  EFI_STATUS        Status;
  RP1_BUS_PROTOCOL  *Rp1Bus;

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

  *Base = mRp1PeripheralBase;
  return EFI_SUCCESS;
}

/**
  Return the mux input a leaf clock is currently taking.

  @param  Base                  The RP1 peripheral base address.
  @param  Desc                  The clock's descriptor.
  @param  ParentIndex           Receives the raw mux setting. Written even when
                                the selected input is not modelled, so that a
                                caller can report which one it was. May be NULL.
  @param  Parent                Receives the selected parent.

  @retval EFI_SUCCESS           The parent was resolved.
  @retval EFI_DEVICE_ERROR      The mux holds a value with no input behind it.
  @retval EFI_UNSUPPORTED       The selected input is not modelled.

**/
STATIC
EFI_STATUS
Rp1ClockGetLeafParent (
  IN  EFI_PHYSICAL_ADDRESS   Base,
  IN  CONST RP1_CLOCK_DESC   *Desc,
  OUT UINT8                  *ParentIndex  OPTIONAL,
  OUT RP1_CLOCK_ID           *Parent
  )
{
  UINT32  Ctrl;
  UINT32  Index;

  Ctrl = MmioRead32 (RP1_CLOCKS_REG (Base, Desc->CtrlReg));

  if (Desc->SrcMask != 0) {
    Index = (Ctrl >> RP1_CLK_CTRL_SRC_SHIFT) & Desc->SrcMask;
  } else {
    Index = (Ctrl & RP1_CLK_CTRL_AUXSRC_MASK) >> RP1_CLK_CTRL_AUXSRC_SHIFT;
  }

  if (ParentIndex != NULL) {
    *ParentIndex = (UINT8)Index;
  }

  if (Index >= Desc->ParentCount) {
    DEBUG ((
      DEBUG_ERROR,
      "%a: %a selects input %u, beyond its %u inputs\n",
      __func__,
      Desc->Name,
      Index,
      Desc->ParentCount
      ));
    return EFI_DEVICE_ERROR;
  }

  if (Desc->Parents[Index] >= Rp1ClockIdMax) {
    DEBUG ((
      DEBUG_WARN,
      "%a: %a is sourced from unmodelled input %u\n",
      __func__,
      Desc->Name,
      Index
      ));
    return EFI_UNSUPPORTED;
  }

  *Parent = Desc->Parents[Index];
  return EFI_SUCCESS;
}

/**
  Walk the clock tree from ClockId towards the crystal, computing its rate.

  @param  Base                  The RP1 peripheral base address.
  @param  ClockId               The clock to evaluate.
  @param  Depth                 Recursion guard.
  @param  Rate                  Receives the rate in Hz.

  @retval EFI_SUCCESS           The rate was computed.
  @retval Others                The tree could not be walked.

**/
STATIC
EFI_STATUS
Rp1ClockComputeRate (
  IN  EFI_PHYSICAL_ADDRESS  Base,
  IN  RP1_CLOCK_ID          ClockId,
  IN  UINTN                 Depth,
  OUT UINT64                *Rate
  )
{
  EFI_STATUS              Status;
  CONST RP1_CLOCK_DESC    *Desc;
  RP1_CLOCK_ID            Parent;
  UINT64                  ParentRate;
  UINT64                  Divider;
  UINT64                  Multiplier;
  UINT32                  DivInt;
  UINT32                  DivFrac;
  UINT32                  Prim;
  UINT32                  Div1;
  UINT32                  Div2;
  UINT32                  SecDiv;

  if (Depth >= RP1_CLOCK_MAX_DEPTH) {
    ASSERT (FALSE);
    return EFI_DEVICE_ERROR;
  }

  Desc = &mRp1Clocks[ClockId];

  if (Desc->Kind == Rp1ClockKindFixed) {
    *Rate = Desc->FixedRate;
    return EFI_SUCCESS;
  }

  if (Desc->Kind == Rp1ClockKindLeaf) {
    Status = Rp1ClockGetLeafParent (Base, Desc, NULL, &Parent);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  } else {
    Parent = Desc->Source;
  }

  Status = Rp1ClockComputeRate (Base, Parent, Depth + 1, &ParentRate);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  switch (Desc->Kind) {
    case Rp1ClockKindPllCore:
      //
      // The VCO runs at the crystal times an integer plus 24-bit fraction.
      //
      Multiplier = LShiftU64 (
                     (UINT64)MmioRead32 (RP1_CLOCKS_REG (Base, Desc->FbDivIntReg)),
                     RP1_PLL_FBDIV_FRAC_BITS
                     ) + MmioRead32 (RP1_CLOCKS_REG (Base, Desc->FbDivFracReg));

      *Rate = RShiftU64 (
                MultU64x64 (ParentRate, Multiplier) +
                LShiftU64 (1, RP1_PLL_FBDIV_FRAC_BITS - 1),
                RP1_PLL_FBDIV_FRAC_BITS
                );
      return EFI_SUCCESS;

    case Rp1ClockKindPllPrimary:
      Prim = MmioRead32 (RP1_CLOCKS_REG (Base, Desc->CtrlReg));
      Div1 = (Prim & RP1_PLL_PRIM_DIV1_MASK) >> RP1_PLL_PRIM_DIV1_SHIFT;
      Div2 = (Prim & RP1_PLL_PRIM_DIV2_MASK) >> RP1_PLL_PRIM_DIV2_SHIFT;

      if ((Div1 == 0) || (Div2 == 0)) {
        DEBUG ((DEBUG_ERROR, "%a: %a has a zero divider\n", __func__, Desc->Name));
        return EFI_DEVICE_ERROR;
      }

      Divider = (UINT64)Div1 * Div2;
      *Rate   = DivU64x64Remainder (ParentRate + Divider / 2, Divider, NULL);
      return EFI_SUCCESS;

    case Rp1ClockKindPllSecondary:
      SecDiv = (MmioRead32 (RP1_CLOCKS_REG (Base, Desc->CtrlReg)) &
                RP1_PLL_SEC_DIV_MASK) >> RP1_PLL_SEC_DIV_SHIFT;

      if (SecDiv == 0) {
        DEBUG ((DEBUG_ERROR, "%a: %a has a zero divider\n", __func__, Desc->Name));
        return EFI_DEVICE_ERROR;
      }

      *Rate = DivU64x64Remainder (ParentRate, SecDiv, NULL);
      return EFI_SUCCESS;

    case Rp1ClockKindLeaf:
      DivInt  = MmioRead32 (RP1_CLOCKS_REG (Base, Desc->DivIntReg));
      DivFrac = (Desc->DivFracReg != 0)
                ? MmioRead32 (RP1_CLOCKS_REG (Base, Desc->DivFracReg))
                : 0;

      if (DivInt == 0) {
        DivInt = RP1_CLK_DIV_INT_ZERO;
      }

      //
      // Assemble the 16.16 divider, taking only the top half of the
      // fractional register.
      //
      Divider = LShiftU64 ((UINT64)DivInt, RP1_CLK_DIV_FRAC_BITS) |
                (DivFrac >> (32 - RP1_CLK_DIV_FRAC_BITS));

      *Rate = DivU64x64Remainder (
                LShiftU64 (ParentRate, RP1_CLK_DIV_FRAC_BITS),
                Divider,
                NULL
                );
      return EFI_SUCCESS;

    default:
      ASSERT (FALSE);
      return EFI_DEVICE_ERROR;
  }
}

EFI_STATUS
EFIAPI
Rp1ClockGetRate (
  IN  RP1_CLOCK_ID   ClockId,
  OUT UINT64         *Rate
  )
{
  EFI_STATUS            Status;
  EFI_PHYSICAL_ADDRESS  Base;

  if ((ClockId >= Rp1ClockIdMax) || (Rate == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  Status = Rp1ClockGetBase (&Base);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return Rp1ClockComputeRate (Base, ClockId, 0, Rate);
}

EFI_STATUS
EFIAPI
Rp1ClockGetName (
  IN  RP1_CLOCK_ID   ClockId,
  OUT CONST CHAR8    **Name
  )
{
  if ((ClockId >= Rp1ClockIdMax) || (Name == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  *Name = mRp1Clocks[ClockId].Name;
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
Rp1ClockGetSource (
  IN  RP1_CLOCK_ID   ClockId,
  OUT UINT8          *ParentIndex  OPTIONAL,
  OUT RP1_CLOCK_ID   *Parent
  )
{
  EFI_STATUS            Status;
  EFI_PHYSICAL_ADDRESS  Base;
  CONST RP1_CLOCK_DESC  *Desc;

  if ((ClockId >= Rp1ClockIdMax) || (Parent == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  Desc = &mRp1Clocks[ClockId];

  if (Desc->Kind == Rp1ClockKindFixed) {
    return EFI_UNSUPPORTED;
  }

  if (ParentIndex != NULL) {
    *ParentIndex = 0;
  }

  if (Desc->Kind != Rp1ClockKindLeaf) {
    *Parent = Desc->Source;
    return EFI_SUCCESS;
  }

  Status = Rp1ClockGetBase (&Base);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = Rp1ClockGetLeafParent (Base, Desc, ParentIndex, Parent);
  if (Status == EFI_UNSUPPORTED) {
    //
    // Reporting which input was selected is the whole point of this call, so
    // an unmodelled one is answered rather than refused.
    //
    *Parent = Rp1ClockIdMax;
    return EFI_SUCCESS;
  }

  return Status;
}

EFI_STATUS
EFIAPI
Rp1ClockIsEnabled (
  IN  RP1_CLOCK_ID   ClockId,
  OUT BOOLEAN        *Enabled
  )
{
  EFI_STATUS            Status;
  EFI_PHYSICAL_ADDRESS  Base;

  if ((ClockId >= Rp1ClockIdMax) || (Enabled == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if (mRp1Clocks[ClockId].Kind != Rp1ClockKindLeaf) {
    return EFI_UNSUPPORTED;
  }

  Status = Rp1ClockGetBase (&Base);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  *Enabled = (MmioRead32 (RP1_CLOCKS_REG (Base, mRp1Clocks[ClockId].CtrlReg)) &
              RP1_CLK_CTRL_ENABLE) != 0;

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
Rp1ClockEnable (
  IN  RP1_CLOCK_ID   ClockId
  )
{
  EFI_STATUS            Status;
  EFI_PHYSICAL_ADDRESS  Base;

  if (ClockId >= Rp1ClockIdMax) {
    return EFI_INVALID_PARAMETER;
  }

  if (mRp1Clocks[ClockId].Kind != Rp1ClockKindLeaf) {
    return EFI_UNSUPPORTED;
  }

  Status = Rp1ClockGetBase (&Base);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  MmioOr32 (
    RP1_CLOCKS_REG (Base, mRp1Clocks[ClockId].CtrlReg),
    RP1_CLK_CTRL_ENABLE
    );

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
Rp1ClockDisable (
  IN  RP1_CLOCK_ID   ClockId
  )
{
  EFI_STATUS            Status;
  EFI_PHYSICAL_ADDRESS  Base;

  if (ClockId >= Rp1ClockIdMax) {
    return EFI_INVALID_PARAMETER;
  }

  if (mRp1Clocks[ClockId].Kind != Rp1ClockKindLeaf) {
    return EFI_UNSUPPORTED;
  }

  Status = Rp1ClockGetBase (&Base);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  MmioAnd32 (
    RP1_CLOCKS_REG (Base, mRp1Clocks[ClockId].CtrlReg),
    ~(UINT32)RP1_CLK_CTRL_ENABLE
    );

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
Rp1ClockConfigure (
  IN  RP1_CLOCK_ID   ClockId,
  IN  UINT8          ParentIndex,
  IN  UINT32         Divider
  )
{
  EFI_STATUS              Status;
  EFI_PHYSICAL_ADDRESS    Base;
  CONST RP1_CLOCK_DESC    *Desc;
  UINT32                  Ctrl;

  if ((ClockId >= Rp1ClockIdMax) || (Divider == 0)) {
    return EFI_INVALID_PARAMETER;
  }

  Desc = &mRp1Clocks[ClockId];

  if (Desc->Kind != Rp1ClockKindLeaf) {
    return EFI_UNSUPPORTED;
  }

  if (ParentIndex >= Desc->ParentCount) {
    return EFI_INVALID_PARAMETER;
  }

  Status = Rp1ClockGetBase (&Base);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Program the divider before switching the mux, so that the clock never
  // runs at the new source scaled by the old divider.
  //
  MmioWrite32 (RP1_CLOCKS_REG (Base, Desc->DivIntReg), Divider);

  Ctrl = MmioRead32 (RP1_CLOCKS_REG (Base, Desc->CtrlReg));

  if (Desc->SrcMask != 0) {
    Ctrl &= ~(Desc->SrcMask << RP1_CLK_CTRL_SRC_SHIFT);
    Ctrl |= (UINT32)ParentIndex << RP1_CLK_CTRL_SRC_SHIFT;
  } else {
    Ctrl &= ~RP1_CLK_CTRL_AUXSRC_MASK;
    Ctrl |= (UINT32)ParentIndex << RP1_CLK_CTRL_AUXSRC_SHIFT;
  }

  MmioWrite32 (RP1_CLOCKS_REG (Base, Desc->CtrlReg), Ctrl);

  return EFI_SUCCESS;
}
