/** @file
  Minimal DXE_SMM_DRIVER that prints a greeting to the 16550 serial port
  on load and dumps a few fields from the SMM System Table for verification.

  Copyright (c) 2026, HelloSmmPkg contributors.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <PiSmm.h>
#include <Library/SmmServicesTableLib.h>
#include <Library/SerialPortLib.h>
#include <Library/DebugLib.h>

/**
  Entry point for the HelloSmm DXE_SMM_DRIVER.

  The SmmServicesTableLib constructor has already populated gSmst by the
  time this is called; it used the ImageHandle to locate
  EFI_SMM_BASE2_PROTOCOL and fetched the SMM System Table pointer.

  @param[in] ImageHandle  Driver image handle (in SMRAM-tracked form).
  @param[in] SystemTable  DXE System Table. Not used from SMM context
                          post-ReadyToLock; present because the entry
                          point signature is shared with DXE drivers.

  @retval EFI_SUCCESS  Greeting emitted.
**/
EFI_STATUS
EFIAPI
HelloSmmEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  STATIC CONST CHAR8  Greeting[] = "Hello from SMM!\r\n";

  //
  // Direct, unbuffered write to the 16550. BaseSerialPortLib16550 is
  // pure port I/O (default 0x3F8), so it is safe from SMM context.
  // No DXE services or runtime pointers are touched.
  //
  SerialPortWrite ((UINT8 *)Greeting, sizeof (Greeting) - 1);

  //
  // gSmst is EFI_SMM_SYSTEM_TABLE2 (see MdePkg/Include/Pi/PiSmmCis.h).
  // These fields confirm we are running against a live SMM dispatch.
  //
  DEBUG ((DEBUG_INFO, "HelloSmm: ImageHandle        = %p\n", ImageHandle));
  DEBUG ((DEBUG_INFO, "HelloSmm: gSmst              = %p\n", gSmst));
  DEBUG ((
    DEBUG_INFO,
    "HelloSmm: gSmst->Hdr.Revision = 0x%08x  HeaderSize = 0x%08x\n",
    gSmst->Hdr.Revision,
    gSmst->Hdr.HeaderSize
    ));
  DEBUG ((
    DEBUG_INFO,
    "HelloSmm: gSmst->SmmFirmwareRevision  = 0x%08x\n",
    gSmst->SmmFirmwareRevision
    ));
  DEBUG ((
    DEBUG_INFO,
    "HelloSmm: gSmst->NumberOfTableEntries = %u\n",
    gSmst->NumberOfTableEntries
    ));
  DEBUG ((
    DEBUG_INFO,
    "HelloSmm: gSmst->NumberOfCpus         = %lu\n",
    (UINT64)gSmst->NumberOfCpus
    ));

  return EFI_SUCCESS;
}
