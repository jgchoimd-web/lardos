NASM      := nasm
CC        := gcc
LD        := ld
QEMU      := qemu-system-x86_64

PROJECT   := lardos

BOOT_DIR  := boot
KERNEL_DIR:= kernel
INCLUDE   := include
SCRIPTS_DIR := scripts
HOST_TOOL_DIR := ../build/tools

# Host tools (run on build machine, no Python)
MKLDLL    := $(HOST_TOOL_DIR)/mkldll
MKARDX    := $(HOST_TOOL_DIR)/mkardx
MKLFS     := $(HOST_TOOL_DIR)/mklfs
MK_SSAV   := $(HOST_TOOL_DIR)/mk_default_ssav
MKEXE     := $(HOST_TOOL_DIR)/mkexe
MKIMG     := $(HOST_TOOL_DIR)/mkimg
MKISO     := $(HOST_TOOL_DIR)/mkiso
BOSLA     := $(HOST_TOOL_DIR)/bosla
SEEDC     := $(HOST_TOOL_DIR)/seedc
LIL       := $(HOST_TOOL_DIR)/lil
BIN2INC   := $(HOST_TOOL_DIR)/bin2inc

RELEASE_BASE_VERSION := v1.59.0b
RELEASE_HW ?= universal
RELEASE_HW_LIST := universal seabios ami vbox usb realpc
RELEASE_HW_UPPER := $(shell printf '%s' '$(RELEASE_HW)' | tr '[:lower:]' '[:upper:]' | tr '-' '_')
RELEASE_VERSION := $(if $(filter universal,$(RELEASE_HW)),$(RELEASE_BASE_VERSION),$(RELEASE_BASE_VERSION)-$(RELEASE_HW))
RELEASE_NAME := lardos-$(RELEASE_VERSION)
ISO_VOLUME_BASE := LARDOS_V1_59_0B
ISO_VOLUME_ID := $(if $(filter universal,$(RELEASE_HW)),$(ISO_VOLUME_BASE),$(ISO_VOLUME_BASE)_$(RELEASE_HW_UPPER))
RELEASE_DIR := release/$(RELEASE_VERSION)
ISO_BOOTIMG_SECTOR := 23
ISO_STAGE2_LBA := 93
ISO_KERNEL_LBA := 97

ifneq ($(filter $(RELEASE_HW),$(RELEASE_HW_LIST)),$(RELEASE_HW))
$(error Unknown RELEASE_HW '$(RELEASE_HW)' (expected one of: $(RELEASE_HW_LIST)))
endif

LANG_EX   := lang/examples
LAFILLO_ELF := $(LANG_EX)/lafillo_demo.elf
LAFILLO_BOSX:= $(LANG_EX)/lafillo_demo.bosx
LAFILLO_INC := $(KERNEL_DIR)/lafillo_demo_data.inc
RELEASES_DOC := RELEASES.lardd
RELEASES_INC := $(KERNEL_DIR)/releases_lardd.inc

STAGE2_SECTORS := 4
KERNEL_LBA := 5
BOOT_BIN  := $(BOOT_DIR)/stage1.bin
STAGE2_BIN:= $(BOOT_DIR)/stage2.bin
ISO_MBR_BIN:= $(BOOT_DIR)/iso_mbr.bin
KERNEL_ELF:= $(KERNEL_DIR)/kernel.elf
KERNEL_EXE:= $(KERNEL_DIR)/kernel.bosx
RAW_IMAGE := os-image.bin
IMG_IMAGE := $(RELEASE_NAME).img
ISO_IMAGE := $(RELEASE_NAME).iso
RELEASE_IMG := $(RELEASE_DIR)/$(RELEASE_NAME).img
RELEASE_ISO := $(RELEASE_DIR)/$(RELEASE_NAME).iso

include deps.mk

KERNEL_LIB_OBJS := $(addprefix $(KERNEL_DIR)/,$(addsuffix .o,$(KERNEL_LIBS)))
LDLL_INC_DEPS   := $(addprefix $(KERNEL_DIR)/ldll_lib,$(addsuffix .inc,$(LDLL_LIBS)))

HOST_CFLAGS := -std=c11 -O2 -Wall
CFLAGS    := -std=gnu11 -ffreestanding -O2 -Wall -Wextra -m64 -mno-red-zone -mcmodel=kernel -fno-pic -fno-stack-protector -fno-exceptions -fno-asynchronous-unwind-tables -nostdlib -fno-builtin -I$(INCLUDE) -include stdio.h -DLARDOS_HARDWARE_PROFILE=\"$(RELEASE_HW)\"

LDFLAGS   := -T linker.ld -nostdlib -m elf_x86_64

all: $(RAW_IMAGE)

$(BOOT_BIN): $(BOOT_DIR)/stage1.s
	$(NASM) -f bin -D STAGE2_SECTORS=$(STAGE2_SECTORS) $< -o $@

$(STAGE2_BIN): $(BOOT_DIR)/boot.s
	$(NASM) -f bin -D KERNEL_LBA=$(KERNEL_LBA) $< -o $@
	@test $$(wc -c < $@) -le $$(($(STAGE2_SECTORS) * 512))

$(ISO_MBR_BIN): $(BOOT_DIR)/iso_mbr.s
	$(NASM) -f bin -D ISO_STAGE2_LBA=$(ISO_STAGE2_LBA) -D ISO_KERNEL_LBA=$(ISO_KERNEL_LBA) -D STAGE2_SECTORS=$(STAGE2_SECTORS) $< -o $@

$(KERNEL_ELF): $(KERNEL_DIR)/entry64.o $(KERNEL_DIR)/isr64.o $(KERNEL_DIR)/kernel64.o $(KERNEL_DIR)/smp.o $(KERNEL_DIR)/ap_trampoline.o $(KERNEL_DIR)/aux_kernel.o $(KERNEL_DIR)/pci.o $(KERNEL_DIR)/rtl8139.o $(KERNEL_DIR)/net.o $(KERNEL_DIR)/oslink.o $(KERNEL_DIR)/taskprio.o $(KERNEL_DIR)/awake.o $(KERNEL_DIR)/bootprof.o $(KERNEL_DIR)/crashlog.o $(KERNEL_DIR)/lpack.o $(KERNEL_DIR)/screencheck.o $(KERNEL_DIR)/lardkit.o $(KERNEL_DIR)/lguilib.o $(KERNEL_DIR)/guioverlay.o $(KERNEL_DIR)/lassist.o $(KERNEL_DIR)/lard_tls.o $(KERNEL_DIR)/rtc.o $(KERNEL_DIR)/lardtime.o $(KERNEL_DIR)/vmmon.o $(KERNEL_DIR)/storage.o $(KERNEL_DIR)/string.o $(KERNEL_DIR)/stdlib_min.o $(KERNEL_DIR)/lard_stdio.o $(KERNEL_DIR)/lard_printf.o $(KERNEL_DIR)/fs.o $(KERNEL_DIR)/bmp.o $(KERNEL_DIR)/lard_inflate.o $(KERNEL_DIR)/lafillo.o $(KERNEL_DIR)/lard_doc.o $(KERNEL_DIR)/ldi.o $(KERNEL_DIR)/lda.o $(KERNEL_DIR)/lar.o $(KERNEL_DIR)/lvcs.o $(KERNEL_DIR)/lcontainer.o $(KERNEL_DIR)/ldll.o $(KERNEL_DIR)/img_glyph.o $(KERNEL_DIR)/drfl.o $(KERNEL_DIR)/post.o $(KERNEL_DIR)/ssav.o $(KERNEL_DIR)/lss.o $(KERNEL_DIR)/lfs.o $(KERNEL_DIR)/lsh.o $(KERNEL_DIR)/larsh.o $(KERNEL_DIR)/kr_basic.o $(KERNEL_LIB_OBJS) $(KERNEL_DIR)/lardx_load.o $(KERNEL_DIR)/gui.o $(KERNEL_DIR)/ps2.o $(KERNEL_DIR)/idt64.o $(KERNEL_DIR)/panic.o $(KERNEL_DIR)/mmu.o $(KERNEL_DIR)/cpumode.o $(KERNEL_DIR)/mode_switch.o $(KERNEL_DIR)/mem.o $(KERNEL_DIR)/gc.o $(KERNEL_DIR)/gc_demo.o $(KERNEL_DIR)/lib3d.o $(KERNEL_DIR)/lib3d_demo.o $(KERNEL_DIR)/unicode.o $(KERNEL_DIR)/console.o $(KERNEL_DIR)/bosl_vm.o $(KERNEL_DIR)/bosl_jit.o $(KERNEL_DIR)/bosl_demo.o $(KERNEL_DIR)/bosl_asm.o $(KERNEL_DIR)/lil.o $(KERNEL_DIR)/lil_demo.o $(KERNEL_DIR)/lml.o $(KERNEL_DIR)/lml_demo.o $(KERNEL_DIR)/gasm_vm.o $(KERNEL_DIR)/gasm_demo.o $(KERNEL_DIR)/lafillo_vm.o $(KERNEL_DIR)/lafillo_demo.o $(KERNEL_DIR)/os_vm.o $(KERNEL_DIR)/os_demo.o $(KERNEL_DIR)/gdt64.o $(KERNEL_DIR)/gdt64_asm.o $(KERNEL_DIR)/lipc.o $(KERNEL_DIR)/syscall.o $(KERNEL_DIR)/usermode.o $(KERNEL_DIR)/usermode_asm.o
	$(LD) $(LDFLAGS) -o $@ $^

$(KERNEL_DIR)/lard_tls.o: $(KERNEL_DIR)/lard_tls.c $(KERNEL_DIR)/lard_tls_roots.inc $(INCLUDE)/lard_tls.h $(INCLUDE)/rtc.h
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/rtc.o: $(KERNEL_DIR)/rtc.c $(INCLUDE)/rtc.h
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/lardtime.o: $(KERNEL_DIR)/lardtime.c $(INCLUDE)/lardtime.h $(INCLUDE)/rtc.h
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/vmmon.o: $(KERNEL_DIR)/vmmon.c $(INCLUDE)/vmmon.h
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/string.o: $(KERNEL_DIR)/string.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/stdlib_min.o: $(KERNEL_DIR)/stdlib_min.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/lard_stdio.o: $(KERNEL_DIR)/lard_stdio.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/lard_printf.o: $(KERNEL_DIR)/lard_printf.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/bmp.o: $(KERNEL_DIR)/bmp.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/img_glyph.o: $(KERNEL_DIR)/img_glyph.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/drfl.o: $(KERNEL_DIR)/drfl.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/ssav.o: $(KERNEL_DIR)/ssav.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/lss.o: $(KERNEL_DIR)/lss.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/lfs.o: $(KERNEL_DIR)/lfs.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/lsh.o: $(KERNEL_DIR)/lsh.c $(INCLUDE)/lardtime.h
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/larsh.o: $(KERNEL_DIR)/larsh.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/post.o: $(KERNEL_DIR)/post.c $(INCLUDE)/lardtime.h
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/%.o: $(KERNEL_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/ldi.o: $(KERNEL_DIR)/ldi.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/lda.o: $(KERNEL_DIR)/lda.c
	$(CC) $(CFLAGS) -c $< -o $@

$(HOST_TOOL_DIR):
	mkdir -p $@

$(MKLDLL): $(SCRIPTS_DIR)/mkldll.c | $(HOST_TOOL_DIR)
	$(CC) $(HOST_CFLAGS) -o $@ $<

$(MKARDX): $(SCRIPTS_DIR)/mkardx.c | $(HOST_TOOL_DIR)
	$(CC) $(HOST_CFLAGS) -o $@ $<

$(BIN2INC): $(SCRIPTS_DIR)/bin2inc.c | $(HOST_TOOL_DIR)
	$(CC) $(HOST_CFLAGS) -o $@ $<

$(LAFILLO_ELF): $(LANG_EX)/lafillo_demo.s $(LANG_EX)/user.ld
	$(NASM) -f elf64 $(LANG_EX)/lafillo_demo.s -o $(LANG_EX)/lafillo_demo.o
	$(LD) -T $(LANG_EX)/user.ld -nostdlib -m elf_x86_64 $(LANG_EX)/lafillo_demo.o -o $@

$(LAFILLO_BOSX): $(LAFILLO_ELF) $(MKARDX)
	$(MKARDX) $(LAFILLO_ELF) $@ --user

$(LAFILLO_INC): $(LAFILLO_BOSX) $(BIN2INC)
	$(BIN2INC) $(LAFILLO_BOSX) $@ file_lafillo_demo_bosx

$(RELEASES_INC): $(RELEASES_DOC) $(BIN2INC)
	$(BIN2INC) $(RELEASES_DOC) $@ file_releases_lardd

$(EMBED_CA): $(SCRIPTS_DIR)/embed_ca_pem.c
	$(CC) $(HOST_CFLAGS) -o $@ $<

$(MKLFS): $(SCRIPTS_DIR)/mklfs.c | $(HOST_TOOL_DIR)
	$(CC) $(HOST_CFLAGS) -o $@ $<

$(MK_SSAV): $(SCRIPTS_DIR)/mk_default_ssav.c | $(HOST_TOOL_DIR)
	$(CC) $(HOST_CFLAGS) -o $@ $<

$(MKEXE): $(SCRIPTS_DIR)/mkexe.c | $(HOST_TOOL_DIR)
	$(CC) $(HOST_CFLAGS) -o $@ $<

$(MKIMG): $(SCRIPTS_DIR)/mkimg.c | $(HOST_TOOL_DIR)
	$(CC) $(HOST_CFLAGS) -o $@ $<

$(MKISO): $(SCRIPTS_DIR)/mkiso.c | $(HOST_TOOL_DIR)
	$(CC) $(HOST_CFLAGS) -o $@ $<

$(BOSLA): lang/bosla.c | $(HOST_TOOL_DIR)
	$(CC) $(HOST_CFLAGS) -o $@ $<

$(SEEDC): lang/seed/seedc.c | $(HOST_TOOL_DIR)
	$(CC) $(HOST_CFLAGS) -o $@ $<

$(LIL): tools/lil.c | $(HOST_TOOL_DIR)
	$(CC) $(HOST_CFLAGS) -o $@ $<

define ldll_rule
$(KERNEL_DIR)/ldll_lib$(1).inc: $(MKLDLL)
	$(MKLDLL) $(1) -o $(KERNEL_DIR)/lib$(1).ldll --embed $$@
endef
$(foreach lib,$(LDLL_LIBS),$(eval $(call ldll_rule,$(lib))))

$(KERNEL_DIR)/fs_ldll.inc: deps.mk
	@echo "/* Auto-generated from deps.mk LDLL_LIBS - do not edit */" > $@
	@for lib in $(LDLL_LIBS); do echo "#include \"ldll_lib$$lib.inc\"" >> $@; done

$(KERNEL_DIR)/fs_ldll_entries.inc: deps.mk
	@echo "/* Auto-generated from deps.mk LDLL_LIBS - do not edit */" > $@
	@for lib in $(LDLL_LIBS); do \
	  up=$$(echo "$$lib" | tr 'a-z' 'A-Z'); \
	  echo "  { \"lib$$lib.ldll\",  file_lib$$lib""_ldll,  LDLL_LIB$$up""_SIZE }," >> $@; \
	done

$(KERNEL_DIR)/fs.o: $(KERNEL_DIR)/fs.c $(LDLL_INC_DEPS) $(KERNEL_DIR)/fs_ldll.inc $(KERNEL_DIR)/fs_ldll_entries.inc $(LAFILLO_INC) $(RELEASES_INC)
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/ldll.o: $(KERNEL_DIR)/ldll.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/gdt64.o: $(KERNEL_DIR)/gdt64.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/gdt64_asm.o: $(KERNEL_DIR)/gdt64_asm.s
	$(NASM) -f elf64 $< -o $@

$(KERNEL_DIR)/mode_switch.o: $(KERNEL_DIR)/mode_switch.s
	$(NASM) -f elf64 $< -o $@

$(KERNEL_DIR)/syscall.o: $(KERNEL_DIR)/syscall.c $(INCLUDE)/lardtime.h
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/usermode.o: $(KERNEL_DIR)/usermode.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/usermode_asm.o: $(KERNEL_DIR)/usermode_asm.s
	$(NASM) -f elf64 $< -o $@

$(KERNEL_EXE): $(KERNEL_ELF) $(MKARDX)
	$(MKARDX) $< $@

$(KERNEL_DIR)/entry64.o: $(KERNEL_DIR)/entry64.s
	$(NASM) -f elf64 $< -o $@

$(KERNEL_DIR)/isr64.o: $(KERNEL_DIR)/isr64.s
	$(NASM) -f elf64 $< -o $@

$(KERNEL_DIR)/ap_trampoline.o: $(KERNEL_DIR)/ap_trampoline.s
	$(NASM) -f elf64 $< -o $@

$(KERNEL_DIR)/aux_kernel.o: $(KERNEL_DIR)/aux_kernel.s
	$(NASM) -f elf64 $< -o $@

$(KERNEL_DIR)/smp.o: $(KERNEL_DIR)/smp.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/kernel64.o: $(KERNEL_DIR)/kernel64.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/pci.o: $(KERNEL_DIR)/pci.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/rtl8139.o: $(KERNEL_DIR)/rtl8139.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/net.o: $(KERNEL_DIR)/net.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/gui.o: $(KERNEL_DIR)/gui.c $(INCLUDE)/bootinfo.h $(INCLUDE)/gui.h
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/ps2.o: $(KERNEL_DIR)/ps2.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/idt64.o: $(KERNEL_DIR)/idt64.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/panic.o: $(KERNEL_DIR)/panic.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/mmu.o: $(KERNEL_DIR)/mmu.c $(INCLUDE)/bootinfo.h $(INCLUDE)/mmu.h
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/video.o: $(KERNEL_DIR)/video.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/unicode.o: $(KERNEL_DIR)/unicode.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/console.o: $(KERNEL_DIR)/console.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/mem.o: $(KERNEL_DIR)/mem.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/gc.o: $(KERNEL_DIR)/gc.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/gc_demo.o: $(KERNEL_DIR)/gc_demo.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/lib3d.o: $(KERNEL_DIR)/lib3d.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/lib3d_demo.o: $(KERNEL_DIR)/lib3d_demo.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/bosl_vm.o: $(KERNEL_DIR)/bosl_vm.c $(INCLUDE)/lardtime.h
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/bosl_jit.o: $(KERNEL_DIR)/bosl_jit.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/bosl_demo.o: $(KERNEL_DIR)/bosl_demo.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/bosl_asm.o: $(KERNEL_DIR)/bosl_asm.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/lil.o: $(KERNEL_DIR)/lil.c $(INCLUDE)/lardtime.h
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/lil_demo.o: $(KERNEL_DIR)/lil_demo.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/lml.o: $(KERNEL_DIR)/lml.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/lml_demo.o: $(KERNEL_DIR)/lml_demo.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/gasm_vm.o: $(KERNEL_DIR)/gasm_vm.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/gasm_demo.o: $(KERNEL_DIR)/gasm_demo.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/lafillo_vm.o: $(KERNEL_DIR)/lafillo_vm.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/lafillo_demo.o: $(KERNEL_DIR)/lafillo_demo.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/os_vm.o: $(KERNEL_DIR)/os_vm.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/os_demo.o: $(KERNEL_DIR)/os_demo.c
	$(CC) $(CFLAGS) -c $< -o $@

$(RAW_IMAGE): $(BOOT_BIN) $(STAGE2_BIN) $(KERNEL_EXE) $(MKIMG)
	$(MKIMG) -o $@ -b $(BOOT_BIN) -s $(STAGE2_BIN) -k $(KERNEL_EXE)

$(IMG_IMAGE): $(RAW_IMAGE)
	cp $< $@

$(ISO_IMAGE): $(IMG_IMAGE) $(MKISO) $(ISO_MBR_BIN)
	$(MKISO) -o $@ -i $(IMG_IMAGE) -m $(ISO_MBR_BIN) -v $(ISO_VOLUME_ID)

$(RELEASE_DIR):
	mkdir -p $@

$(RELEASE_IMG): $(IMG_IMAGE) | $(RELEASE_DIR)
	cp $< $@

$(RELEASE_ISO): $(ISO_IMAGE) | $(RELEASE_DIR)
	cp $< $@

.PHONY: run run-iso release release-hardware release-all-hardware clean

release: $(RELEASE_IMG) $(RELEASE_ISO)

release-%:
	$(MAKE) clean
	$(MAKE) release RELEASE_HW=$*

release-hardware release-all-hardware:
	@set -e; for hw in $(RELEASE_HW_LIST); do \
	  echo "==> release hardware profile $$hw"; \
	  $(MAKE) clean; \
	  $(MAKE) release RELEASE_HW=$$hw; \
	done

run: $(RAW_IMAGE)
	$(QEMU) -drive format=raw,file=$(RAW_IMAGE) -netdev user,id=n1 -device rtl8139,netdev=n1 -smp 3

run-iso: $(ISO_IMAGE)
	$(QEMU) -cdrom $(ISO_IMAGE) -netdev user,id=n1 -device rtl8139,netdev=n1 -smp 3

clean:
	rm -f $(BOOT_BIN) $(STAGE2_BIN) $(ISO_MBR_BIN) $(KERNEL_ELF) $(KERNEL_EXE) $(RAW_IMAGE) $(IMG_IMAGE) $(ISO_IMAGE) \
	      $(KERNEL_DIR)/*.o $(KERNEL_DIR)/lafillo_demo_data.inc $(RELEASES_INC) \
	      $(LAFILLO_ELF) $(LAFILLO_BOSX) $(LANG_EX)/lafillo_demo.o \
	      $(MKLDLL) $(MKARDX) $(MKLFS) $(MK_SSAV) $(MKEXE) $(MKIMG) $(MKISO) $(BIN2INC) $(BOSLA) $(SEEDC) $(LIL)
