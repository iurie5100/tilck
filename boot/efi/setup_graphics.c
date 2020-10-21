/* SPDX-License-Identifier: BSD-2-Clause */

#include <tilck_gen_headers/config_boot.h>

#include "defs.h"
#include "utils.h"

#include <multiboot.h>

static void PrintModeInfo(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mi)
{
   Print(L"Resolution: %u x %u\n",
         mi->HorizontalResolution,
         mi->VerticalResolution);

   if (mi->PixelFormat == PixelRedGreenBlueReserved8BitPerColor)
      Print(L"PixelFormat: RGB + reserved\n");
   else if (mi->PixelFormat == PixelBlueGreenRedReserved8BitPerColor)
      Print(L"PixelFormat: BGR + reserved\n");
   else
      Print(L"PixelFormat: other\n");

   Print(L"PixelsPerScanLine: %u\n", mi->PixelsPerScanLine);
}

static void PrintModeFullInfo(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *mode)
{
   Print(L"Framebuffer addr: 0x%x\n", mode->FrameBufferBase);
   Print(L"Framebuffer size: %u\n", mode->FrameBufferSize);
   PrintModeInfo(mode->Info);
}

static bool IsSupported(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mi)
{
   if (sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL) != 4)
      return false;

   if (!is_tilck_usable_resolution(mi->HorizontalResolution,
                                   mi->VerticalResolution))
   {
      return false;
   }

   return mi->PixelFormat == PixelBlueGreenRedReserved8BitPerColor ||
          mi->PixelFormat == PixelRedGreenBlueReserved8BitPerColor;
}

/*
 * Find a good video mode for the bootloader itself.
 *
 * This function is called in EarlySetDefaultResolution(), before displaying
 * anything on the screen. It solves the problem with modern machines with
 * "retina" displays, where just using the native resolution with the default
 * EFI font results in extremely tiny text, a pretty bad user experience.
 */
static EFI_STATUS
FindGoodVideoMode(EFI_GRAPHICS_OUTPUT_PROTOCOL *gProt,
                  bool supported,
                  INTN *choice)
{
   INTN chosenMode = -1, minResPixels = 0, minResModeN = -1;
   EFI_STATUS status = EFI_SUCCESS;

   for (UINTN i = 0; i < gProt->Mode->MaxMode; i++) {

      EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mi = NULL;
      UINTN sizeof_info = 0;

      status = gProt->QueryMode(gProt, i, &sizeof_info, &mi);
      HANDLE_EFI_ERROR("QueryMode() failed");

      if (supported && !IsSupported(mi))
         continue;

      /*
       * NOTE: it's fine to use a resolution not supported by Tilck here.
       * We just need any good-enough and low resolution for the displaying
       * stuff on the screen.
       */

      if (mi->HorizontalResolution == PREFERRED_GFX_MODE_W &&
          mi->VerticalResolution == PREFERRED_GFX_MODE_H)
      {
         chosenMode = (INTN) i;
         break; /* Our preferred resolution */
      }

      const INTN p = (INTN)(mi->HorizontalResolution * mi->VerticalResolution);

      if (p < minResPixels) {
         minResPixels = p;
         minResModeN = (INTN) i;
      }
   }

   if (chosenMode >= 0)
      *choice = chosenMode;
   else if (minResModeN >= 0)
      *choice = minResModeN;
   else
      *choice = -1;

end:
   return status;
}

EFI_STATUS
EarlySetDefaultResolution(void)
{
   EFI_STATUS status;
   EFI_HANDLE handles[32];
   UINTN handles_buf_size;
   UINTN handles_count;
   INTN origMode, chosenMode = -1;
   EFI_GRAPHICS_OUTPUT_PROTOCOL *gProt;

   ST->ConOut->ClearScreen(ST->ConOut);
   handles_buf_size = sizeof(handles);

   status = BS->LocateHandle(ByProtocol,
                             &GraphicsOutputProtocol,
                             NULL,
                             &handles_buf_size,
                             handles);

   HANDLE_EFI_ERROR("LocateHandle() failed");

   handles_count = handles_buf_size / sizeof(EFI_HANDLE);
   CHECK(handles_count > 0);

   status = BS->HandleProtocol(handles[0],
                               &GraphicsOutputProtocol,
                               (void **)&gProt);
   HANDLE_EFI_ERROR("HandleProtocol() failed");

   FindGoodVideoMode(gProt, true, &chosenMode);
   HANDLE_EFI_ERROR("FindGoodVideoMode() failed");

   if (chosenMode < 0) {

      /*
       * We were unable to find a good and supported (= 32 bps) video mode.
       * That's bad, but not fatal: just re-run FindGoodVideoMode() including
       * also non-32bps video modes. They are still perfectly fine for the
       * bootloader. The resolution used by Tilck instead, will be chosen later
       * directly by the user, among the available ones.
       */

      FindGoodVideoMode(gProt, false, &chosenMode);
      HANDLE_EFI_ERROR("FindGoodVideoMode() failed");

      if (chosenMode < 0) {
         /* Do nothing: just keep the current video mode */
         return status;
      }
   }

   origMode = (INTN) gProt->Mode->Mode;

   if (chosenMode == origMode)
      return status; /* We're already using a "good" video mode */

   status = gProt->SetMode(gProt, (UINTN)chosenMode);

   if (EFI_ERROR(status)) {
      /* Something went wrong: just restore the previous video mode */
      status = gProt->SetMode(gProt, (UINTN)origMode);
      HANDLE_EFI_ERROR("SetMode() failed");
   }

end:
   return status;
}

static void
PrintFailedModeInfo(EFI_GRAPHICS_OUTPUT_PROTOCOL *gProt, UINTN failed_mode)
{
   EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mi = NULL;
   UINTN sizeof_info = 0;
   EFI_STATUS status;

   status = gProt->QueryMode(gProt, failed_mode, &sizeof_info, &mi);

   if (!EFI_ERROR(status)) {
      Print(L"Failed mode info:\r\n");
      PrintModeInfo(mi);
   } else {
      Print(L"ERROR: Unable to print failed mode info: %r\r\n", status);
   }
}

static bool
SwitchToUserSelectedMode(EFI_GRAPHICS_OUTPUT_PROTOCOL *gProt,
                         UINTN wanted_mode,
                         UINTN orig_mode)
{
   EFI_STATUS status;

   ST->ConOut->ClearScreen(ST->ConOut);    /* NOTE: do not handle failures */
   status = gProt->SetMode(gProt, wanted_mode);

   if (EFI_ERROR(status)) {

      gProt->SetMode(gProt, orig_mode);    /* NOTE: do not handle failures */
      ST->ConOut->ClearScreen(ST->ConOut); /* NOTE: do not handle failures */

      Print(L"ERROR: Unable to set desired mode: %r\r\n", status);
      PrintFailedModeInfo(gProt, wanted_mode);
      return false;
   }

   return true;
}

static UINTN
GetUserModeChoice(struct ok_modes_info *okm)
{
   char buf[16];
   UINTN len;
   long sel;
   int err;

   while (true) {

      Print(L"Select mode [0 - %d]: ", okm->ok_modes_cnt - 1);
      len = ReadAsciiLine(buf, sizeof(buf));

      if (!len) {
         Print(L"<default>\r\n\r\n");
         return okm->defmode;
      }

      sel = tilck_strtol(buf, NULL, 10, &err);

      if (err || sel < 0 || sel > okm->ok_modes_cnt - 1) {
         Print(L"Invalid selection\n");
         continue;
      }

      break;
   }

   return okm->ok_modes[sel];
}

static bool
efi_boot_get_mode_info(void *ctx,
                       video_mode_t m,
                       void *opaque_info,
                       struct generic_video_mode_info *gi)
{
   EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **mi_ref = opaque_info;
   EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mi;
   EFI_GRAPHICS_OUTPUT_PROTOCOL *gProt = ctx;
   UINTN sizeof_info = 0;
   EFI_STATUS status;

   *mi_ref = NULL;
   status = gProt->QueryMode(gProt, m, &sizeof_info, mi_ref);

   if (EFI_ERROR(status))
      return false;

   mi = *mi_ref;

   gi->xres = mi->HorizontalResolution;
   gi->yres = mi->VerticalResolution;
   gi->bpp = 0;

   if (mi->PixelFormat == PixelBlueGreenRedReserved8BitPerColor ||
       mi->PixelFormat == PixelRedGreenBlueReserved8BitPerColor)
   {
      gi->bpp = 32;
   }

   return true;
}

static bool
efi_boot_is_mode_usable(void *ctx, void *opaque_info)
{
   EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **mi_ref = opaque_info;
   EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mi = *mi_ref;
   return IsSupported(mi);
}

static void
efi_boot_show_mode(void *ctx, int num, void *opaque_info, bool is_default)
{
   EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **mi_ref = opaque_info;
   EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mi = *mi_ref;

   Print(L"Mode [%d]: %u x %u%s\n",
         num,
         mi->HorizontalResolution,
         mi->VerticalResolution,
         is_default ? L" [DEFAULT]" : L"");
}

static const struct bootloader_intf efi_boot_intf = {
   .get_mode_info = &efi_boot_get_mode_info,
   .is_mode_usable = &efi_boot_is_mode_usable,
   .show_mode = &efi_boot_show_mode,
};

EFI_STATUS
SetupGraphicMode(UINTN *fb_addr,
                 EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mode_info)
{
   static video_mode_t ok_modes[16];   /* static: reduce stack usage */
   static EFI_HANDLE handles[32];      /* static: reduce stack usage */

   EFI_STATUS status;
   UINTN handles_count;
   UINTN handles_buf_size;
   EFI_GRAPHICS_OUTPUT_PROTOCOL *gProt;
   EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *mode;
   EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mi;

   video_mode_t wanted_mode;
   video_mode_t orig_mode;

   struct ok_modes_info okm = {
      .ok_modes = ok_modes,
      .ok_modes_array_size = ARRAY_SIZE(ok_modes),
      .ok_modes_cnt = 0,
      .defmode = INVALID_VIDEO_MODE,
   };

   handles_buf_size = sizeof(handles);

   status = BS->LocateHandle(ByProtocol,
                             &GraphicsOutputProtocol,
                             NULL,
                             &handles_buf_size,
                             handles);

   HANDLE_EFI_ERROR("LocateHandle() failed");

   handles_count = handles_buf_size / sizeof(EFI_HANDLE);
   CHECK(handles_count > 0);

   status = BS->HandleProtocol(handles[0],
                               &GraphicsOutputProtocol,
                               (void **)&gProt);
   HANDLE_EFI_ERROR("HandleProtocol() failed");

   mode = gProt->Mode;
   orig_mode = mode->Mode;

   if (!BOOT_INTERACTIVE)
      goto after_user_choice;

retry:

   filter_video_modes(&efi_boot_intf,  /* intf */
                      NULL,            /* all_modes */
                      mode->MaxMode,   /* all_modes_cnt */
                      &mi,             /* opaque_mode_info_buf */
                      true,            /* show_modes */
                      32,              /* bpp */
                      0,               /* ok_modes_start */
                      &okm,            /* okm */
                      gProt);          /* ctx */

   if (!okm.ok_modes_cnt) {
      Print(L"No supported modes available\n");
      status = EFI_LOAD_ERROR;
      goto end;
   }

   wanted_mode = GetUserModeChoice(&okm);

   if (wanted_mode != orig_mode) {
      if (!SwitchToUserSelectedMode(gProt, wanted_mode, orig_mode))
         goto retry;
   }

after_user_choice:

   mode = gProt->Mode;
   *fb_addr = mode->FrameBufferBase;
   *mode_info = *mode->Info;

end:
   return status;
}
