# ximgtest - host test program for the X-IMG controller module (src/ximg.c)
#
#   make                  simlink from ../vice-lre-simlink/vice/src/simlink
#   make SIMLINK_DIR=...
SIMLINK_DIR ?= ../vice-lre-simlink/vice/src/simlink
CFLAGS ?= -O2 -g -Wall -Wextra -std=c11
CPPFLAGS += -I$(SIMLINK_DIR) -I../../src
LDLIBS += -lpthread
ifneq ($(OS),Windows_NT)
  ifeq ($(shell uname -s),Linux)
    LDLIBS += -lrt
  endif
endif

ximgtest: ximgtest.c fast4040.h drive4040.h ../../src/ximg.c ../../src/ximg.h $(SIMLINK_DIR)/simlink.c $(SIMLINK_DIR)/simlink.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ ximgtest.c ../../src/ximg.c $(SIMLINK_DIR)/simlink.c $(LDLIBS)

clean:
	rm -f ximgtest

.PHONY: clean

fast4040.h: fast4040.py
	python3 fast4040.py 2 > $@

drive4040.h: drive4040.py
	python3 drive4040.py > $@
