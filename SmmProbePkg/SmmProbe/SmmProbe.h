/** @file
  Shared definitions for the SmmProbe driver and any future OS-side
  trigger utility.

  Copyright (c) 2026, SmmProbePkg contributors.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef SMM_PROBE_H_
#define SMM_PROBE_H_

#include <Uefi.h>

#define MAX_PROBE_SIZE           4096
#define PROBE_COMMAND_SIGNATURE  0x50524F42  // 'PROB'

#define PROBE_STATUS_SUCCESS  0
#define PROBE_STATUS_ERROR    1

#pragma pack(1)

typedef struct {
  UINT32    Signature;        // Must be PROBE_COMMAND_SIGNATURE
  UINT64    PhysicalAddress;  // Target physical address to read
  UINT32    Length;            // Bytes to read (max MAX_PROBE_SIZE)
  UINT32    Status;           // Output: set by handler
} PROBE_COMMAND;

#pragma pack()

#endif // SMM_PROBE_H_
