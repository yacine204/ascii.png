# Ascii.png

A from scratch PNG decoder and terminal renderer written in C.

Parses PNG chunks manually, decompresses IDAT data with zlib, applies all five PNG defiltering passes, then renders the result to your terminal using Unicode half-block characters with 24 bit true color ANSI escapes.

![test_image](showcase.png)

Features:
- Manual PNG chunk parsing (IHDR, IDAT, IEND)
- Full filter reconstruction (None, Sub, Up, Average, Paeth)
- True color output (16 million colors)
- Half-block rendering for 2x vertical pixel density
- Auto-scales to terminal size via ioctl

## Build:

```bash
$ gcc main.c -o main -lz -lm
```

## Usage:

```
./main <image_path> <verbose:optional>
```
