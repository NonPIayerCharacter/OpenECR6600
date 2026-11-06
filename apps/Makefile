.PHONY:all depend

include $(TOPDIR)/Make.defs


###########################################
#
#			directory path
#
###########################################
SUBDIRS := $(shell find $(CURDIR) -type d )
SUBDIRS	:= $(filter-out $(CURDIR),$(SUBDIRS))

OUTPUTDIR_OBJS:=$(OUTPUTDIR)/objs/apps
OUTPUTDIR_DEPS:=$(OUTPUTDIR)/deps/apps
OUTPUTDIR_LIB:=$(OUTPUTDIR)/libs


###########################################
#
#	C source and header files 	
#
###########################################
ASRCS	:= 
CSRCS	:=
INCPATHS:=
MAKEDEFS:=

$(foreach dir, $(SUBDIRS), $(eval $(call MAKEDEFS_template,$(dir))))

CFLAGS := $(foreach incpath, $(INCPATHS), -I "$(incpath)") $(CFLAGS)

###########################################
#
#		target
#
###########################################

LIBTARGET = libapps.a
LIBAIVLLM = libps_ai.a
LIBCOZE   = libps_coze.a
LIBGX     = libps_gx.a
LIBQTH    = libqthsdk.a
ASRCS	:= $(wildcard $(foreach SRC, $(ASRCS), $(addsuffix /$(SRC), $(subst :, ,$(VPATH)))))
CSRCS	:= $(wildcard $(foreach SRC, $(CSRCS), $(addsuffix /$(SRC), $(subst :, ,$(VPATH)))))
AOBJS = $(addprefix $(OUTPUTDIR_OBJS)/,$(ASRCS:.S=.o))
COBJS = $(addprefix $(OUTPUTDIR_OBJS)/,$(CSRCS:.c=.o))
OBJS = $(AOBJS) $(COBJS)

DEPS += $(addprefix $(OUTPUTDIR_DEPS)/,$(CSRCS:.c=.deps))
all: $(OUTPUTDIR_LIB)/$(LIBTARGET) $(OUTPUTDIR_LIB)/$(LIBAIVLLM) $(OUTPUTDIR_LIB)/$(LIBCOZE) $(OUTPUTDIR_LIB)/$(LIBGX) $(OUTPUTDIR_LIB)/$(LIBQTH)

###########################################
#
#		generate legacy-style ai lib
#
###########################################
ifneq ($(strip $(AI_CSRCS)),)
AI_CSRCS   := $(foreach SRC, $(AI_CSRCS), $(addsuffix /$(SRC), $(subst :, ,$(AI_VPATH))))
AI_COBJS   := $(addprefix $(OUTPUTDIR_OBJS)/,$(AI_CSRCS:.c=.o))
HDEPS+=$(OBJS:.o=.d)
-include $(HDEPS)
DEPS += $(addprefix $(OUTPUTDIR_DEPS)/,$(AI_CSRCS:.c=.deps))

$(AI_COBJS): $(OUTPUTDIR_OBJS)/%.o: %.c
	$(Q) -mkdir -p $(dir $@)
	$(call COMPILE, $<, $@)

$(OUTPUTDIR_LIB)/$(LIBAIVLLM): $(AI_COBJS) 
	$(Q) -mkdir -p $(dir $@)
	$(call ARCHIVE, $@, $(AI_COBJS))

else
$(OUTPUTDIR_LIB)/$(LIBAIVLLM):
endif

###########################################
#
#		generate coze lib
#
###########################################
coze_builtin = $(shell if [ "$(CONFIG_AI_COZE)" = "y" ]; then echo "y"; else echo "n"; fi;)
ifeq ($(coze_builtin), y)
ifneq ($(strip $(COZE_CSRCS)),)
COZE_CSRCS   := $(foreach SRC, $(COZE_CSRCS), $(addsuffix /$(SRC), $(subst :, ,$(COZE_VPATH))))
COZE_COBJS   := $(addprefix $(OUTPUTDIR_OBJS)/,$(COZE_CSRCS:.c=.o))
HDEPS+=$(COZE_COBJS:.o=.d)
-include $(HDEPS)
DEPS += $(addprefix $(OUTPUTDIR_DEPS)/,$(COZE_CSRCS:.c=.deps))

$(COZE_COBJS): $(OUTPUTDIR_OBJS)/%.o: %.c
	$(Q) -mkdir -p $(dir $@)
	$(call COMPILE, $<, $@)

$(OUTPUTDIR_LIB)/$(LIBCOZE): $(COZE_COBJS)
	$(Q) -mkdir -p $(dir $@)
	$(call ARCHIVE, $@, $(COZE_COBJS))

else
$(OUTPUTDIR_LIB)/$(LIBCOZE):
endif
else
$(OUTPUTDIR_LIB)/$(LIBCOZE):
endif

###########################################
#
#		generate gx lib
#
###########################################
gx_builtin = $(shell if [ -f $(CURDIR)/ai_sys/gx8006/Make.defs ]; then echo "y"; else echo "n"; fi;)
ifeq ($(gx_builtin), y)
GX_CSRCS   := $(foreach SRC, $(GX_CSRCS), $(addsuffix /$(SRC), $(subst :, ,$(GX_VPATH))))
GX_COBJS   := $(addprefix $(OUTPUTDIR_OBJS)/,$(GX_CSRCS:.c=.o))
HDEPS+=$(OBJS:.o=.d)
-include $(HDEPS)
DEPS += $(addprefix $(OUTPUTDIR_DEPS)/,$(GX_CSRCS:.c=.deps))

$(GX_COBJS): $(OUTPUTDIR_OBJS)/%.o: %.c
	$(Q) -mkdir -p $(dir $@)
	$(call COMPILE, $<, $@)

$(OUTPUTDIR_LIB)/$(LIBGX): $(GX_COBJS) 
	$(Q) -mkdir -p $(dir $@)
	$(call ARCHIVE, $@, $(GX_COBJS))

else
$(OUTPUTDIR_LIB)/$(LIBGX):
endif

###########################################
#
#		generate qthsdk lib
#
###########################################
ifeq ($(CONFIG_YIYUAN_CLOUD), y)
QTH_CSRCS   := $(foreach SRC, $(QTH_CSRCS), $(addsuffix /$(SRC), $(subst :, ,$(QTH_VPATH))))
QTH_OUTPUTDIR_OBJS := $(OUTPUTDIR_OBJS)/qthsdk-$(QTHSDK_VARIANT)
QTH_COBJS   := $(addprefix $(QTH_OUTPUTDIR_OBJS)/,$(QTH_CSRCS:.c=.o))
HDEPS += $(QTH_COBJS:.o=.d)
-include $(HDEPS)
DEPS += $(addprefix $(OUTPUTDIR_DEPS)/,$(QTH_CSRCS:.c=.deps))

$(QTH_COBJS): $(QTH_OUTPUTDIR_OBJS)/%.o: %.c
	$(Q) -mkdir -p $(dir $@)
	$(call COMPILE, $<, $@)

.PHONY: qthsdk_force
qthsdk_force:

$(OUTPUTDIR_LIB)/$(LIBQTH): qthsdk_force $(QTHSDK_PREBUILT_LIB) $(QTH_COBJS) 
	$(Q) -mkdir -p $(dir $@)
	$(Q) cp $(QTHSDK_PREBUILT_LIB) $@
	$(call ARCHIVE, $@, $(QTH_COBJS))
else
$(OUTPUTDIR_LIB)/$(LIBQTH):
endif

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

###########################################
#
#		generate deps
#
###########################################
depend: $(DEPS)
$(DEPS):$(OUTPUTDIR_DEPS)/%.deps: %.c
	$(Q) -mkdir -p $(dir $@)
	$(call DEPEND, $<, $@)

