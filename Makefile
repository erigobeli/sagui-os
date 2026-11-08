# Makefile do SaguiOS
#
# Alvos principais:
#   make            -> gera build/saguios.img (imagem para pendrive)
#   make vdi        -> gera também build/saguios.vdi (VirtualBox)
#   make test       -> roda todos os testes automáticos no QEMU
#   make rodar      -> abre o sistema no QEMU (precisa de janela; na VPS use os testes)
#   make entrega FASE=N -> copia .img, .vdi e SHA-256 para entrega/faseN/
#   make vazio      -> alvo vazio (critério da H01)
#   make limpar     -> apaga build/

# O GCC do sistema em modo 32 bits e freestanding faz o papel do compilador
# cruzado (ver docs/decisoes.md, D001).
CC      := gcc
AS      := nasm
LD      := ld
PY      := python3

CFLAGS  := -m32 -march=i686 -std=gnu11 -O2 -g \
           -ffreestanding -fno-builtin -nostdlib -fno-pic -fno-pie \
           -fno-stack-protector -fno-asynchronous-unwind-tables \
           -mgeneral-regs-only -fno-omit-frame-pointer \
           -Wall -Wextra -Werror=implicit-function-declaration \
           -finput-charset=UTF-8 -fexec-charset=ISO-8859-1 \
           -DSAGUI_KERNEL -Ikernel/inc
LDFLAGS := -m elf_i386 -nostdlib --no-dynamic-linker -z noexecstack --no-warn-rwx-segments

B       := build

KERNEL_C   := $(shell find kernel -name '*.c' | sort)
KERNEL_ASM := $(shell find kernel -name '*.asm' | sort)
KERNEL_OBJ := $(patsubst %.c,$(B)/%.o,$(KERNEL_C)) $(patsubst %.asm,$(B)/%.asm.o,$(KERNEL_ASM)) \
              $(B)/gerado/fonte8x16.o
KERNEL_HDR := $(shell find kernel -name '*.h')
ROOTFS     := $(shell find rootfs -type f | sort)

# Programas de usuário: libsagui + um .c por programa em userland/programas/
# (vão para /bin) e em userland/testes/ (vão para /testes).
UCFLAGS := -m32 -march=i686 -std=gnu11 -O2 \
           -ffreestanding -fno-builtin -nostdlib -fno-pic -fno-pie \
           -fno-stack-protector -fno-asynchronous-unwind-tables -mgeneral-regs-only \
           -ffunction-sections -fdata-sections \
           -Wall -Wextra -Werror=implicit-function-declaration \
           -finput-charset=UTF-8 -fexec-charset=ISO-8859-1 \
           -Iuserland/inc -idirafter kernel/inc
ULDFLAGS := -m elf_i386 -nostdlib -T userland/programa.ld --gc-sections -z max-page-size=4096 \
            -z noexecstack --no-warn-rwx-segments
LIBSAGUI_OBJ := $(B)/userland/fonte8x16.o \
                $(patsubst userland/libsagui/%.c,$(B)/userland/libsagui/%.o,$(wildcard userland/libsagui/*.c)) \
                $(patsubst userland/libsagui/%.asm,$(B)/userland/libsagui/%.asm.o,$(wildcard userland/libsagui/*.asm))
USER_HDR  := $(wildcard userland/inc/*.h) kernel/inc/abi.h
PROGRAMAS := $(patsubst userland/programas/%.c,$(B)/userland/bin/%,$(wildcard userland/programas/*.c))
# O Sagui 3D (B4) tem vários arquivos, numa pasta própria.
SAGUI3D_OBJ := $(patsubst userland/sagui3d/%.c,$(B)/userland/sagui3d/%.o,$(wildcard userland/sagui3d/*.c))
PROGRAMAS += $(B)/userland/bin/sagui3d
PROG_TESTE := $(patsubst userland/testes/%.c,$(B)/userland/testes/%,$(wildcard userland/testes/*.c))

.PHONY: all vazio vdi test rodar entrega limpar dashboard

all: $(B)/saguios.img

vazio:
	@echo "SaguiOS: alvo vazio ok"

# ---------- carregador de boot ----------
$(B)/boot/mbr.bin: boot/mbr.asm boot/cabecalho.inc
	@mkdir -p $(dir $@)
	$(AS) -f bin -I boot/ -o $@ $<

$(B)/boot/estagio2.bin: boot/estagio2.asm $(wildcard boot/*.inc)
	@mkdir -p $(dir $@)
	$(AS) -f bin -I boot/ -o $@ $<

# ---------- kernel ----------
$(B)/kernel/%.o: kernel/%.c $(KERNEL_HDR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(B)/kernel/%.asm.o: kernel/%.asm
	@mkdir -p $(dir $@)
	$(AS) -f elf32 -I kernel/arch/ -o $@ $<

# A fonte é desenhada em texto (tools/fonte/) e vira C no build.
$(B)/gerado/fonte8x16.c: tools/fonte/sagui8x16.txt tools/mkfonte.py
	@mkdir -p $(dir $@)
	$(PY) tools/mkfonte.py $< $@

$(B)/gerado/fonte8x16.o: $(B)/gerado/fonte8x16.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Os ícones são descritos em tools/mkarte.py e entram no ramdisk.
$(B)/gerado/icones.bin: tools/mkarte.py
	@mkdir -p $(dir $@)
	$(PY) tools/mkarte.py $@

$(B)/kernel.elf: $(KERNEL_OBJ) kernel/ligacao.ld
	$(LD) $(LDFLAGS) -T kernel/ligacao.ld -o $@ $(KERNEL_OBJ)

$(B)/kernel.bin: $(B)/kernel.elf
	objcopy -O binary $< $@

# ---------- programas de usuário ----------
$(B)/userland/%.o: userland/%.c $(USER_HDR)
	@mkdir -p $(dir $@)
	$(CC) $(UCFLAGS) -c -o $@ $<

$(B)/userland/fonte8x16.o: $(B)/gerado/fonte8x16.c
	@mkdir -p $(dir $@)
	$(CC) $(UCFLAGS) -c -o $@ $<

$(B)/userland/libsagui/%.asm.o: userland/libsagui/%.asm
	@mkdir -p $(dir $@)
	$(AS) -f elf32 -o $@ $<

# Programas que usam ponto flutuante.
$(B)/userland/libsagui/aac.o: UCFLAGS := $(filter-out -mgeneral-regs-only,$(UCFLAGS)) -msse2 -mstackrealign -mfpmath=sse -fno-math-errno
$(B)/userland/libsagui/fonte.o: UCFLAGS := $(filter-out -mgeneral-regs-only,$(UCFLAGS)) -msse2 -mstackrealign -mfpmath=sse -fno-math-errno
$(B)/userland/testes/fpu.o: UCFLAGS := $(filter-out -mgeneral-regs-only,$(UCFLAGS)) -msse2 -mstackrealign -mfpmath=387 -fno-tree-vectorize

$(B)/userland/libsagui/js.o: userland/libsagui/js_base.h userland/libsagui/js_nativo.h

$(B)/userland/bin/%: $(B)/userland/programas/%.o $(LIBSAGUI_OBJ) userland/programa.ld
	@mkdir -p $(dir $@)
	$(LD) $(ULDFLAGS) -o $@.elf $< $(LIBSAGUI_OBJ)
	objcopy --strip-all $@.elf $@

$(B)/userland/sagui3d/%.o: userland/sagui3d/%.c userland/sagui3d/jogo.h $(USER_HDR)
	@mkdir -p $(dir $@)
	$(CC) $(UCFLAGS) -c -o $@ $<

$(B)/userland/bin/sagui3d: $(SAGUI3D_OBJ) $(LIBSAGUI_OBJ) userland/programa.ld
	@mkdir -p $(dir $@)
	$(LD) $(ULDFLAGS) -o $@.elf $(SAGUI3D_OBJ) $(LIBSAGUI_OBJ)
	objcopy --strip-all $@.elf $@

$(B)/userland/testes/%: $(B)/userland/testes/%.o $(LIBSAGUI_OBJ) userland/programa.ld
	@mkdir -p $(dir $@)
	$(LD) $(ULDFLAGS) -o $@.elf $< $(LIBSAGUI_OBJ)
	objcopy --strip-all $@.elf $@

# ---------- ramdisk (SGFS) ----------
# O ramdisk é montado a partir de uma cópia do rootfs/ com os programas em
# /bin e os programas de teste em /testes.
$(B)/ramdisk.sgfs: $(ROOTFS) $(PROGRAMAS) $(PROG_TESTE) $(B)/gerado/icones.bin tools/mksgfs.py tools/lz.py
	rm -rf $(B)/rootfs
	cp -r rootfs $(B)/rootfs
	cp $(B)/gerado/icones.bin $(B)/rootfs/arte/icones.bin
	$(if $(PROGRAMAS),cp $(PROGRAMAS) $(B)/rootfs/bin/)
	$(if $(PROG_TESTE),mkdir -p $(B)/rootfs/testes && cp $(PROG_TESTE) $(B)/rootfs/testes/)
	$(PY) tools/mksgfs.py $(B)/rootfs $@

# ---------- imagem final ----------
$(B)/saguios.img: $(B)/boot/mbr.bin $(B)/boot/estagio2.bin $(B)/kernel.bin $(B)/ramdisk.sgfs tools/mkimagem.py
	$(PY) tools/mkimagem.py --mbr $(B)/boot/mbr.bin --estagio2 $(B)/boot/estagio2.bin \
	    --kernel $(B)/kernel.bin --kernel-elf $(B)/kernel.elf --ramdisk $(B)/ramdisk.sgfs \
	    --saida $@ --vesa
	cd $(B) && sha256sum saguios.img > saguios.img.sha256

$(B)/saguios.vdi: $(B)/saguios.img
	rm -f $@ $(B)/vdi.raw
	cp $< $(B)/vdi.raw
	# disco virtual com tamanho múltiplo de 1 MB (ver D004)
	truncate -s %1M $(B)/vdi.raw
	qemu-img convert -f raw -O vdi $(B)/vdi.raw $@
	rm -f $(B)/vdi.raw
	cd $(B) && sha256sum saguios.vdi > saguios.vdi.sha256

vdi: $(B)/saguios.vdi

test: $(B)/saguios.img
	$(PY) tests/rodar_todos.py

rodar: $(B)/saguios.img
	qemu-system-i386 -m 512M -drive format=raw,file=$< -serial stdio

entrega: $(B)/saguios.img $(B)/saguios.vdi
	@test -n "$(FASE)" || (echo "use: make entrega FASE=N" && false)
	mkdir -p entrega/fase$(FASE)
	cp $(B)/saguios.img $(B)/saguios.vdi entrega/fase$(FASE)/
	cd entrega/fase$(FASE) && sha256sum saguios.img saguios.vdi > SHA256SUMS.txt
	@cat entrega/fase$(FASE)/SHA256SUMS.txt

dashboard:
	$(PY) tools/status.py atualizar

limpar:
	rm -rf $(B)
