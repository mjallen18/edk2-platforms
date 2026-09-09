/** @file
 *
 *  Register definitions for the RP1 clock generator.
 *
 *  Offsets are relative to RP1_CLOCKS_MAIN_BASE and follow the "RP1
 *  Peripherals" datasheet published by Raspberry Pi Ltd. The PLL blocks sit
 *  inside the same window: PLL_SYS at +0x8000 resolves to RP1_PLL_SYS_BASE.
 *
 *  Copyright (c) 2026, mjallen18 <matt.l.jallen@gmail.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef __RP1_CLOCKS_H__
#define __RP1_CLOCKS_H__

//
// Leaf clock generators. Each has a control register carrying the gate and
// the source mux, plus an integer divider. Only some have a fractional one.
//
#define RP1_CLK_SYS_CTRL              0x0014
#define RP1_CLK_SYS_DIV_INT           0x0018
#define RP1_CLK_SYS_SEL               0x0020

#define RP1_CLK_SLOW_SYS_CTRL         0x0024
#define RP1_CLK_SLOW_SYS_DIV_INT      0x0028
#define RP1_CLK_SLOW_SYS_SEL          0x0030

#define RP1_CLK_ETH_CTRL              0x0064
#define RP1_CLK_ETH_DIV_INT           0x0068
#define RP1_CLK_ETH_SEL               0x0070

#define RP1_CLK_ETH_TSU_CTRL          0x0134
#define RP1_CLK_ETH_TSU_DIV_INT       0x0138
#define RP1_CLK_ETH_TSU_SEL           0x0140

//
// A clock either takes its source from the small "standard" mux at the bottom
// of the control register, or from the wider auxiliary mux - never both.
//
#define RP1_CLK_CTRL_SRC_SHIFT        0
#define RP1_CLK_CTRL_AUXSRC_SHIFT     5
#define RP1_CLK_CTRL_AUXSRC_MASK      (0x1fu << 5)
#define RP1_CLK_CTRL_ENABLE           BIT11

//
// Dividers are 16.16 fixed point. Where a fractional register exists only its
// top 16 bits take part, and an integer part of zero means 2^16.
//
#define RP1_CLK_DIV_FRAC_BITS         16
#define RP1_CLK_DIV_INT_ZERO          (1u << 16)

//
// PLL_SYS. The core registers describe the VCO; the primary and secondary
// registers each divide it down to an output.
//
#define RP1_PLL_SYS_CS                0x8000
#define RP1_PLL_SYS_PWR               0x8004
#define RP1_PLL_SYS_FBDIV_INT         0x8008
#define RP1_PLL_SYS_FBDIV_FRAC        0x800c
#define RP1_PLL_SYS_PRIM              0x8010
#define RP1_PLL_SYS_SEC               0x8014

#define RP1_PLL_CS_LOCK               BIT31

#define RP1_PLL_PWR_PD                BIT0
#define RP1_PLL_PWR_DACPD             BIT1
#define RP1_PLL_PWR_DSMPD             BIT2
#define RP1_PLL_PWR_POSTDIVPD         BIT3
#define RP1_PLL_PWR_4PHASEPD          BIT4
#define RP1_PLL_PWR_VCOPD             BIT5

//
// The feedback divider is an integer part plus a 24-bit fraction.
//
#define RP1_PLL_FBDIV_FRAC_BITS       24

//
// The primary output divides the VCO by the product of two 3-bit dividers.
//
#define RP1_PLL_PRIM_DIV1_SHIFT       16
#define RP1_PLL_PRIM_DIV1_MASK        (0x7u << 16)
#define RP1_PLL_PRIM_DIV2_SHIFT       12
#define RP1_PLL_PRIM_DIV2_MASK        (0x7u << 12)

//
// The secondary output divides the VCO by a single 5-bit divider.
//
#define RP1_PLL_SEC_DIV_SHIFT         8
#define RP1_PLL_SEC_DIV_MASK          (0x1fu << 8)
#define RP1_PLL_SEC_RST               BIT16

#endif // __RP1_CLOCKS_H__
