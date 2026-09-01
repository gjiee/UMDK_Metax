# On the GPU node:  make && ./gpu_ws_probe
# Do not LD_PRELOAD libgdr_geforce_hook.so

NVCC ?= nvcc
CXXFLAGS ?= -O2 -std=c++14

gpu_ws_probe: gpu_ws_probe.cu
	$(NVCC) $(CXXFLAGS) $< -o $@

clean:
	rm -f gpu_ws_probe
