NASM      := nasm
CC        := gcc
LD        := ld
QEMU      := qemu-system-x86_64

PROJECT   := lardos

BOOT_DIR  := boot
KERNEL_DIR:= kernel
INCLUDE   := include
SCRIPTS_DIR := scripts

# Host tools (run on build machine, no Python)
MKLDLL    := $(SCRIPTS_DIR)/mkldll
MKARDX    := $(SCRIPTS_DIR)/mkardx
EMBED_CA  := $(SCRIPTS_DIR)/embed_ca_pem
MKLFS     := $(SCRIPTS_DIR)/mklfs
MK_SSAV   := $(SCRIPTS_DIR)/mk_default_ssav
MKEXE     := $(SCRIPTS_DIR)/mkexe
MKIMG     := $(SCRIPTS_DIR)/mkimg
BOSLA     := lang/bosla
SEEDC     := lang/seed/seedc
LIL       := tools/lil
HTTPS_BR  := $(SCRIPTS_DIR)/host_https_bridge
BIN2INC   := $(SCRIPTS_DIR)/bin2inc

LANG_EX   := lang/examples
LAFILLO_ELF := $(LANG_EX)/lafillo_demo.elf
LAFILLO_BOSX:= $(LANG_EX)/lafillo_demo.bosx
LAFILLO_INC := $(KERNEL_DIR)/lafillo_demo_data.inc

BOOT_BIN  := $(BOOT_DIR)/boot.bin
KERNEL_ELF:= $(KERNEL_DIR)/kernel.elf
KERNEL_EXE:= $(KERNEL_DIR)/kernel.bosx
ISO_IMAGE := os-image.bin

include deps.mk

KERNEL_LIB_OBJS := $(addprefix $(KERNEL_DIR)/,$(addsuffix .o,$(KERNEL_LIBS)))
LDLL_INC_DEPS   := $(addprefix $(KERNEL_DIR)/ldll_lib,$(addsuffix .inc,$(LDLL_LIBS)))

MBEDTLS_DIR := third_party/mbedtls
MBEDTLS_SRCS := $(filter-out $(MBEDTLS_DIR)/library/net_sockets.c,$(wildcard $(MBEDTLS_DIR)/library/*.c))
MBEDTLS_OBJS := $(patsubst $(MBEDTLS_DIR)/library/%.c,$(KERNEL_DIR)/mbedtls_%.o,$(MBEDTLS_SRCS))

CFLAGS    := -ffreestanding -O2 -Wall -Wextra -m64 -mno-red-zone -mcmodel=kernel -fno-pic -fno-stack-protector -fno-exceptions -fno-asynchronous-unwind-tables -nostdlib -fno-builtin -I$(INCLUDE) -DMBEDTLS_USER_CONFIG_FILE=\"lard_mbedtls_user.h\" -I$(MBEDTLS_DIR)/include -include stdio.h

LDFLAGS   := -T linker.ld -nostdlib -m elf_x86_64

all: $(ISO_IMAGE)

$(BOOT_BIN): $(BOOT_DIR)/boot.s
	$(NASM) -f bin $< -o $@

$(KERNEL_ELF): $(KERNEL_DIR)/entry64.o $(KERNEL_DIR)/isr64.o $(KERNEL_DIR)/kernel64.o $(KERNEL_DIR)/smp.o $(KERNEL_DIR)/ap_trampoline.o $(KERNEL_DIR)/aux_kernel.o $(KERNEL_DIR)/pci.o $(KERNEL_DIR)/rtl8139.o $(KERNEL_DIR)/net.o $(MBEDTLS_OBJS) $(KERNEL_DIR)/mbedtls_lard.o $(KERNEL_DIR)/ca_bundle.o $(KERNEL_DIR)/rtc.o $(KERNEL_DIR)/string.o $(KERNEL_DIR)/stdlib_min.o $(KERNEL_DIR)/lard_stdio.o $(KERNEL_DIR)/printf_mpaland.o $(KERNEL_DIR)/fs.o $(KERNEL_DIR)/bmp.o $(KERNEL_DIR)/lard_inflate.o $(KERNEL_DIR)/lafillo.o $(KERNEL_DIR)/ldi.o $(KERNEL_DIR)/lda.o $(KERNEL_DIR)/ldll.o $(KERNEL_DIR)/img_glyph.o $(KERNEL_DIR)/drfl.o $(KERNEL_DIR)/ssav.o $(KERNEL_DIR)/lss.o $(KERNEL_DIR)/lfs.o $(KERNEL_DIR)/lsh.o $(KERNEL_DIR)/larsh.o $(KERNEL_DIR)/kr_basic.o $(KERNEL_LIB_OBJS) $(KERNEL_DIR)/lardx_load.o $(KERNEL_DIR)/gui.o $(KERNEL_DIR)/ps2.o $(KERNEL_DIR)/idt64.o $(KERNEL_DIR)/panic.o $(KERNEL_DIR)/mmu.o $(KERNEL_DIR)/mem.o $(KERNEL_DIR)/gc.o $(KERNEL_DIR)/gc_demo.o $(KERNEL_DIR)/lib3d.o $(KERNEL_DIR)/lib3d_demo.o $(KERNEL_DIR)/unicode.o $(KERNEL_DIR)/console.o $(KERNEL_DIR)/bosl_vm.o $(KERNEL_DIR)/bosl_jit.o $(KERNEL_DIR)/bosl_demo.o $(KERNEL_DIR)/bosl_asm.o $(KERNEL_DIR)/lil.o $(KERNEL_DIR)/lil_demo.o $(KERNEL_DIR)/lml.o $(KERNEL_DIR)/lml_demo.o $(KERNEL_DIR)/gasm_vm.o $(KERNEL_DIR)/gasm_demo.o $(KERNEL_DIR)/lafillo_vm.o $(KERNEL_DIR)/lafillo_demo.o $(KERNEL_DIR)/os_vm.o $(KERNEL_DIR)/os_demo.o $(KERNEL_DIR)/gdt64.o $(KERNEL_DIR)/gdt64_asm.o $(KERNEL_DIR)/lipc.o $(KERNEL_DIR)/syscall.o $(KERNEL_DIR)/usermode.o $(KERNEL_DIR)/usermode_asm.o
	$(LD) $(LDFLAGS) -o $@ $^

$(KERNEL_DIR)/mbedtls_%.o: $(MBEDTLS_DIR)/library/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/mbedtls_lard.o: $(KERNEL_DIR)/mbedtls_lard.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/ca_bundle.o: $(KERNEL_DIR)/ca_bundle.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/rtc.o: $(KERNEL_DIR)/rtc.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/string.o: $(KERNEL_DIR)/string.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/stdlib_min.o: $(KERNEL_DIR)/stdlib_min.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/lard_stdio.o: $(KERNEL_DIR)/lard_stdio.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/printf_mpaland.o: $(KERNEL_DIR)/printf_mpaland.c
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

$(KERNEL_DIR)/lsh.o: $(KERNEL_DIR)/lsh.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/larsh.o: $(KERNEL_DIR)/larsh.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/%.o: $(KERNEL_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/ldi.o: $(KERNEL_DIR)/ldi.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/lda.o: $(KERNEL_DIR)/lda.c
	$(CC) $(CFLAGS) -c $< -o $@

$(MKLDLL): $(SCRIPTS_DIR)/mkldll.c
	$(CC) -O2 -Wall -o $@ $<

$(MKARDX): $(SCRIPTS_DIR)/mkardx.c
	$(CC) -O2 -Wall -o $@ $<

$(BIN2INC): $(SCRIPTS_DIR)/bin2inc.c
	$(CC) -O2 -Wall -o $@ $<

$(LAFILLO_ELF): $(LANG_EX)/lafillo_demo.s $(LANG_EX)/user.ld
	$(NASM) -f elf64 $(LANG_EX)/lafillo_demo.s -o $(LANG_EX)/lafillo_demo.o
	$(LD) -T $(LANG_EX)/user.ld -nostdlib -m elf_x86_64 $(LANG_EX)/lafillo_demo.o -o $@

$(LAFILLO_BOSX): $(LAFILLO_ELF) $(MKARDX)
	$(MKARDX) $(LAFILLO_ELF) $@ --user

$(LAFILLO_INC): $(LAFILLO_BOSX) $(BIN2INC)
	$(BIN2INC) $(LAFILLO_BOSX) $@ file_lafillo_demo_bosx

$(EMBED_CA): $(SCRIPTS_DIR)/embed_ca_pem.c
	$(CC) -O2 -Wall -o $@ $<

$(MKLFS): $(SCRIPTS_DIR)/mklfs.c
	$(CC) -O2 -Wall -o $@ $<

$(MK_SSAV): $(SCRIPTS_DIR)/mk_default_ssav.c
	$(CC) -O2 -Wall -o $@ $<

$(MKEXE): $(SCRIPTS_DIR)/mkexe.c
	$(CC) -O2 -Wall -o $@ $<

$(MKIMG): $(SCRIPTS_DIR)/mkimg.c
	$(CC) -O2 -Wall -o $@ $<

$(BOSLA): lang/bosla.c
	$(CC) -O2 -Wall -o $@ $<

$(SEEDC): lang/seed/seedc.c
	$(CC) -O2 -Wall -o $@ $<

$(LIL): tools/lil.c
	$(CC) -O2 -Wall -o $@ $<

$(HTTPS_BR): $(SCRIPTS_DIR)/host_https_bridge.c
	$(CC) -O2 -Wall -o $@ $<

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

$(KERNEL_DIR)/fs.o: $(KERNEL_DIR)/fs.c $(LDLL_INC_DEPS) $(KERNEL_DIR)/fs_ldll.inc $(KERNEL_DIR)/fs_ldll_entries.inc $(LAFILLO_INC)
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/ldll.o: $(KERNEL_DIR)/ldll.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/gdt64.o: $(KERNEL_DIR)/gdt64.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/gdt64_asm.o: $(KERNEL_DIR)/gdt64_asm.s
	$(NASM) -f elf64 $< -o $@

$(KERNEL_DIR)/syscall.o: $(KERNEL_DIR)/syscall.c
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

$(KERNEL_DIR)/gui.o: $(KERNEL_DIR)/gui.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/ps2.o: $(KERNEL_DIR)/ps2.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/idt64.o: $(KERNEL_DIR)/idt64.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/panic.o: $(KERNEL_DIR)/panic.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/mmu.o: $(KERNEL_DIR)/mmu.c
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

$(KERNEL_DIR)/gdt.o: $(KERNEL_DIR)/gdt.c $(KERNEL_DIR)/gdt.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/gdt_asm.o: $(KERNEL_DIR)/gdt_asm.s
	$(NASM) -f elf32 $< -o $@

$(KERNEL_DIR)/idt.o: $(KERNEL_DIR)/idt.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/idt_asm.o: $(KERNEL_DIR)/idt_asm.s
	$(NASM) -f elf32 $< -o $@

$(KERNEL_DIR)/isr.o: $(KERNEL_DIR)/isr.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/isr_asm.o: $(KERNEL_DIR)/isr_asm.s
	$(NASM) -f elf32 $< -o $@

$(KERNEL_DIR)/pic.o: $(KERNEL_DIR)/pic.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/irq.o: $(KERNEL_DIR)/irq.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/irq_asm.o: $(KERNEL_DIR)/irq_asm.s
	$(NASM) -f elf32 $< -o $@

$(KERNEL_DIR)/timer.o: $(KERNEL_DIR)/timer.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/keyboard.o: $(KERNEL_DIR)/keyboard.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/bosl_vm.o: $(KERNEL_DIR)/bosl_vm.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/bosl_jit.o: $(KERNEL_DIR)/bosl_jit.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/bosl_demo.o: $(KERNEL_DIR)/bosl_demo.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/bosl_asm.o: $(KERNEL_DIR)/bosl_asm.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_DIR)/lil.o: $(KERNEL_DIR)/lil.c
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

$(ISO_IMAGE): $(BOOT_BIN) $(KERNEL_EXE) $(MKIMG)
	$(MKIMG) -o $@ -b $(BOOT_BIN) -k $(KERNEL_EXE)

.PHONY: run clean

run: $(ISO_IMAGE)
	$(QEMU) -drive format=raw,file=$(ISO_IMAGE) -netdev user,id=n1 -device rtl8139,netdev=n1 -smp 3

clean:
	rm -f $(BOOT_BIN) $(KERNEL_ELF) $(KERNEL_EXE) $(ISO_IMAGE) \
	      $(KERNEL_DIR)/*.o $(KERNEL_DIR)/mbedtls_*.o $(KERNEL_DIR)/lafillo_demo_data.inc \
	      $(LAFILLO_ELF) $(LAFILLO_BOSX) $(LANG_EX)/lafillo_demo.o \
	      $(MKLDLL) $(MKARDX) $(EMBED_CA) $(MKLFS) $(MK_SSAV) $(MKEXE) $(MKIMG) $(BIN2INC) $(BOSLA) $(SEEDC) $(LIL) $(HTTPS_BR)

