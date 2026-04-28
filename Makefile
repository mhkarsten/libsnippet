.PHONY: depend clean zydis python

# Universal compilation components
CC = clang
OBJ_DIR = build/obj
SRC_DIR = src
LIB_DIR = build
DEPS = Makefile.depend

SRCS := $(wildcard $(SRC_DIR)/*)
OBJS := $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))
HEADS = $(shell find ./include -type f -name *.h)
ZYDIS_STATIC_LIBS := $(shell find zydis/builddir -name '*.a')

INCLUDES = -I./include -I./zydis/include

CFLAGS = -g -O3 -Wall -fPIC -mxsave $(INCLUDES) -D_GNU_SOURCE -DZYDIS_STATIC_BUILD

LDFLAGS = -lm -lpthread 							\
		  -Wl,--whole-archive $(ZYDIS_STATIC_LIBS) 	\
		  -Wl,--no-whole-archive 					\
		  -Wl,-rpath,$$ORIGIN

# TODO: Add flag for test vs production
# Custom cflags for each target
libsnippet_CFLAGS := -UNDEBUG -DDEBUG

all: $(LIB_DIR)/libsnippet.so

# Compile C files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(HEADS)
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) $($(basename $@)_CFLAGS) $< -o $@ 

# Compile S files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.S $(HEADS)
	@mkdir -p $(dir $@)
	$(CC) $(INCLUDES) -fPIC -c $< -o $@

# @echo "SNIPPET_INCLUDES := -I$(abspath ./include) -I$(abspath ./zydis/include) -I$(abspath ./zydis/dependencies/zycore/include) -DZYDIS_STATIC_BUILD" >> $@
# @echo "SNIPPET_LDFLAGS  := -L$(abspath ./build) -lsnippet -Wl,-rpath,$(abspath ./build)" >> $@

zydis:
	meson setup 					\
		--wipe 						\
		-Ddefault_library=static 	\
		-Db_staticpic=true 			\
		-Dbuildtype=release 		\
		zydis/builddir zydis
	meson compile -C zydis/builddir

$(LIB_DIR)/libsnippet.so: zydis $(OBJS)
	@mkdir -p $(LIB_DIR)
	$(CC) -shared -fPIC 			\
		-Wl,-soname,libsnippet.so 	\
		-o $@						\
		$(OBJS) $(LDFLAGS)

python: $(LIB_DIR)/libsnippet.so
	python build_library.py

depend:
	$(CXX) $(INCLUDES) -MM $(SRCS) > $(DEPS)
	@sed -i -E "s/^(.+?).o: ([^ ]+?)\1/\2\1.o: \2\1/g" $(DEPS)

clean:
	rm -f $(OBJ_DIR)/*.o
	rm -rf
	rm -rf zydis/builddir
	rm -rf _libsnippet*
	rm -f zydis_cdef_ref.h
	
-include $(DEPS)
