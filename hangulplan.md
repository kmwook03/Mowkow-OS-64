  # Default Hangul Support For 64-bit Step 11

  ## Summary

  Add Hangul as the default 64-bit console mode before app porting by restoring the 32-bit OS’s core Korean pipeline in
  the 64-bit tree: two-beolsik keyboard input, UTF-8 command buffer storage, Johab/H04 font rendering, and Korean command
  matching. Because VGA text mode cannot render Hangul glyphs, first switch the 64-bit console to pixel-based VBE output.

  ## Key Changes

  - Extend the 64-bit boot path to enter an 8-bit linear framebuffer VBE mode before long mode, preferring 800x600x8 with
    a text-mode fallback only for boot failure diagnostics.

  - Expand BOOTINFO64 with framebuffer metadata: vram, scrnx, scrny, bytes_per_scanline, bpp, and a mode flag.
  - Add src64/lib/utf864.c and src64/lib/hangul64.c by porting the reusable logic from src/lib/utf8.c and src/lib/
    hangul.c, but remove dependencies on 32-bit TASK, SHEET, fixed addresses like 0x0fe8, and pointer-through-int
    patterns.

  - Add a 64-bit graphics/text layer that can draw ASCII 8x16 glyphs and Hangul 16x16 glyphs directly into the framebuffer
    using hankaku plus H04.FNT.

  - Include H04.FNT in the 64-bit FAT12 image via mkfat12_64.py, load it during kernel64_main, and keep the font pointer
    in normal 64-bit kernel state.

  - Replace console64.c’s byte-oriented VGA cells with UTF-8 aware console output:
      - ASCII advances 8 pixels.
      - Hangul syllables and jamo advance 16 pixels.
      - newline, wrapping, scrolling, and backspace operate by visual character width.

  - Replace the fixed ASCII-only scancode map with a 64-bit keyboard translator supporting shift and two-beolsik letters.
  - Make Hangul mode the initial/default input mode. Use Shift+Space as a language toggle, matching the 32-bit OS
    behavior.

  - Store console input as UTF-8 bytes. Composing Hangul remains provisional on screen until committed by the automata,
    then written as UTF-8 into input_line.

  - Keep existing English commands working, and make Korean command aliases typeable and visible, starting with 목록 for
    ls.

  ## Public Interfaces / Types
    *boot_info).

  - Add console64_put_utf8(const char *s) or make console64_puts UTF-8 aware internally.
  - Add a small 64-bit Hangul state type, for example struct HANGUL64 { int state, cho, jung, jong; };.
  - Add renderer APIs that accept explicit font/framebuffer pointers rather than global magic addresses.

  ## Test Plan

  - make x86_64 builds cleanly.
  - make run64 boots to the graphical 64-bit console and shows ASCII text normally.
  - Type help, ls, and type readme.txt; existing Step 11 behavior still works.
  - Type ahrhr in default Hangul mode and verify 목록 appears visibly and runs the directory listing.
  - Press Shift+Space, type English text, press Shift+Space again, and verify mode switching does not corrupt input.
  - Test Hangul composition/backspace cases: simple syllable, compound vowel, final consonant, compound final consonant,
    and delete while composing.

  - Verify Korean file contents printed by type render correctly when UTF-8 Hangul exists in a FAT12 file.

  ## Assumptions

  - Visible Hangul in QEMU is required before app porting, so framebuffer rendering is part of this step.
  - The 32-bit tree remains untouched as the reference implementation.
  - The 64-bit app ABI is still out of scope; this work only covers kernel console/input/output support.
  - The first graphics target is an 8-bit VBE linear framebuffer to reuse the current palette/font model with minimal
    risk.