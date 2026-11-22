# Fonts

`noto_sans_20.c` and `noto_sans_28.c` are Noto Sans Regular at 20 and 28 px, for LVGL:
the map's labels, and the screen's text. LVGL's own Montserrat has no Greek letters, which
star names use (α Tau). They hold printable ASCII, Latin-1 and Latin Extended-A, Greek, some
punctuation, primes and the minus sign.

- **Font:** Noto Sans, Copyright 2022 The Noto Project Authors
  (https://github.com/notofonts/latin-greek-cyrillic), under the SIL Open Font License 1.1,
  in `OFL.txt` here. The OFL allows bundling the font with software under any licence.
- **Made with** lv_font_conv 1.5.2 (MIT), from `NotoSans-Regular.ttf`:

  ```sh
  npx lv_font_conv@1.5.2 --font NotoSans-Regular.ttf \
    -r 0x20-0x7E,0xA0-0x17F,0x370-0x3FF,0x2010-0x2027,0x2032-0x2033,0x2212 \
    --size 20 --bpp 4 --no-compress --format lvgl --lv-include lvgl.h -o noto_sans_20.c
  ```

  and the same with `--size 28` for `noto_sans_28.c`.
