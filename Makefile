CC = g++
CFLAGS = -std=c++11 -Wall -Wextra -fPIC -luring -lpthread -g # -O0
# TARGET = nvme_check
LDFLAGS = 

# without lib prefix
LIBNAME = fdp

# SRC = libfdp.c
#TODO(mfd) : Add a rule to execute clang-format on all source and header files

SRCDIR = src
INCDIR = include
OBJDIR = obj
LIBDIR = lib

SRCS = $(wildcard $(SRCDIR)/*.c)
OBJS = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRCS))
HEADER = $(INCDIR)/$(LIBNAME).h  # Main public header


# Installation paths
PREFIX = /usr/local
INSTALL_INC_DIR = $(PREFIX)/include
INSTALL_LIB_DIR = $(PREFIX)/lib

STATIC_LIB = $(LIBDIR)/lib$(LIBNAME).a
SHARED_LIB = $(LIBDIR)/lib$(LIBNAME).so


.PHONY: all clean install uninstall


# all: $(TARGET)
all: $(STATIC_LIB) $(SHARED_LIB)

# -c option to tell gcc to stop before linking
$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -I$(INCDIR) -c $< -o $@

$(STATIC_LIB): $(OBJS) | $(LIBDIR)
	ar rcs $@ $^

$(SHARED_LIB): $(OBJS) | $(LIBDIR)
	$(CC) -shared -o $@ $^ $(LDFLAGS)

$(OBJDIR) $(LIBDIR):
	mkdir -p $@

# $(TARGET): $(SRC)
#	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) -luring

clean:
	rm -rf $(OBJDIR) $(LIBDIR)

install: all
	install -d $(INSTALL_INC_DIR) $(INSTALL_LIB_DIR)
	@echo $(HEADER)
	install -m 644 $(HEADER) $(INSTALL_INC_DIR)
	install -m 644 $(STATIC_LIB) $(INSTALL_LIB_DIR)
	install -m 755 $(SHARED_LIB) $(INSTALL_LIB_DIR)
	ldconfig

uninstall:
	rm -f $(INSTALL_INC_DIR)/$(notdir $(HEADER))
	rm -f $(INSTALL_LIB_DIR)/lib$(LIBNAME).*
	ldconfig
