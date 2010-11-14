# Compiler Info ('g++-4.1 --version')
# g++-4.1 (GCC) 4.1.2
# Copyright (C) 2006 Free Software Foundation, Inc.
# This is free software; see the source for copying conditions.  There is NO
# warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# End Compiler Info Output
NDKDIR ?= /opt/Nuke6.1v3
MYCXX ?= g++-4.4
LINK ?= g++-4.4
CXXFLAGS ?= -g -c -DUSE_GLEW -I$(NDKDIR)/include -fPIC -msse -D__STDC_CONSTANT_MACROS 
LINKFLAGS ?= -L$(NDKDIR) 
LIBS ?= -lDDImage -lavformat -lavcodec -lswscale -lavutil
LINKFLAGS += -shared
all: ffmpegWriter.so 
.PRECIOUS : %.os
%.os: %.cpp
	$(MYCXX) $(CXXFLAGS) -o $(@) $<
%.so: %.os
	$(LINK) $(LINKFLAGS) $(LIBS) -o $(@) $<
clean:
	rm -rf *.os ffmpegWriter.so 

