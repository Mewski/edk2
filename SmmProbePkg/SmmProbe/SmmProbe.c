/** @file
  SMM physical memory probe driver.

  Registers a root SMI handler that reads commands from a fixed
  physical memory mailbox and writes results to a dedicated UART.

  Copyright (c) 2026, SmmProbePkg contributors.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <PiSmm.h>
#include <Library/SmmServicesTableLib.h>
#include <Library/SerialPortLib.h>
#include <Library/DebugLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h>
#include <Library/IoLib.h>
#include <Library/PrintLib.h>

#include "SmmProbe.h"

STATIC EFI_HANDLE  mSmiHandle = NULL;

STATIC
VOID
ProbeUartInit (
  VOID
  )
{
  IoWrite8 (PROBE_UART_IER, 0x00);
  IoWrite8 (PROBE_UART_LCR, 0x80);
  IoWrite8 (PROBE_UART_DLL, 0x01);
  IoWrite8 (PROBE_UART_DLH, 0x00);
  IoWrite8 (PROBE_UART_LCR, 0x03);
  IoWrite8 (PROBE_UART_FCR, 0xC7);
  IoWrite8 (PROBE_UART_MCR, 0x0B);
}

STATIC
VOID
ProbeUartPutChar (
  IN CHAR8  Ch
  )
{
  while ((IoRead8 (PROBE_UART_LSR) & LSR_THRE) == 0) {
  }

  IoWrite8 (PROBE_UART_THR, (UINT8)Ch);
}

STATIC
VOID
ProbeUartWrite (
  IN CONST CHAR8  *Str,
  IN UINTN        Length
  )
{
  UINTN  Index;

  for (Index = 0; Index < Length; Index++) {
    ProbeUartPutChar (Str[Index]);
  }
}

STATIC
VOID
ProbeUartPrint (
  IN CONST CHAR8  *Str
  )
{
  while (*Str != '\0') {
    ProbeUartPutChar (*Str);
    Str++;
  }
}

STATIC
VOID
HexDump (
  IN UINTN  Address,
  IN UINTN  Length
  )
{
  UINTN  Offset;
  UINT8  *Ptr;
  CHAR8  Line[80];
  UINTN  LineLen;
  UINTN  i;

  if (Length > MAX_PROBE_SIZE) {
    Length = MAX_PROBE_SIZE;
  }

  Ptr = (UINT8 *)(UINTN)Address;

  for (Offset = 0; Offset < Length; Offset += 16) {
    LineLen = AsciiSPrint (
                Line,
                sizeof (Line),
                "%016lx: ",
                (UINT64)(Address + Offset)
                );

    for (i = 0; (i < 16) && (Offset + i < Length); i++) {
      LineLen += AsciiSPrint (
                   Line + LineLen,
                   sizeof (Line) - LineLen,
                   "%02x ",
                   Ptr[Offset + i]
                   );
    }

    LineLen += AsciiSPrint (
                 Line + LineLen,
                 sizeof (Line) - LineLen,
                 "\r\n"
                 );

    ProbeUartWrite (Line, LineLen);
  }
}

STATIC
EFI_STATUS
EFIAPI
SmmProbeHandler (
  IN     EFI_HANDLE  DispatchHandle,
  IN     CONST VOID  *Context         OPTIONAL,
  IN OUT VOID        *CommBuffer      OPTIONAL,
  IN OUT UINTN       *CommBufferSize  OPTIONAL
  )
{
  volatile PROBE_MAILBOX  *Mailbox;

  Mailbox = (volatile PROBE_MAILBOX *)(UINTN)MAILBOX_PHYS_ADDR;

  if (Mailbox->Signature != MAILBOX_SIG) {
    return EFI_SUCCESS;
  }

  switch (Mailbox->Command) {
    case MAILBOX_CMD_PING:
      ProbeUartPrint ("PONG\r\n");
      Mailbox->Status = 0;
      break;

    case MAILBOX_CMD_READ:
      HexDump ((UINTN)Mailbox->PhysicalAddress, (UINTN)Mailbox->Length);
      Mailbox->Status = 0;
      break;

    default:
      ProbeUartPrint ("ERR: unknown command\r\n");
      Mailbox->Status = 1;
      break;
  }

  Mailbox->Signature = 0;

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
SmmProbeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  ProbeUartInit ();
  ProbeUartPrint ("SmmProbe: control channel active\r\n");

  DEBUG ((DEBUG_INFO, "SmmProbe: loaded at %p\n", ImageHandle));

  Status = gSmst->SmiHandlerRegister (
                    SmmProbeHandler,
                    NULL,
                    &mSmiHandle
                    );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "SmmProbe: SmiHandlerRegister failed: %r\n", Status));
    return Status;
  }

  DEBUG ((DEBUG_INFO, "SmmProbe: root SMI handler registered\n"));

  ProbeUartPrint ("SmmProbe: self-test, 64 bytes at 0x1000:\r\n");
  HexDump (0x1000, 64);

  return EFI_SUCCESS;
}
