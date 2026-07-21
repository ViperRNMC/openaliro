<!-- generated documentation — edit the source, not this file -->
# `scripts/docs.sh`

docs.sh — build the documentation site into site/.
Two generators write into the same output directory, in this order:
1. the subsystem tree + guides + search shell   -> site/*.html
2. doxygen (docs/Doxyfile)                      -> site/api/
then a link pass rewrites cross-document links so the published site has no
dead ends, and the freshness gate confirms the committed docs/ tree matches
the source. Run it through `make docs`.
Nothing here needs the NCS toolchain or hardware.

**discussed in** [`docs/RELEASING.md`](../../RELEASING.md)
