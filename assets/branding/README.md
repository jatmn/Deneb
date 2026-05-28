# Deneb Branding Assets

Source image:

- `deneb-tail-of-cygnus-source.png`
  - PNG, 1264x848
  - Original Deneb "Tail of the Cygnus" splash artwork.

Generated variants:

- `deneb-boot-320x240.png`
  - PNG, 320x240
  - Center-cropped and scaled for the UM2C touchscreen framebuffer/LVGL display size.

- `deneb-splash-128x102.jpg`
  - JPEG, 128x102
  - Center-cropped and scaled to match the stock nodogsplash `splash.jpg` dimensions.

- `deneb-loading-mark-48x48.png`
  - PNG, 48x48
  - Cropped mark variant sized like the existing LVGL icon/loading assets.

These assets are not wired into the extracted `rootfs` yet. The extracted firmware tree is intentionally ignored by git, so replacement should happen from packaging or source overlays once the boot/loading path is selected.
