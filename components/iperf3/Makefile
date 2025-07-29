.PHONY:all

include $(TOPDIR)/Make.defs


###########################################
#
#			directory path
#
###########################################
COMPONENTS_SUB_DIR_NAME := $(notdir ${shell echo $(CURDIR) | sed -e 's/ /\\ /g'})
SUBDIRS := $(shell find $(CURDIR) -type d )
SUBDIRS := $(CURDIR) $(filter-out $(CURDIR),$(SUBDIRS))

OUTPUTDIR_OBJS:=$(OUTPUTDIR)/objs/components/$(COMPONENTS_SUB_DIR_NAME)
OUTPUTDIR_DEPS:=$(OUTPUTDIR)/deps/components/$(COMPONENTS_SUB_DIR_NAME)
OUTPUTDIR_LIB:=$(OUTPUTDIR)/libs


###########################################
#
#	C source and header files
#
###########################################
ASRCS	:=
CSRCS	:=
CPPSRCS	:=
INCPATHS:=
MAKEDEFS:=

$(foreach dir, $(SUBDIRS), $(eval $(call MAKEDEFS_template,$(dir))))

CFLAGS := $(foreach incpath, $(INCPATHS), -I "$(incpath)") $(foreach incpath, $(TOP_INCPATHS), -I "$(incpath)") $(CFLAGS)
CPPFLAGS := $(foreach incpath, $(INCPATHS), -I "$(incpath)") $(foreach incpath, $(TOP_INCPATHS), -I "$(incpath)") $(CPPFLAGS)
VPATH += :.

###########################################
#
#		target
#
###########################################

LIBTARGET = libcomponents_$(COMPONENTS_SUB_DIR_NAME).a
AOBJS = $(addprefix $(OUTPUTDIR_OBJS)/,$(ASRCS:.S=.o))
COBJS = $(addprefix $(OUTPUTDIR_OBJS)/,$(CSRCS:.c=.o))
CPPOBJS = $(addprefix $(OUTPUTDIR_OBJS)/,$(CPPSRCS:.cpp=.o))
OBJS = $(AOBJS) $(COBJS) $(CPPOBJS)

all: $(OUTPUTDIR_LIB)/$(LIBTARGET)

###########################################
#
#		generate lib
#
###########################################

$(OUTPUTDIR_LIB)/$(LIBTARGET): $(OBJS)
	$(Q) -mkdir -p $(dir $@)
	$(call ARCHIVE, $@, $(OBJS))


###########################################
#
#		generate objs
#
###########################################
$(AOBJS): $(OUTPUTDIR_OBJS)/%.o: %.S
	$(Q) -mkdir -p $(dir $@)
	$(call ASSEMBLE, $<, $@)

$(COBJS): $(OUTPUTDIR_OBJS)/%.o: %.c
	$(Q) -mkdir -p $(dir $@)
	$(call COMPILE, $<, $@)

$(CPPOBJS): $(OUTPUTDIR_OBJS)/%.o: %.cpp
	$(Q) -mkdir -p $(dir $@)
	$(call COMPILE_CPP, $<, $@)
