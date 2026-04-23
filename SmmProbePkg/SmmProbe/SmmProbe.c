/** @file
  SMM physical memory probe driver.

  Registers a root SMI handler that reads commands from a fixed
  physical memory mailbox and writes results to a dedicated UART.
  Supports continuous monitoring via the ICH9 SWSMI timer at 60Hz
  with results written to a physical memory ring buffer.

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
#include <Library/PciLib.h>

#include "SmmProbe.h"

STATIC EFI_HANDLE  mSmiHandle     = NULL;
STATIC BOOLEAN     mWatchActive   = FALSE;
STATIC UINT64      mWatchAddress  = 0;
STATIC UINT32      mWatchLength   = 0;

//
// UART helpers (output only, for mailbox command responses).
//

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

//
// Ring buffer helpers (for high-speed continuous capture).
//

STATIC
VOID
RingInit (
  VOID
  )
{
  volatile RING_HEADER  *Hdr;

  Hdr = (volatile RING_HEADER *)(UINTN)RING_PHYS_ADDR;
  Hdr->WriteOffset = sizeof (RING_HEADER);
  Hdr->ReadOffset  = sizeof (RING_HEADER);
  Hdr->EntryCount  = 0;
  Hdr->Overflow    = 0;
}

STATIC
VOID
RingWrite (
  IN UINT64  Address,
  IN UINT32  Length
  )
{
  volatile RING_HEADER  *Hdr;
  RING_ENTRY            Entry;
  UINT32                TotalSize;
  UINT32                WriteOfs;
  UINT32                Available;
  UINT8                 *RingBase;
  UINT8                 *Src;

  if (Length > MAX_PROBE_SIZE) {
    Length = MAX_PROBE_SIZE;
  }

  Hdr       = (volatile RING_HEADER *)(UINTN)RING_PHYS_ADDR;
  RingBase  = (UINT8 *)(UINTN)RING_PHYS_ADDR;
  TotalSize = sizeof (RING_ENTRY) + Length;
  WriteOfs  = Hdr->WriteOffset;

  Available = RING_SIZE - WriteOfs;
  if (Available < TotalSize) {
    //
    // Wrap around. Reset to start of data area.
    //
    WriteOfs = sizeof (RING_HEADER);
    Hdr->Overflow++;
  }

  Entry.Timestamp = AsmReadTsc ();
  Entry.Length     = Length;
  Entry.Reserved   = 0;

  CopyMem (RingBase + WriteOfs, &Entry, sizeof (RING_ENTRY));
  WriteOfs += sizeof (RING_ENTRY);

  Src = (UINT8 *)(UINTN)Address;
  CopyMem (RingBase + WriteOfs, Src, Length);
  WriteOfs += Length;

  Hdr->WriteOffset = WriteOfs;
  Hdr->EntryCount++;
}

//
// SWSMI timer control.
//

STATIC
VOID
SwSmiTimerEnable (
  IN UINT8  Rate
  )
{
  UINT8   GenPmCon1;
  UINT32  SmiEn;

  //
  // Set SWSMI rate in GEN_PMCON_1 (PCI 0:1F.0 register 0xA0).
  //
  GenPmCon1 = PciRead8 (ICH9_LPC_PCI_ADDR (ICH9_GEN_PMCON_1));
  GenPmCon1 = (GenPmCon1 & ~SWSMI_RATE_MASK) | (Rate & SWSMI_RATE_MASK);
  PciWrite8 (ICH9_LPC_PCI_ADDR (ICH9_GEN_PMCON_1), GenPmCon1);

  //
  // Enable SWSMI timer in SMI_EN.
  //
  SmiEn = IoRead32 (ICH9_PMBASE_VALUE + ICH9_SMI_EN_OFS);
  SmiEn |= ICH9_SMI_EN_SWSMI_TMR | ICH9_SMI_EN_GBL_SMI;
  IoWrite32 (ICH9_PMBASE_VALUE + ICH9_SMI_EN_OFS, SmiEn);
}

STATIC
VOID
SwSmiTimerDisable (
  VOID
  )
{
  UINT32  SmiEn;

  SmiEn = IoRead32 (ICH9_PMBASE_VALUE + ICH9_SMI_EN_OFS);
  SmiEn &= ~ICH9_SMI_EN_SWSMI_TMR;
  IoWrite32 (ICH9_PMBASE_VALUE + ICH9_SMI_EN_OFS, SmiEn);
}

//
// Root SMI handler.
//

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
  UINT32                  SmiSts;

  //
  // Clear SWSMI timer status so it re-arms.
  //
  SmiSts = IoRead32 (ICH9_PMBASE_VALUE + ICH9_SMI_STS_OFS);
  if ((SmiSts & ICH9_SMI_STS_SWSMI_TMR) != 0) {
    IoWrite32 (ICH9_PMBASE_VALUE + ICH9_SMI_STS_OFS, ICH9_SMI_STS_SWSMI_TMR);
  }

  if ((SmiSts & ICH9_SMI_STS_PERIODIC) != 0) {
    IoWrite32 (ICH9_PMBASE_VALUE + ICH9_SMI_STS_OFS, ICH9_SMI_STS_PERIODIC);
  }

  //
  // Continuous watch: copy target memory into ring buffer.
  //
  if (mWatchActive) {
    RingWrite (mWatchAddress, mWatchLength);
  }

  //
  // Check mailbox for commands.
  //
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

    case MAILBOX_CMD_WATCH:
      if (Mailbox->Length == 0) {
        mWatchActive = FALSE;
        SwSmiTimerDisable ();
        ProbeUartPrint ("WATCH: stopped\r\n");
      } else {
        mWatchAddress = Mailbox->PhysicalAddress;
        mWatchLength  = Mailbox->Length;
        if (mWatchLength > MAX_PROBE_SIZE) {
          mWatchLength = MAX_PROBE_SIZE;
        }

        RingInit ();
        mWatchActive = TRUE;
        SwSmiTimerEnable (SWSMI_RATE_16MS);
        ProbeUartPrint ("WATCH: started at 60Hz\r\n");
      }

      Mailbox->Status = 0;
      break;

    default:
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

  RingInit ();

  //
  // Enable periodic SMI as fallback for mailbox polling.
  //
  {
    UINT32  SmiEn;

    SmiEn = IoRead32 (ICH9_PMBASE_VALUE + ICH9_SMI_EN_OFS);
    SmiEn |= ICH9_SMI_EN_PERIODIC | ICH9_SMI_EN_GBL_SMI;
    IoWrite32 (ICH9_PMBASE_VALUE + ICH9_SMI_EN_OFS, SmiEn);
  }

  ProbeUartPrint ("SmmProbe: ready\r\n");

  return EFI_SUCCESS;
}
