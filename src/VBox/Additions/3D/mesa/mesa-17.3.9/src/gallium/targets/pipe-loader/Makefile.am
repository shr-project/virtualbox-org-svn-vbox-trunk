# Copyright © 2012 Intel Corporation
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
# HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
# WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.

include $(top_srcdir)/src/gallium/Automake.inc

AM_CPPFLAGS = \
	$(GALLIUM_CFLAGS) \
	-I$(top_srcdir)/include \
	-I$(top_builddir)/src/gallium/drivers \
	-I$(top_srcdir)/src/gallium/drivers \
	-I$(top_srcdir)/src/gallium/winsys \
	-I$(top_builddir)/src/util \
	$(GALLIUM_PIPE_LOADER_DEFINES) \
	$(LIBDRM_CFLAGS) \
	$(VISIBILITY_CFLAGS) \
	-DGALLIUM_RBUG \
	-DGALLIUM_TRACE

pipedir = $(libdir)/gallium-pipe
pipe_LTLIBRARIES =

PIPE_LIBS =

if NEED_GALLIUM_VL
PIPE_LIBS += \
	$(top_builddir)/src/gallium/auxiliary/libgalliumvl.la
else
PIPE_LIBS += \
	$(top_builddir)/src/gallium/auxiliary/libgalliumvl_stub.la
endif

if NEED_GALLIUM_VL_WINSYS
PIPE_LIBS+= \
	$(top_builddir)/src/gallium/auxiliary/libgalliumvlwinsys.la
endif

PIPE_LIBS += \
	$(top_builddir)/src/gallium/auxiliary/libgallium.la \
	$(top_builddir)/src/compiler/nir/libnir.la \
	$(top_builddir)/src/util/libmesautil.la \
	$(top_builddir)/src/gallium/drivers/rbug/librbug.la \
	$(top_builddir)/src/gallium/drivers/trace/libtrace.la \
	$(GALLIUM_COMMON_LIB_DEPS)

AM_LDFLAGS = \
	-module \
	-no-undefined \
	-avoid-version \
	$(GC_SECTIONS) \
	$(LD_NO_UNDEFINED)

if HAVE_LD_VERSION_SCRIPT
AM_LDFLAGS += \
	-Wl,--version-script=$(top_srcdir)/src/gallium/targets/pipe-loader/pipe.sym
endif

if HAVE_GALLIUM_LLVM
PIPE_LIBS += $(LLVM_LIBS)
AM_LDFLAGS += $(LLVM_LDFLAGS)
endif

if HAVE_GALLIUM_I915
pipe_LTLIBRARIES += pipe_i915.la

pipe_i915_la_SOURCES = pipe_i915.c
nodist_EXTRA_pipe_i915_la_SOURCES = dummy.cpp
pipe_i915_la_LIBADD = \
	$(PIPE_LIBS) \
	$(top_builddir)/src/gallium/winsys/i915/drm/libi915drm.la \
	$(top_builddir)/src/gallium/drivers/i915/libi915.la \
	$(LIBDRM_LIBS) \
	$(I915_LIBS)

endif

if HAVE_GALLIUM_NOUVEAU
pipe_LTLIBRARIES += pipe_nouveau.la

pipe_nouveau_la_SOURCES = pipe_nouveau.c
nodist_EXTRA_pipe_nouveau_la_SOURCES = dummy.cpp
pipe_nouveau_la_LIBADD = \
	$(PIPE_LIBS) \
	$(top_builddir)/src/gallium/winsys/nouveau/drm/libnouveaudrm.la \
	$(top_builddir)/src/gallium/drivers/nouveau/libnouveau.la \
	$(LIBDRM_LIBS) \
	$(NOUVEAU_LIBS)

endif

if HAVE_GALLIUM_R300
pipe_LTLIBRARIES += pipe_r300.la

pipe_r300_la_SOURCES = pipe_r300.c
nodist_EXTRA_pipe_r300_la_SOURCES = dummy.cpp
pipe_r300_la_LIBADD = \
	$(PIPE_LIBS) \
	$(top_builddir)/src/gallium/winsys/radeon/drm/libradeonwinsys.la \
	$(top_builddir)/src/gallium/drivers/r300/libr300.la \
	$(LIBDRM_LIBS) \
	$(RADEON_LIBS)

endif

if HAVE_GALLIUM_R600
pipe_LTLIBRARIES += pipe_r600.la

pipe_r600_la_SOURCES = pipe_r600.c
nodist_EXTRA_pipe_r600_la_SOURCES = dummy.cpp
pipe_r600_la_LIBADD = \
	$(PIPE_LIBS) \
	$(top_builddir)/src/gallium/winsys/radeon/drm/libradeonwinsys.la \
	$(top_builddir)/src/gallium/drivers/r600/libr600.la \
	$(LIBDRM_LIBS) \
	$(RADEON_LIBS) \
	$(LIBELF_LIBS)
endif

if HAVE_GALLIUM_RADEONSI
pipe_LTLIBRARIES += pipe_radeonsi.la

pipe_radeonsi_la_SOURCES = pipe_radeonsi.c
nodist_EXTRA_pipe_radeonsi_la_SOURCES = dummy.cpp
pipe_radeonsi_la_LIBADD = \
	$(PIPE_LIBS) \
	$(top_builddir)/src/gallium/winsys/radeon/drm/libradeonwinsys.la \
	$(top_builddir)/src/gallium/winsys/amdgpu/drm/libamdgpuwinsys.la \
	$(top_builddir)/src/gallium/drivers/radeon/libradeon.la \
	$(top_builddir)/src/gallium/drivers/radeonsi/libradeonsi.la \
	$(top_builddir)/src/amd/common/libamd_common.la \
	$(top_builddir)/src/util/libxmlconfig.la \
	$(LIBDRM_LIBS) \
	$(RADEON_LIBS) \
	$(AMDGPU_LIBS)

endif

if HAVE_GALLIUM_FREEDRENO
pipe_LTLIBRARIES += pipe_msm.la

pipe_msm_la_SOURCES = pipe_msm.c
nodist_EXTRA_pipe_msm_la_SOURCES = dummy.cpp
pipe_msm_la_LIBADD = \
	$(PIPE_LIBS) \
	$(top_builddir)/src/gallium/winsys/freedreno/drm/libfreedrenodrm.la \
	$(top_builddir)/src/gallium/drivers/freedreno/libfreedreno.la \
	$(LIBDRM_LIBS) \
	$(FREEDRENO_LIBS)

endif

if HAVE_GALLIUM_SVGA
pipe_LTLIBRARIES += pipe_vmwgfx.la

pipe_vmwgfx_la_SOURCES = pipe_vmwgfx.c
nodist_EXTRA_pipe_vmwgfx_la_SOURCES = dummy.cpp

pipe_vmwgfx_la_LIBADD = \
	$(PIPE_LIBS) \
	$(top_builddir)/src/gallium/winsys/svga/drm/libsvgadrm.la \
	$(top_builddir)/src/gallium/drivers/svga/libsvga.la \
	$(LIBDRM_LIBS)

endif

if HAVE_GALLIUM_SWRAST
pipe_LTLIBRARIES += pipe_swrast.la
pipe_swrast_la_SOURCES = pipe_swrast.c
nodist_EXTRA_pipe_swrast_la_SOURCES = dummy.cpp
pipe_swrast_la_LIBADD = $(PIPE_LIBS)

if HAVE_GALLIUM_SOFTPIPE
AM_CPPFLAGS += -DGALLIUM_SOFTPIPE

pipe_swrast_la_LIBADD += \
	$(top_builddir)/src/gallium/drivers/softpipe/libsoftpipe.la
endif

if HAVE_GALLIUM_LLVMPIPE
AM_CPPFLAGS += -DGALLIUM_LLVMPIPE

pipe_swrast_la_LIBADD += \
	$(top_builddir)/src/gallium/drivers/llvmpipe/libllvmpipe.la
endif

if HAVE_GALLIUM_SWR
AM_CPPFLAGS += -DGALLIUM_SWR

pipe_swrast_la_LIBADD += \
	$(top_builddir)/src/gallium/drivers/swr/libmesaswr.la
endif

pipe_swrast_la_LIBADD += \
	$(GALLIUM_PIPE_LOADER_WINSYS_LIBS)

endif

EXTRA_DIST = pipe.sym
