/**
 * SPDX-License-Identifier: MPL-2.0
 *
 * LVGL v9 configuration for UltiMaker 2+ Connect (Deneb)
 * Target: MT7688 MIPS, ILI9341 320x240 SPI TFT, RGB565
 * Minimal RAM/CPU, fast transitions
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   COLOR
 *====================*/
#define LV_COLOR_DEPTH          16

/*====================
   MEMORY
 *====================*/
#define LV_MEM_CUSTOM           0
#define LV_MEM_SIZE             (160U * 1024U)

/*====================
   STDLIB
 *====================*/
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING    LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_BUILTIN

/*====================
   HAL / OS
 *====================*/
#define LV_USE_OS               LV_OS_NONE
#define LV_DPI_DEF              120
#define LV_DEF_REFR_PERIOD      16

/*====================
   DRAW
 *====================*/
#define LV_USE_DRAW_SW          1
#define LV_DRAW_BUF_STRIDE_ALIGN  1
#define LV_DRAW_BUF_ALIGN         4

/*====================
   FEATURE CONFIG
 *====================*/
#define LV_USE_ANIMATION        1
#define LV_USE_FLOAT            0
#define LV_USE_MATRIX           0

/*====================
   FONTS
 *====================*/
#define LV_FONT_MONTSERRAT_8    0
#define LV_FONT_MONTSERRAT_10   0
#define LV_FONT_MONTSERRAT_12   1
#define LV_FONT_MONTSERRAT_14   1
#define LV_FONT_MONTSERRAT_16   1
#define LV_FONT_MONTSERRAT_18   0
#define LV_FONT_MONTSERRAT_20   0
#define LV_FONT_MONTSERRAT_22   0
#define LV_FONT_MONTSERRAT_24   0
#define LV_FONT_MONTSERRAT_26   0
#define LV_FONT_MONTSERRAT_28   0
#define LV_FONT_MONTSERRAT_30   0
#define LV_FONT_MONTSERRAT_32   0
#define LV_FONT_MONTSERRAT_34   0
#define LV_FONT_MONTSERRAT_36   0
#define LV_FONT_MONTSERRAT_38   0
#define LV_FONT_MONTSERRAT_40   0
#define LV_FONT_MONTSERRAT_42   0
#define LV_FONT_MONTSERRAT_44   0
#define LV_FONT_MONTSERRAT_46   0
#define LV_FONT_MONTSERRAT_48   0
#define LV_FONT_DEFAULT         &lv_font_montserrat_14
#define LV_FONT_COMPRESSED      0
#define LV_FONT_PLACEHOLDER     1

/*====================
   WIDGETS - disable all we don't use
 *====================*/
#define LV_USE_ANIMIMG          0
#define LV_USE_ARC              0
#define LV_USE_ARCLABEL         0
#define LV_USE_BAR              1
#define LV_USE_BUTTON           1
#define LV_USE_BUTTONMATRIX     0
#define LV_USE_CALENDAR         0
#define LV_USE_CANVAS           0
#define LV_USE_CHART            0
#define LV_USE_CHECKBOX         0
#define LV_USE_DROPDOWN         0
#define LV_USE_IMAGE            1
#define LV_USE_IMAGEBUTTON      0
#define LV_USE_KEYBOARD         0
#define LV_USE_LABEL            1
#define LV_USE_LED              0
#define LV_USE_LINE             0
#define LV_USE_LIST             0
#define LV_USE_LOTTIE           0
#define LV_USE_MENU             0
#define LV_USE_MSGBOX           0
#define LV_USE_ROLLER           0
#define LV_USE_SCALE            0
#define LV_USE_SLIDER           1
#define LV_USE_SPAN             0
#define LV_USE_SPINBOX          0
#define LV_USE_SPINNER          0
#define LV_USE_SWITCH           1
#define LV_USE_TABLE            0
#define LV_USE_TABVIEW          0
#define LV_USE_TEXTAREA         0
#define LV_USE_TILEVIEW         0
#define LV_USE_WIN              0
#define LV_USE_3DTEXTURE        0

/*====================
   THEMES
 *====================*/
#define LV_USE_THEME_DEFAULT    1
#define LV_THEME_DEFAULT_DARK   1
#define LV_USE_THEME_SIMPLE     0
#define LV_USE_THEME_MONO       0

/*====================
   LAYOUT
 *====================*/
#define LV_USE_FLEX             1
#define LV_USE_GRID             0

/*====================
   LOGGING
 *====================*/
#define LV_USE_LOG              1
#define LV_LOG_LEVEL            LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF           1

/*====================
   ASSERTIONS
 *====================*/
#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1
#define LV_USE_ASSERT_STYLE         0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ           0

/*====================
   DISABLE EVERYTHING ELSE
 *====================*/
#define LV_USE_BIDI                 0
#define LV_USE_ARABIC_PERSIAN_CHARS 0
#define LV_USE_OBJ_ID              0
#define LV_USE_OBJ_NAME            0
#define LV_USE_OBJ_PROPERTY        0
#define LV_USE_GESTURE_RECOGNITION 0
#define LV_USE_NEMA_GFX            0
#define LV_USE_PXP                 0
#define LV_USE_G2D                 0
#define LV_USE_DRAW_DAVE2D         0
#define LV_USE_DRAW_SDL            0
#define LV_USE_DRAW_VG_LITE        0
#define LV_USE_DRAW_DMA2D          0
#define LV_USE_DRAW_OPENGLES       0
#define LV_USE_PPA                 0
#define LV_USE_DRAW_EVE            0
#define LV_USE_DRAW_NANOVG         0
#define LV_USE_FS_STDIO            0
#define LV_USE_FS_POSIX            1
#define LV_USE_FS_WIN32            0
#define LV_USE_FS_FATFS            0
#define LV_USE_FS_MEMFS            0
#define LV_USE_FS_LITTLEFS         0

#define LV_FS_POSIX_LETTER         'A'
#define LV_FS_POSIX_PATH           ""
#define LV_FS_POSIX_CACHE_SIZE     0

#define LV_CACHE_DEF_SIZE          (80U * 1024U)

#define LV_USE_LODEPNG             1
#define LV_USE_LIBPNG              0
#define LV_USE_BMP                 0
#define LV_USE_TJPGD               0
#define LV_USE_LIBJPEG_TURBO       0
#define LV_USE_LIBWEBP             0
#define LV_USE_GIF                 0
#define LV_USE_GSTREAMER           0
#define LV_USE_RLE                 0
#define LV_USE_QRCODE              0
#define LV_USE_BARCODE             0
#define LV_USE_FREETYPE            0
#define LV_USE_TINY_TTF            0
#define LV_USE_RLOTTIE             0
#define LV_USE_GLTF                0
#define LV_USE_VECTOR_GRAPHIC      0
#define LV_USE_THORVG_INTERNAL     0
#define LV_USE_THORVG_EXTERNAL     0
#define LV_USE_NANOVG              0
#define LV_USE_LZ4_INTERNAL        0
#define LV_USE_LZ4_EXTERNAL        0
#define LV_USE_SVG                 0
#define LV_USE_SVG_ANIMATION       0
#define LV_USE_FFMPEG              0
#define LV_USE_SNAPSHOT            0
#define LV_USE_SYSMON              0
#define LV_USE_PROFILER            0
#define LV_USE_MONKEY              0
#define LV_USE_GRIDNAV             0
#define LV_USE_FRAGMENT            0
#define LV_USE_IMGFONT             0
#define LV_USE_OBSERVER            0
#define LV_USE_IME_PINYIN          0
#define LV_USE_FILE_EXPLORER       0
#define LV_USE_FONT_MANAGER        0
#define LV_USE_TEST                0
#define LV_USE_TRANSLATION         0
#define LV_USE_COLOR_FILTER        0
#define LV_USE_SDL                 0
#define LV_USE_X11                 0
#define LV_USE_WAYLAND             0
#define LV_USE_LINUX_FBDEV         0
#define LV_USE_NUTTX               0
#define LV_USE_LINUX_DRM           0
#define LV_USE_TFT_ESPI            0
#define LV_USE_LOVYAN_GFX          0
#define LV_USE_EVDEV               1
#define LV_USE_LIBINPUT            0
#define LV_USE_ST7735              0
#define LV_USE_ST7789              0
#define LV_USE_ST7796              0
#define LV_USE_ILI9341             0
#define LV_USE_WINDOWS             0
#define LV_USE_UEFI                0
#define LV_USE_OPENGLES            0
#define LV_USE_GLFW                0
#define LV_USE_QNX                 0
#define LV_USE_EXT_DATA            0

#endif /* LV_CONF_H */
