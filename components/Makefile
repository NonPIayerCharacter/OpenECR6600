.PHONY:all

include $(TOPDIR)/Make.defs


###########################################
#
#			directory path
#
###########################################
SUBDIRS := $(shell find $(CURDIR) -type d )
SUBDIRS	:= $(filter-out $(CURDIR),$(SUBDIRS))

OUTPUTDIR_OBJS:=$(OUTPUTDIR)/objs/components
OUTPUTDIR_DEPS:=$(OUTPUTDIR)/deps/components
OUTPUTDIR_LIB:=$(OUTPUTDIR)/libs


###########################################
#
#	C source and header files 	
#
###########################################
ASRCS	:=
CSRCS	:=
CPPSRCS	:=
CCSRCS	:=
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

LIBTARGET = libcomponents.a
ASRCS	:= $(wildcard $(foreach SRC, $(ASRCS), $(addsuffix /$(SRC), $(subst :, ,$(VPATH)))))
CSRCS	:= $(wildcard $(foreach SRC, $(CSRCS), $(addsuffix /$(SRC), $(subst :, ,$(VPATH)))))
CPPSRCS	:= $(wildcard $(foreach SRC, $(CPPSRCS), $(addsuffix /$(SRC), $(subst :, ,$(VPATH)))))
CCSRCS	:= $(wildcard $(foreach SRC, $(CCSRCS), $(addsuffix /$(SRC), $(subst :, ,$(VPATH)))))
AOBJS = $(addprefix $(OUTPUTDIR_OBJS)/,$(ASRCS:.S=.o))
COBJS = $(addprefix $(OUTPUTDIR_OBJS)/,$(CSRCS:.c=.o))
CPPOBJS = $(addprefix $(OUTPUTDIR_OBJS)/,$(CPPSRCS:.cpp=.o))
CCOBJS = $(addprefix $(OUTPUTDIR_OBJS)/,$(CCSRCS:.cc=.o))
OBJS = $(AOBJS) $(COBJS) $(CPPOBJS) $(CCOBJS)
HDEPS:=$(OBJS:.o=.d)
DEPS += $(addprefix $(OUTPUTDIR_DEPS)/,$(CSRCS:.c=.deps))
all: $(OUTPUTDIR_LIB)/$(LIBTARGET)

depend: $(DEPS)
-include $(HDEPS)
###########################################
#
#		generate lib
#
###########################################

$(OUTPUTDIR_LIB)/$(LIBTARGET): $(OBJS)
#	$(Q) echo "MAKEDEFS:"$(MAKEDEFS)
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

$(CCOBJS): $(OUTPUTDIR_OBJS)/%.o: %.cc
	$(Q) -mkdir -p $(dir $@)
	$(call COMPILE_CPP, $<, $@)
###########################################
#
#		generate deps
#
###########################################
$(DEPS):$(OUTPUTDIR_DEPS)/%.deps: %.c %.cpp %.cc
	$(Q) -mkdir -p $(dir $@)
	$(call DEPEND, $<, $@)



