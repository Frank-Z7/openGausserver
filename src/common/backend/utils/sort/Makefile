#-------------------------------------------------------------------------
#
# Makefile--
#    Makefile for utils/sort
#
# IDENTIFICATION
#    src/common/backend/utils/sort/Makefile
#
#-------------------------------------------------------------------------

subdir = src/common/backend/utils/sort
top_builddir = ../../../../..
include $(top_builddir)/src/Makefile.global

override CPPFLAGS := -I. -I$(srcdir) $(CPPFLAGS) -I$(LZ4_INCLUDE_PATH)

ifneq "$(MAKECMDGOALS)" "clean"
  ifneq "$(MAKECMDGOALS)" "distclean"
    ifneq "$(shell which g++ |grep hutaf_llt |wc -l)" "1"
      -include $(DEPEND)
    endif
  endif
endif
OBJS = logtape.o sortsupport.o tuplesort.o tuplestore.o batchsort.o batchstore.o rowstore.o

include $(top_srcdir)/src/gausskernel/common.mk
