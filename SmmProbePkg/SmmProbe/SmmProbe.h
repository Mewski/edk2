/** @file
  SmmProbe shared definitions.

  Copyright (c) 2026, SmmProbePkg contributors.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef SMM_PROBE_H_
#define SMM_PROBE_H_

#include <Uefi.h>

//
// Control channel UART at non-standard base (0x100).
// OVMF and guest OS will not discover this port.
//
#define PROBE_UART_BASE  0x100
#define PROBE_UART_THR   (PROBE_UART_BASE + 0)
#define PROBE_UART_IER   (PROBE_UART_BASE + 1)
#define PROBE_UART_FCR   (PROBE_UART_BASE + 2)
#define PROBE_UART_LCR   (PROBE_UART_BASE + 3)
#define PROBE_UART_MCR   (PROBE_UART_BASE + 4)
#define PROBE_UART_LSR   (PROBE_UART_BASE + 5)
#define PROBE_UART_DLL   (PROBE_UART_BASE + 0)
#define PROBE_UART_DLH   (PROBE_UART_BASE + 1)

#define LSR_THRE  BIT5

//
// Command mailbox at fixed physical address.
//
#define MAILBOX_PHYS_ADDR  0xF000

#define MAILBOX_SIG        0x50524F42  // 'PROB'
#define MAILBOX_CMD_NOP    0
#define MAILBOX_CMD_PING   1
#define MAILBOX_CMD_READ   2
#define MAILBOX_CMD_WATCH  3  // Start continuous monitoring

#define MAX_PROBE_SIZE     4096

//
// ICH9 ACPI PM registers.
// PMBASE is at PCI 0:1F.0 register 0x40, OVMF sets it to 0x600.
//
#define ICH9_PMBASE_VALUE       0x600
#define ICH9_SMI_EN_OFS         0x30
#define ICH9_SMI_STS_OFS        0x34

#define ICH9_SMI_EN_GBL_SMI     BIT0
#define ICH9_SMI_EN_APMC        BIT5
#define ICH9_SMI_EN_SWSMI_TMR   BIT6
#define ICH9_SMI_EN_PERIODIC    BIT14

#define ICH9_SMI_STS_SWSMI_TMR  BIT6
#define ICH9_SMI_STS_PERIODIC   BIT14

//
// GEN_PMCON_1: PCI register 0xA0 on LPC bridge (0:1F.0).
// Bits 1:0 = SWSMI_RATE_SEL: 00=1.5ms, 01=16ms, 10=32ms, 11=64ms.
//
#define ICH9_LPC_PCI_ADDR(Reg)  PCI_LIB_ADDRESS (0, 0x1F, 0, (Reg))
#define ICH9_GEN_PMCON_1        0xA0
#define SWSMI_RATE_1_5MS        0x00
#define SWSMI_RATE_16MS         0x01
#define SWSMI_RATE_32MS         0x02
#define SWSMI_RATE_64MS         0x03
#define SWSMI_RATE_MASK         0x03

//
// Ring buffer in physical memory for high-speed capture.
// Avoids UART overhead in the hot path.
//
#define RING_PHYS_ADDR  0x10000
#define RING_SIZE       (64 * 1024)  // 64KB ring

#pragma pack(1)

typedef struct {
  UINT32    Signature;
  UINT32    Command;
  UINT64    PhysicalAddress;
  UINT32    Length;
  UINT32    Status;
} PROBE_MAILBOX;

typedef struct {
  UINT32    WriteOffset;
  UINT32    ReadOffset;
  UINT32    EntryCount;
  UINT32    Overflow;
} RING_HEADER;

typedef struct {
  UINT64    Timestamp;
  UINT32    Length;
  UINT32    Reserved;
  // Followed by Length bytes of captured data
} RING_ENTRY;

#pragma pack()

#endif
