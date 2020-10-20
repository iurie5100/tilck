/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once
#include <tilck/common/basic_defs.h>
#include <tilck/common/page_size.h>
#include <tilck/common/gfx.h>
#include <tilck/common/failsafe_assert.h>
#include <tilck/common/string_util.h>
#undef ASSERT

#include <efi.h>
#include <efilib.h>
#include <multiboot.h>

extern multiboot_info_t *mbi;
extern bool any_warnings;

EFI_STATUS
LoadKernelFile(EFI_FILE_PROTOCOL *fileProt,
               EFI_PHYSICAL_ADDRESS *filePaddr);

EFI_STATUS AllocateMbi(void);
EFI_STATUS MultibootSaveMemoryMap(UINTN *mapkey);
EFI_STATUS MbiSetBootloaderName(void);
EFI_STATUS MbiSetPointerToAcpiTable(void);
EFI_STATUS MbiSetRamdisk(EFI_PHYSICAL_ADDRESS ramdisk_paddr,
                         UINTN ramdisk_size);


EFI_STATUS
MbiSetFramebufferInfo(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mode_info,
                      UINTN fb_addr);

EFI_STATUS
LoadRamdisk(EFI_HANDLE image,
            EFI_LOADED_IMAGE *loaded_image,
            EFI_PHYSICAL_ADDRESS *ramdisk_paddr_ref,
            UINTN *ramdisk_size,
            UINTN CurrentConsoleRow); /* HACK: see ShowProgress() */

EFI_STATUS
SetupGraphicMode(UINTN *fb_addr                                  /* out */,
                 EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mode_info /* out */);

EFI_STATUS
EarlySetDefaultResolution(void);
