/** @file
 *
 *  Register definitions for the RP1 GPIO, RIO and pad control blocks.
 *
 *  Offsets and field positions follow the "RP1 Peripherals" datasheet
 *  published by Raspberry Pi Ltd.
 *
 *  Copyright (c) 2026, mjallen18 <matt.l.jallen@gmail.com>
 *
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 **/

#ifndef __RP1_GPIO_H__
#define __RP1_GPIO_H__

//
// The GPIO, RIO and pad blocks are each replicated per bank at 16 KB
// intervals, and every bank exposes four 4 KB aliases of the same registers:
// plain read/write, atomic XOR, atomic bit set and atomic bit clear.
//
#define RP1_GPIO_BANK_STRIDE                 0x4000

#define RP1_ATOMIC_RW                        0x0000
#define RP1_ATOMIC_XOR                       0x1000
#define RP1_ATOMIC_SET                       0x2000
#define RP1_ATOMIC_CLR                       0x3000

//
// IO bank - two registers per pin.
//
#define RP1_GPIO_PIN_STRIDE                  0x8
#define RP1_GPIO_STATUS                      0x0
#define RP1_GPIO_CTRL                        0x4

#define RP1_GPIO_CTRL_FUNCSEL_SHIFT          0
#define RP1_GPIO_CTRL_FUNCSEL_MASK           (0x1fu << 0)
#define RP1_GPIO_CTRL_OUTOVER_SHIFT          12
#define RP1_GPIO_CTRL_OUTOVER_MASK           (0x3u << 12)
#define RP1_GPIO_CTRL_OEOVER_SHIFT           14
#define RP1_GPIO_CTRL_OEOVER_MASK            (0x3u << 14)
#define RP1_GPIO_CTRL_INOVER_SHIFT           16
#define RP1_GPIO_CTRL_INOVER_MASK            (0x3u << 16)

//
// Override selectors. The output and input paths can be forced to a level,
// whereas the output-enable path is forced on or off instead - the encodings
// only agree for values 0 and 1, so they are spelled out separately.
//
#define RP1_GPIO_OUTOVER_PERIPHERAL          0
#define RP1_GPIO_OUTOVER_INVERT_PERIPHERAL   1
#define RP1_GPIO_OUTOVER_LOW                 2
#define RP1_GPIO_OUTOVER_HIGH                3

#define RP1_GPIO_OEOVER_PERIPHERAL           0
#define RP1_GPIO_OEOVER_INVERT_PERIPHERAL    1
#define RP1_GPIO_OEOVER_DISABLE              2
#define RP1_GPIO_OEOVER_ENABLE               3

#define RP1_GPIO_INOVER_PERIPHERAL           0
#define RP1_GPIO_INOVER_INVERT_PERIPHERAL    1
#define RP1_GPIO_INOVER_LOW                  2
#define RP1_GPIO_INOVER_HIGH                 3

//
// FUNCSEL 0..8 select the per-pin alternate functions. Function 5 hands the
// pin to the RIO block for software control; any value the hardware does not
// decode leaves the pin unconnected.
//
#define RP1_GPIO_FUNCSEL_COUNT               9
#define RP1_GPIO_FUNCSEL_SYS_RIO             5
#define RP1_GPIO_FUNCSEL_NONE                0x1f

//
// RIO bank - software drive and sample, one bit per pin within the bank.
//
#define RP1_RIO_OUT                          0x0
#define RP1_RIO_OE                           0x4
#define RP1_RIO_IN                           0x8

//
// Pad bank - one register per pin, following a leading voltage select
// register.
//
#define RP1_PADS_FIRST_PIN                   0x4
#define RP1_PADS_PIN_STRIDE                  0x4

#define RP1_PADS_SLEWFAST                    BIT0
#define RP1_PADS_SCHMITT                     BIT1
#define RP1_PADS_PULL_SHIFT                  2
#define RP1_PADS_PULL_MASK                   (0x3u << 2)
#define RP1_PADS_DRIVE_SHIFT                 4
#define RP1_PADS_DRIVE_MASK                  (0x3u << 4)
#define RP1_PADS_IN_ENABLE                   BIT6
#define RP1_PADS_OUT_DISABLE                 BIT7

#define RP1_PADS_PULL_NONE                   0
#define RP1_PADS_PULL_DOWN                   1
#define RP1_PADS_PULL_UP                     2

#endif // __RP1_GPIO_H__
