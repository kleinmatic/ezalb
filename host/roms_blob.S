/* roms_blob.S — the firmware images, gzipped by the Makefile and linked in as
 * binary blobs (no source-code hexdumps). Consumed by host/roms.c. */

#ifdef __APPLE__
#define SYM(name) _##name
	.section __TEXT,__const
#else
#define SYM(name) name
	.section .rodata
#endif

	.p2align 4
	.globl SYM(rom_vt420)
	.globl SYM(rom_vt420_end)
SYM(rom_vt420):
	.incbin "build/roms/vt420/23-068E9-00.z"
SYM(rom_vt420_end):

	.p2align 4
	.globl SYM(rom_vt420_14)
	.globl SYM(rom_vt420_14_end)
SYM(rom_vt420_14):
	.incbin "build/roms/vt420/23-208E9-00.z"
SYM(rom_vt420_14_end):

	.p2align 4
	.globl SYM(rom_vt420_b14)
	.globl SYM(rom_vt420_b14_end)
SYM(rom_vt420_b14):
	.incbin "build/roms/vt420/2E-C394A-01.z"
SYM(rom_vt420_b14_end):

	.p2align 4
	.globl SYM(rom_vt510)
	.globl SYM(rom_vt510_end)
SYM(rom_vt510):
	.incbin "build/roms/vt510/23-032ED-00.z"
SYM(rom_vt510_end):

	.p2align 4
	.globl SYM(rom_vt520)
	.globl SYM(rom_vt520_end)
SYM(rom_vt520):
	.incbin "build/roms/vt520/23-010ED-00.z"
SYM(rom_vt520_end):

	.p2align 4
	.globl SYM(rom_vt525)
	.globl SYM(rom_vt525_end)
SYM(rom_vt525):
	.incbin "build/roms/vt525/23-011ED-00.z"
SYM(rom_vt525_end):
