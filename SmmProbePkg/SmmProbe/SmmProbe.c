/** @file
  SMM physical memory probe driver.

  Registers a software SMI handler that reads arbitrary physical memory
  from SMM context and logs results to the serial port. This demonstrates
  that SMM (ring -2) can access memory that is protected from ring 0 and
  ring -1 by VBS/HVCI/Credential Guard.

  The driver also performs a self-test on load by reading a page of low
  memory and dumping the first 64 bytes to serial.

  Trigger the SMI handler from the guest OS by writing to IO port 0xB2.
  The communication buffer (passed via EFI_SMM_SW_DISPATCH2_PROTOCOL or
  direct MMIO) specifies the target physical address and length.

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

/**
  Hex-dump a region of physical memory to the serial port.

  @param[in] Address  Physical address to read.
  @param[in] Length   Number of bytes to dump (capped at MAX_PROBE_SIZE).
**/
STATIC
VOID
HexDumpToSerial (
  IN UINTN  Address,
  IN UINTN  Length
  )
{
  UINTN        Offset;
  UINT8        *Ptr;
  CHAR8        Line[80];
  UINTN        LineLen;
  UINTN        i;

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

    SerialPortWrite ((UINT8 *)Line, LineLen);
  }
}

/**
  Root SMI handler for memory probe requests.

  When a software SMI is triggered, this handler checks the communication
  buffer for a PROBE_COMMAND structure. If valid, it reads the requested
  physical memory and dumps it to serial.

  Without a communication buffer, it performs a default probe of low memory
  as a connectivity test.

  @param[in]     DispatchHandle   Unused.
  @param[in]     Context          Unused.
  @param[in,out] CommBuffer       Pointer to PROBE_COMMAND, or NULL.
  @param[in,out] CommBufferSize   Size of CommBuffer.

  @retval EFI_SUCCESS  Handler executed.
**/
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
  PROBE_COMMAND  *Cmd;
  STATIC CONST CHAR8  Banner[] = "\r\n[SmmProbe] SMI triggered - probing memory\r\n";

  SerialPortWrite ((UINT8 *)Banner, sizeof (Banner) - 1);

  if ((CommBuffer != NULL) &&
      (CommBufferSize != NULL) &&
      (*CommBufferSize >= sizeof (PROBE_COMMAND)))
  {
    Cmd = (PROBE_COMMAND *)CommBuffer;

    if (Cmd->Signature == PROBE_COMMAND_SIGNATURE) {
      DEBUG ((
        DEBUG_INFO,
        "[SmmProbe] Reading %u bytes at physical address 0x%016lx\n",
        Cmd->Length,
        Cmd->PhysicalAddress
        ));

      HexDumpToSerial ((UINTN)Cmd->PhysicalAddress, (UINTN)Cmd->Length);

      Cmd->Status = PROBE_STATUS_SUCCESS;
      return EFI_SUCCESS;
    }
  }

  //
  // No valid command buffer — do a default probe of address 0x1000
  // (typically the real-mode IVT area or zeroed memory on UEFI systems).
  //
  DEBUG ((DEBUG_INFO, "[SmmProbe] No command buffer, probing default address 0x1000\n"));
  HexDumpToSerial (0x1000, 64);

  return EFI_SUCCESS;
}

/**
  Entry point. Registers the root SMI handler and performs a self-test.

  @param[in] ImageHandle  Driver image handle.
  @param[in] SystemTable  EFI System Table.

  @retval EFI_SUCCESS  Handler registered.
**/
EFI_STATUS
EFIAPI
SmmProbeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS      Status;
  STATIC CONST CHAR8  Banner[] =
    "\r\n"
    "========================================\r\n"
    " SmmProbe loaded - ring -2 memory probe\r\n"
    "========================================\r\n";

  SerialPortWrite ((UINT8 *)Banner, sizeof (Banner) - 1);

  DEBUG ((DEBUG_INFO, "[SmmProbe] ImageHandle = %p\n", ImageHandle));
  DEBUG ((DEBUG_INFO, "[SmmProbe] gSmst       = %p\n", gSmst));
  DEBUG ((
    DEBUG_INFO,
    "[SmmProbe] SMRAM NumberOfCpus = %lu\n",
    (UINT64)gSmst->NumberOfCpus
    ));

  //
  // Register a root SMI handler. Root handlers fire on every SMI,
  // regardless of source. For targeted triggering, use
  // EFI_SMM_SW_DISPATCH2_PROTOCOL instead (future enhancement).
  //
  Status = gSmst->SmiHandlerRegister (
                    SmmProbeHandler,
                    NULL,  // root handler — no specific GUID filter
                    &mSmiHandle
                    );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "[SmmProbe] SmiHandlerRegister failed: %r\n", Status));
    return Status;
  }

  DEBUG ((DEBUG_INFO, "[SmmProbe] Root SMI handler registered at handle %p\n", mSmiHandle));

  //
  // Self-test: read 64 bytes from physical address 0x1000.
  //
  DEBUG ((DEBUG_INFO, "[SmmProbe] Self-test: dumping 64 bytes at 0x1000\n"));
  HexDumpToSerial (0x1000, 64);

  return EFI_SUCCESS;
}
