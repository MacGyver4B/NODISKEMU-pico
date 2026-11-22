# petsdsim - petSD+ (unveraenderte NODISKEMU-Firmware, ATmega1284P) in simavr
# am simlink-IEEE-Bus, mit Bedienfeld (psdpanel).
#
#   make                      Linux (simavr-Paket) oder macOS (Homebrew)
#   make SIMLINK_DIR=...      simlink aus VICE-LRE-SIMLINK (src/simlink)
#
# Voraussetzungen: simavr (Linux: libsimavr-dev, macOS: brew tap osx-cross/avr
# && brew install simavr), libelf, und vorher in ../psdpanel: cargo build --release
SIMLINK_DIR ?= ../vice-lre-simlink/vice/src/simlink
PANEL       ?= ../psdpanel/target/release/libpsdpanel.a
UNAME       := $(shell uname -s)

SIMAVR_CFLAGS := $(shell pkg-config --cflags simavr 2>/dev/null || pkg-config --cflags simavr-avr 2>/dev/null)
SIMAVR_LIBS   := $(shell pkg-config --libs simavr 2>/dev/null || echo -lsimavr)
ifeq ($(SIMAVR_CFLAGS),)
  # Homebrew legt die Header unter include/simavr ab
  BREW := $(shell brew --prefix 2>/dev/null)
  SIMAVR_CFLAGS := -I/usr/include/simavr $(if $(BREW),-I$(BREW)/include -I$(BREW)/include/simavr -L$(BREW)/lib)
endif

CFLAGS ?= -O2
CFLAGS += -std=c11 -Wall -DPSDPANEL -I$(SIMLINK_DIR) $(SIMAVR_CFLAGS)
LIBS    = $(PANEL) $(SIMAVR_LIBS) -lelf -lpthread -lm
ifeq ($(UNAME),Linux)
  LIBS += -ldl -lrt
endif
ifeq ($(UNAME),Darwin)
  LIBS += -framework CoreFoundation -framework Security
endif

petsdsim: petsdsim.c $(SIMLINK_DIR)/simlink.c $(PANEL)
	$(CC) $(CFLAGS) -o $@ petsdsim.c $(SIMLINK_DIR)/simlink.c $(LIBS)

$(PANEL):
	cd ../psdpanel && cargo build --release

clean:
	rm -f petsdsim

.PHONY: clean
