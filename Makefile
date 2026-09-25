# Crossy Road for NumWorks
#   make            build output/crossyroad.nwa for the calculator
#   make sim        build output/crossyroad.nwb for the native Epsilon simulator
#   make host       build a desktop test harness (frame dumps)

NWLINK ?= npx --yes -- nwlink@1.0.0
CC_DEVICE = arm-none-eabi-gcc
BUILD = output
APP = crossyroad

EADK_INC := $(shell $(NWLINK) eadk-cflags-device 2>/dev/null | sed -e 's/.*"-I\([^"]*\)".*/\1/')

DEVICE_CFLAGS = -std=c11 -mthumb -mfloat-abi=hard -mcpu=cortex-m7 -mfpu=fpv5-sp-d16 -DPLATFORM_DEVICE=1 \
  -I$(EADK_INC) -Os -Wall -g0 -fno-common \
  -fno-asynchronous-unwind-tables -fno-unwind-tables -fsingle-precision-constant -fno-math-errno -ffreestanding -fno-tree-loop-distribute-patterns \
  -flto -fno-fat-lto-objects -fwhole-program -fvisibility=internal -fno-reorder-blocks -fno-schedule-insns2 -fno-ipa-sra \
  -fno-caller-saves -fno-thread-jumps -fno-optimize-sibling-calls
DEVICE_LDFLAGS = -Wl,--relocatable -nostartfiles -nostdlib -Wl,-e,main \
  -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level -Wl,--gc-sections \
  -flinker-output=nolto-rel -Tsrc/nwa.ld

.PHONY: all sim host clean
all: $(BUILD)/$(APP).nwa

$(BUILD):
	mkdir -p $@

# Icon: LZ4 block of RGB565 pixels in .rodata.eadk_app_icon (what nwlink png-icon-o
# makes, with a much tighter encoder).
$(BUILD)/icon.o: assets/icon.png tools/png2nwi.py | $(BUILD)
	mkdir -p $(BUILD)/icon && python3 tools/png2nwi.py $< $(BUILD)/icon/input.dat
	cd $(BUILD)/icon && arm-none-eabi-objcopy --input-target binary --output-target elf32-littlearm \
	  --rename-section .data=.rodata.eadk_app_icon --redefine-sym _binary_input_dat_start=eadk_app_icon \
	  --strip-symbol _binary_input_dat_end --strip-symbol _binary_input_dat_size input.dat ../icon.o

# Constant tables: compiled once to read their bytes, then embedded compressed.
$(BUILD)/rom.inc: src/crossy.c tools/pack_rom.py | $(BUILD)
	$(CC_DEVICE) $(filter-out -flto -fno-fat-lto-objects -fwhole-program,$(DEVICE_CFLAGS)) -c $< -o $(BUILD)/rom.o
	python3 tools/pack_rom.py $(BUILD)/rom.o $@

$(BUILD)/$(APP).nwa: src/crossy.c src/nwa.ld $(BUILD)/icon.o $(BUILD)/rom.inc | $(BUILD)
	$(CC_DEVICE) $(DEVICE_CFLAGS) -DROM_PACKED='"rom.inc"' -I$(BUILD) $(DEVICE_LDFLAGS) src/crossy.c $(BUILD)/icon.o -lgcc -o $@.tmp
	python3 tools/nwa_min.py $@.tmp $@
	rm -f $@.tmp

sim: $(BUILD)/$(APP).nwb
$(BUILD)/$(APP).nwb: src/crossy.c | $(BUILD)
	gcc -std=c11 -O2 -fno-math-errno -fPIC -shared -DPLATFORM_SIM=1 -I$(EADK_INC) src/crossy.c -o $@ -Wl,--unresolved-symbols=ignore-all

host: $(BUILD)/host
$(BUILD)/host: src/crossy.c host/host.c | $(BUILD)
	gcc -std=c11 -O2 -g -Wall -fno-math-errno -DPLATFORM_DEVICE=1 -DHOST=1 -Dmain=game_main -I$(EADK_INC) -c src/crossy.c -o $(BUILD)/crossy_host.o
	gcc -std=c11 -O2 -g -DPLATFORM_DEVICE=1 -I$(EADK_INC) host/host.c $(BUILD)/crossy_host.o -o $@ -lm

clean:
	rm -rf $(BUILD)
