# Musashi upstream

This directory vendors Musashi from:

- Repository: https://github.com/kstenerud/Musashi
- Commit: `313ebf1bd9f4d0d93341eb5ce21fd8a119e9dbdd`
- Version reported by the generator: 4.60

`m68kops.c` and `m68kops.h` were generated from the upstream `m68k_in.c`
using the upstream `m68kmake.c` generator. The generated files are committed so
cross builds do not need to execute a freshly built host generator.

Musashi's permissive license text is included in `readme.txt` and in each
upstream source file.
