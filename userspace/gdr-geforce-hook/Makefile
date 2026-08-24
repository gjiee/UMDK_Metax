# GeForce / 4090D GDRCopy userspace hook
# Build on the target (AArch64) machine:
#   make
# Optional: CUDA_HOME=/usr/local/cuda make

CC      ?= gcc
CFLAGS  ?= -O2 -fPIC -Wall -Wextra -Wno-unused-parameter
LDFLAGS ?= -shared -ldl -pthread

all: libgdr_geforce_hook.so

libgdr_geforce_hook.so: gdr_geforce_hook.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f libgdr_geforce_hook.so

.PHONY: all clean
