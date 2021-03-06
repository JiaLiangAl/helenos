#
# Copyright (c) 2013 Vojtech Horky
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# - Redistributions of source code must retain the above copyright
#   notice, this list of conditions and the following disclaimer.
# - Redistributions in binary form must reproduce the above copyright
#   notice, this list of conditions and the following disclaimer in the
#   documentation and/or other materials provided with the distribution.
# - The name of the author may not be used to endorse or promote products
#   derived from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
# OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
# IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
# NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#

USPACE_PREFIX = ../..
ROOT_PATH = $(USPACE_PREFIX)/..

CONFIG_MAKEFILE = $(ROOT_PATH)/Makefile.config

LIBRARY = libmath
SOVERSION = 0.0

EXTRA_CFLAGS += -Iarch/$(UARCH)/include

-include $(CONFIG_MAKEFILE)
-include arch/$(UARCH)/Makefile.inc

GENERIC_SOURCES = \
	generic/acos.c \
	generic/asin.c \
	generic/atan.c \
	generic/atan2.c \
	generic/ceil.c \
	generic/cosh.c \
	generic/exp.c \
	generic/fabs.c \
	generic/floor.c \
	generic/fmod.c \
	generic/frexp.c \
	generic/ldexp.c \
	generic/log.c \
	generic/log10.c \
	generic/log2.c \
	generic/modf.c \
	generic/pow.c \
	generic/sinh.c \
	generic/sqrt.c \
	generic/tan.c \
	generic/tanh.c \
	generic/trig.c \
	generic/trunc.c

SOURCES = \
	$(GENERIC_SOURCES) \
	$(ARCH_SOURCES)

include $(USPACE_PREFIX)/Makefile.common
