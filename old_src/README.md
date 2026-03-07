# old_src — legacy firmware sources (reference only, not compiled)

This directory contains the original JLPiCart firmware, preserved as a technology testbed reference.

It is **not part of the build**. Nothing here is compiled. No include paths point here.

Its purpose is to document proven implementation patterns — particularly the MSX bus loop (`bus/bus.cc`), mapper structs (`mappers/mappers.h`), and the cartridge callback abstraction (`cartridges/cartridge.h`) — that inform the new implementation under `fw/src/`.
