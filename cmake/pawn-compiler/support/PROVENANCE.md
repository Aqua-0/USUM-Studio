# Pawn compiler Linux support

The five support files (`binreloc.c`, `binreloc.h`, `getch.c`, `getch.h`, and `sclinux.h`) are copied byte-for-byte from the `linux/` directory of [CompuPhase Pawn](https://codeberg.org/compuphase/pawn), revision [`7bbdaf45f239f0833ded6bef90e1eb0281ca1f69`](https://codeberg.org/compuphase/pawn/src/commit/7bbdaf45f239f0833ded6bef90e1eb0281ca1f69/linux).

They provide the Linux support directory missing from [SciresM/gf-pawncc revision 54c45cbd56bdfc433aa3039efd45067e1c472cfe](https://github.com/SciresM/gf-pawncc/tree/54c45cbd56bdfc433aa3039efd45067e1c472cfe).

`LICENSE` and `NOTICE` are from the same CompuPhase Pawn revision. BinReloc declares its files public domain in their headers. The compiler's own license and notice are packaged separately. No game code or game-derived fixtures are included.
