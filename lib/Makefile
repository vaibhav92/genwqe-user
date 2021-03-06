#
# Copyright 2015, International Business Machines
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

include ../config.mk

instdir ?= /opt/genwqe
zlibver=1.2.8
libversion = $(VERSION)

CFLAGS += -fPIC -fno-strict-aliasing


### Accelerated libz implemenation (Accelerated Data Compression/ADC)
libname=libzADC
projA = $(libname).a $(libname)-$(libversion).so $(libname).so
src = wrapper.c hardware.c software.c
objs = __libzHW.o __libcard.o __libDDCB.o $(src:.c=.o)

### libzHW
src0 = libzHW.c inflate.c deflate.c
libname0 = libzHW
proj0 = $(libname0).a $(libname0)-$(libversion).so $(libname0).so
objs0 = $(src0:.c=.o)

### libcard
src1 = libcard.c
libname1 = libcard
proj1 = $(libname1).a $(libname1)-$(libversion).so $(libname1).so
objs1 = $(src1:.c=.o)

###libDDCB
src2 += libddcb.c ddcb_card.c

ifneq ($(CONFIG_LIBCXL_PATH),)
src2 += ddcb_capi.c
endif

libname2 = libDDCB
proj2 = $(libname2).a $(libname2)-$(libversion).so $(libname2).so
objs2 = $(src2:.c=.o)

projs += $(projA) $(proj0) $(proj1) $(proj2)

all: ..check_libcxl $(projs)

.PHONY: ..check_libcxl
..check_libcxl:
ifneq ($(CONFIG_LIBCXL_PATH),)
	@printf "\t[INFO]\tUsing $(libcxl_a).\n"
	@if [ ! -f $(libcxl_a) ]; then 					\
	  printf  "\t[ERROR]\t$(libcxl_a) not found!\n"; 		\
	  echo;								\
	  echo "  Please clone libcxl git before continuing. E.g.:";	\
	  echo "    git clone https://github.com/ibm-capi/libcxl";	\
	  echo;                                                         \
	  exit 1; \
	fi
else
	# @printf "\t[INFO]\tNot using libcxl.a.\n"
endif

### libzHW
__$(libname0).o: $(objs0)
	$(LD) $(XLDFLAGS) -r -o $@ $^

$(libname0).a: __$(libname0).o
	$(AR) rcs $(ARFLAGS) $@ $^

$(libname0).so: $(libname0)-$(libversion).so
	ln -sf $< $@

$(libname0)-$(libversion).so: __$(libname0).o
	$(CC) $(LDFLAGS) -shared -Wl,-soname,$@ -o $@ $^

### libcard
__$(libname1).o: $(objs1)
	$(LD) $(XLDFLAGS) -r -o $@ $^

$(libname1).a: __$(libname1).o
	$(AR) rcs $(ARFLAGS) $@ $^

$(libname1).so: $(libname1)-$(libversion).so
	ln -sf $< $@

$(libname1)-$(libversion).so: __$(libname1).o
	$(CC) $(LDFLAGS) -shared -Wl,-soname,$@ -o $@ $^

### libDDCB
__$(libname2).o: $(objs2) $(libcxl_a)
	$(LD) $(XLDFLAGS) -r -o $@ $^

$(libname2).a: __$(libname2).o
	$(AR) rcs $(ARFLAGS) $@ $^

$(libname2).so: $(libname2)-$(libversion).so
	ln -sf $< $@

$(libname2)-$(libversion).so: __$(libname2).o
	$(CC) $(LDFLAGS) -shared -Wl,-soname,$@ -o $@ $^

### libzADC
__$(libname).o: $(objs) $(libz_a)
	$(LD) $(XLDFLAGS) -r -o $@ $^

$(libname).a: __$(libname).o
	$(AR) rcs $@ $^

$(libname).so: $(libname)-$(libversion).so
	ln -sf $< $@

$(libname)-$(libversion).so: __$(libname).o
	$(CC) $(LDFLAGS) -shared -Wl,-soname,$@ \
		 -Wl,--version-script=libzADC.map -o $@ $^

# Produce z_ prefixed verson of software zlib. We need this when we
# want to include libz statially instead of using dlopen/dlsym to use
# it.
#
# Special version of libz.a which has z_ prefixed function
# names. Required for software zlib fallback in case of small buffers
# and accelerator unavailability.
#
libz_prefixed.o:

zlib_objs = $(CONFIG_LIBZ_PATH)/adler32.lo  \
	    $(CONFIG_LIBZ_PATH)/infback.lo  \
	    $(CONFIG_LIBZ_PATH)/compress.lo \
	    $(CONFIG_LIBZ_PATH)/gzclose.lo  \
	    $(CONFIG_LIBZ_PATH)/inffast.lo  \
	    $(CONFIG_LIBZ_PATH)/trees.lo    \
	    $(CONFIG_LIBZ_PATH)/crc32.lo    \
	    $(CONFIG_LIBZ_PATH)/gzlib.lo    \
	    $(CONFIG_LIBZ_PATH)/inflate.lo  \
	    $(CONFIG_LIBZ_PATH)/uncompr.lo  \
	    $(CONFIG_LIBZ_PATH)/deflate.lo  \
	    $(CONFIG_LIBZ_PATH)/gzread.lo   \
	    $(CONFIG_LIBZ_PATH)/inftrees.lo \
	    $(CONFIG_LIBZ_PATH)/zutil.lo    \
	    $(CONFIG_LIBZ_PATH)/gzwrite.lo

libz_prefixed.o: libz.o
	$(OBJCOPY) --prefix-symbols=z_ $< $@

libz.o: $(CONFIG_LIBZ_PATH)/libz.so
	$(LD) $(XLDFLAGS) -r -o $@ $(zlib_objs)

install: install_zlib

install_zlib: all
	mkdir -p $(instdir)/lib/genwqe
	cp -uv $(projA) $(instdir)/lib/genwqe/
	(cd $(instdir)/lib/genwqe/ && \
		ln -sf $(libname).a libz.a && \
		ln -sf $(libname)-$(libversion).so libz.so && \
		ln -sf $(libname)-$(libversion).so libz.so.1)
	mkdir -p $(instdir)/include/genwqe
	cp -uv ../include/zaddons.h $(instdir)/include/genwqe/

uninstall:
	@echo "removing $(instdir)/include/genwqe $(instdir)/lib/genwqe"
	$(RM) -r $(instdir)/include/genwqe $(instdir)/lib/genwqe

# general things
%.o: %.c
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $< -o $@
	$(CC) -MM $(CPPFLAGS) $(CFLAGS) $< > $*.d

clean distclean:
	$(RM) *.o *.d $(projs) *.so

-include $(objs:.o=.d) $(objs0:.o=.d) $(objs1:.o=.d) $(objs2:.o=.d)
