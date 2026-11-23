# petsdsim - petSD+ (unveraenderte NODISKEMU-Firmware, ATmega1284P) in simavr
# am simlink-IEEE-Bus, mit Bedienfeld (psdpanel).
#
#   make                      mit dem mitgelieferten simavr 1.8 (../simavr)
#   make SIMAVR=system        mit dem simavr der Distribution (pkg-config)
#   make SIMLINK_DIR=...      simlink aus VICE-LRE-SIMLINK (src/simlink)
#
# Windows: in der MSYS2-Shell UCRT64 genauso (make CC=gcc), Pakete
#   mingw-w64-ucrt-x86_64-{toolchain,libelf,avr-gcc,avr-binutils,avr-libc,rust,pkgconf}, make.
# Voraussetzungen:
#   - simavr-Quellen: git submodule update --init sim/simavr
#     (zum Bauen braucht simavr die avr-libc-Header, also die AVR-Toolchain:
#      Linux: gcc-avr avr-libc, macOS: brew tap osx-cross/avr && brew install avr-gcc)
#   - libelf (Linux: libelf-dev, macOS: brew install libelf)
#   - vorher in ../psdpanel: cargo build --release
# Ohne Submodule wird automatisch das simavr der Distribution genommen
# (Linux: libsimavr-dev). petsdsim gleicht die bekannten Fehler von
# simavr 1.6 selbst aus, 1.8 ist aber vorzuziehen.
SIMLINK_DIR ?= ../vice-lre-simlink/vice/src/simlink
PANEL       ?= ../psdpanel/target/release/libpsdpanel.a
SIMAVR_DIR  ?= ../simavr/simavr
UNAME       := $(shell uname -s)
BREW        := $(shell brew --prefix 2>/dev/null)

ifeq ($(SIMAVR),)
  SIMAVR := $(if $(wildcard $(SIMAVR_DIR)/Makefile),bundled,system)
endif

ifeq ($(SIMAVR),bundled)
  SIMAVR_OBJ    := $(SIMAVR_DIR)/obj-$(shell $(CC) -dumpmachine)
  SIMAVR_LIB    := $(SIMAVR_OBJ)/libsimavr.a
  SIMAVR_CFLAGS := -I$(SIMAVR_DIR)/sim -I$(SIMAVR_DIR)
  SIMAVR_LIBS   := $(SIMAVR_LIB)
else
  SIMAVR_LIB    :=
  SIMAVR_CFLAGS := $(shell pkg-config --cflags simavr 2>/dev/null)
  SIMAVR_LIBS   := $(shell pkg-config --libs simavr 2>/dev/null || echo -lsimavr)
  ifeq ($(SIMAVR_CFLAGS),)
    # Homebrew legt die Header unter include/simavr ab
    SIMAVR_CFLAGS := -I/usr/include/simavr $(if $(BREW),-I$(BREW)/include/simavr)
  endif
endif

CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -DPSDPANEL -I$(SIMLINK_DIR) $(SIMAVR_CFLAGS) \
          $(if $(BREW),-I$(BREW)/include)
LIBS    = $(PANEL) $(SIMAVR_LIBS) $(if $(BREW),-L$(BREW)/lib) -lelf -lpthread -lm
ifeq ($(UNAME),Linux)
  LIBS += -ldl -lrt
endif
ifeq ($(UNAME),Darwin)
  LIBS += -framework CoreFoundation -framework Security
endif
# Windows (MSYS2 MINGW64): statisch, damit petsdsim.exe ohne DLLs laeuft;
# dazu die Systembibliotheken, die die Rust-Bibliothek psdpanel braucht
ifneq ($(filter MINGW% MSYS%,$(UNAME)),)
  LIBS += -lws2_32 -luserenv -lbcrypt -lntdll -ladvapi32 -lkernel32 -ldbghelp -lsynchronization
  LDFLAGS += -static
endif

petsdsim: petsdsim.c buscap.c buscap.h $(SIMLINK_DIR)/simlink.c $(PANEL) $(SIMAVR_LIB)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ petsdsim.c buscap.c $(SIMLINK_DIR)/simlink.c $(LIBS)

# simavr seriell bauen: parallel faellt die Kern-Konfiguration aus der Reihe.
# simavr baut mit -Werror; neuere GCC melden in simavr-Quellen Warnungen,
# die keine Fehler sind - diese Arten daher nicht als Fehler werten.
SIMAVR_NOERR = -Wno-error=array-bounds -Wno-error=stringop-overflow \
               -Wno-error=stringop-truncation -Wno-error=format-truncation \
               -Wno-error=dangling-pointer -Wno-error=use-after-free
$(SIMAVR_OBJ)/libsimavr.a:
	CFLAGS="$(SIMAVR_NOERR)" $(MAKE) -j1 -C $(SIMAVR_DIR) libsimavr

$(PANEL):
	cd ../psdpanel && cargo build --release

clean:
	rm -f petsdsim

distclean: clean
	-$(MAKE) -C $(SIMAVR_DIR) clean

.PHONY: clean distclean
