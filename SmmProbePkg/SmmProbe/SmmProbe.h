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

#define MAX_PROBE_SIZE     4096

#pragma pack(1)

typedef struct {
  UINT32    Signature;        // Must be MAILBOX_SIG
  UINT32    Command;          // MAILBOX_CMD_*
  UINT64    PhysicalAddress;  // Target for CMD_READ
  UINT32    Length;            // Bytes to read (max MAX_PROBE_SIZE)
  UINT32    Status;           // Set by handler: 0 = success
} PROBE_MAILBOX;

#pragma pack()

#endif
