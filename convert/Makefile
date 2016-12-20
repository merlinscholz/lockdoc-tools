# Author: A. Lochmann C 2010
# Based on a makefile found at: http://ubuntuforums.org/showthread.php?t=1204739
INCLUDE_PATHS=-I.
BUILD_PATH:=build
LD_TEXT = -e "LD\t$@"
LD_SO_TEXT = -e "LD SHARED\t$@"
CC_TEXT = -e "CC\t$<"
CXX_TEXT = -e "CXX\t$<"
DEB_TEXT= -e "DEB\t$<"

# FIND EVERY EXISTING DEPENDENCY FILE
ifneq (,$(wildcard $(BUILD_PATH)))
EXISTING_DEPS:=$(shell find $(BUILD_PATH) -name '*.d')
else
EXISTING_DEPS:=
endif

ifndef V
V = 0
endif
ifeq ($(V),0)
OUTPUT= @
else
OUTPUT=
endif

#***************************** ADD YOUR LISTING OF SOURCE FILES FOR EACH DIRECTORY HERE *****************************
# Example:
#<name>_DIR=<dir_name>
#<name>_SRC=<source files>
#<name>_OBJ=$(patsubst %.o,$(OBJ_PATH)/$(<name>_DIR)/%.o,$(<name>_SRC:%.cpp=%.o))
GZSTREAM_DIR=gzstream
GZSTREAM_SRC= gzstream.cc
GZSTREAM_OBJ=$(patsubst %.o,$(BUILD_PATH)/$(GZSTREAM_DIR)/%.o,$(GZSTREAM_SRC:%.cc=%.o))
INCLUDE_PATHS+= -I$(GZSTREAM_DIR)

DWARVES_DIR=dwarves
DWARVES_SRC= dwarves.c dutil.c  dwarf_loader.c  dwarves_fprintf.c  rbtree.c strings.c gobuffer.c
DWARVES_OBJ=$(patsubst %.o,$(BUILD_PATH)/$(DWARVES_DIR)/%.o,$(DWARVES_SRC:%.c=%.o))
INCLUDE_PATHS+= -I$(DWARVES_DIR)

MAIN_DIR=main
MAIN_SRC_CXX=convert.cc
MAIN_SRC_C=
MAIN_OBJ=$(patsubst %.o,$(BUILD_PATH)/$(MAIN_DIR)/%.o,$(MAIN_SRC_CXX:%.cc=%.o)) $(patsubst %.o,$(BUILD_PATH)/$(MAIN_DIR)/%.o,$(MAIN_SRC_C:%.c=%.o))
INCLUDE_PATHS+= -I$(MAIN_DIR)

#***************************** COMMANDS AND FLAGS *****************************
# COMPILER AND LINKER FLAGS
CC:=gcc
C_FLAGS := -O3 -Wall -Werror -c -g $(INCLUDE_PATHS)
CXX:=g++
CXX_FLAGS:= -O3 -Wall -Werror -c -g -std=c++11 $(INCLUDE_PATHS)
LD:=gcc
LD_FLAGS :=
LD_LIBS := -ldw -lelf -lz -lbfd

#*****************************			END SOURCE FILE				*****************************

# ADD YOUR NEW OBJ VAR HERE
# Example: $(<name>_OBJ)
OBJ = $(DWARVES_OBJ) $(GZSTREAM_OBJ) $(MAIN_OBJ)
CONVERT_BIN = $(BUILD_PATH)/convert

# ADD HERE YOUR NEW SOURCE DIRECTORY
# Example: $(<name>_DIR)
DIRS_ = $(DWARVES_DIR) $(MAIN_DIR)
DIRS = $(patsubst %,$(BUILD_PATH)/%,$(DIRS_))

#***************************** DO NOT EDIT BELOW THIS LINE EXCEPT YOU WANT TO ADD A TEST APPLICATION (OR YOU KNOW WHAT YOU'RE DOING :-) )***************************** 
DEP = $(subst .o,.d,$(OBJ))

all: git_version.h $(DEP) $(CONVERT_BIN) 

echo:
	@echo $(DEP)
	@echo $(OBJ)
	@echo $(DIRS)
	@echo $(INCLUDE_PATHS)

$(CONVERT_BIN): $(OBJ)
	@echo $(LD_TEXT)
	$(OUTPUT)$(CXX) $^ $(LD_FLAGS)  $(LD_LIBS) -o $@

# Every object file depends on its source and dependency file
$(BUILD_PATH)/%.o: %.c $(BUILD_PATH)/%.d
	@echo $(CC_TEXT)
	@if test \( ! \( -d $(@D) \) \) ;then mkdir -p $(@D);fi
	$(OUTPUT)$(CC) $(C_FLAGS) $< -o $@

$(BUILD_PATH)/%.o: %.cc $(BUILD_PATH)/%.d
	@echo $(CXX_TEXT)
	@if test \( ! \( -d $(@D) \) \) ;then mkdir -p $(@D);fi
	$(OUTPUT)$(CXX) $(CXX_FLAGS) $< -o $@

# Every dependency file depends only on the corresponding source file
$(BUILD_PATH)/%.d: %.c
	@echo $(DEB_TEXT)
	@if test \( ! \( -d $(@D) \) \) ;then mkdir -p $(@D);fi
	$(OUTPUT)$(call make-depend,$<,$(subst .d,.o,$@),$(subst .o,.d,$@))

$(BUILD_PATH)/%.d: %.cc
	@echo $(DEB_TEXT)
	@if test \( ! \( -d $(@D) \) \) ;then mkdir -p $(@D);fi
	$(OUTPUT)$(call make-depend,$<,$(subst .d,.o,$@),$(subst .o,.d,$@))

git_version.h:
	@echo "Generating version information"
	$(OUTPUT)./git_version.sh -s ../ -o git_version.h

clean: clean-dep clean-obj 

clean-dep:
	$(RM) $(DEP)

clean-obj:
	$(RM) $(OBJ)

distclean: clean
	$(RM) -r $(BUILD_PATH)

#***************************** INCLUDE EVERY EXISTING DEPENDENCY FILE  *****************************
include $(EXISTING_DEPS)
#*****************************		END INCLUDE		       *****************************

.PHONY: all clean clean-deps clean-obj distclean 

# usage: $(call make-depend,source-file,object-file,depend-file)
define make-depend
  $(CXX) -MM       \
        -MF $3    \
        -MP       \
        -MT $2    \
	$(INCLUDE_PATHS) \
        $1
endef
