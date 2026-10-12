DM_ROOT ?= $(HOME)/dm-work
# 贴图子块单测（秒级）：make run。真数据样本 make samples 取到 $(SAMPLES)（构建缓存，不进仓）
# Texture sub-module unit tests (seconds): make run. Real-data samples: make samples fetches them into $(SAMPLES) (build cache, not in the repo)
SAMPLES ?= $(DM_ROOT)/build/cache/dm/tex_samples
PY ?= $(DM_ROOT)/build/cache/dm/venv/bin/python
CFLAGS = -O2 -g -Wall -Wextra
t_tex: t_tex.c ../nvn_deswizzle.c ../nvn_deswizzle.h ../../nvn/nvn_tex.c ../../nvn/nvn_tex.h
	clang $(CFLAGS) -o $@ t_tex.c ../nvn_deswizzle.c ../../nvn/nvn_tex.c -lm
t_fmt_mtl: t_fmt_mtl.m ../nvn_fmt_mtl.c ../nvn_fmt_mtl.h
	clang $(CFLAGS) -fobjc-arc -o $@ t_fmt_mtl.m ../nvn_fmt_mtl.c -framework Metal -framework Foundation
samples:
	$(PY) fetch_samples.py $(SAMPLES)
run: t_tex t_fmt_mtl
	./t_fmt_mtl && ./t_tex $(SAMPLES)
.PHONY: samples run
