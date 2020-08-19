#
# Makefile
#
BIN_READ = plxx_read
BIN_BRIDGE = plbridge
BINDIR = /usr/local/sbin/
DESTDIR = /usr
PREFIX = /local
SERVICE = plbridge.service
SERVICEDIR = /etc/systemd/system

CC=gcc
CXX=g++
CFLAGS = -Wall -Wshadow -Wundef -Wmaybe-uninitialized -Wno-unknown-pragmas
CFLAGS += -O3 $(INC)

# directory for local libs
LDFLAGS = -L$(DESTDIR)$(PREFIX)/lib
LIBS += -lstdc++ -lm -lmosquitto -lconfig++

#VPATH =

# folder for our object files
OBJDIR = ./obj

#CSRCS += $(wildcard *.c)
#CPPSRCS += $(wildcard *.cpp)

#COBJS = $(patsubst %.c,$(OBJDIR)/%.o,$(CSRCS))
#CPPOBJS = $(patsubst %.cpp,$(OBJDIR)/%.o,$(CPPSRCS))

#SRCS = $(CSRCS) $(CPPSRCS)
#OBJS = $(COBJS) $(CPPOBJS)

.PHONY: all clean default read bridge service

default:
	@echo
	@echo "Use one of the following:"
	@echo "make read (to compile plxx_read)"
	@echo "make bridge (to compile plbridge)"
	@echo "make all (to compile plxx_read and plbridge)"
	@echo "sudo make install (to install binaries)"
	@echo "sudo make service (to make plbridge a service)"

all: read bridge

#$(OBJDIR)/%.o: %.c
#	@mkdir -p $(OBJDIR)
#	@echo "CC $<"
#	@$(CC)  $(CFLAGS) -c $< -o $@

$(OBJDIR)/%.o: %.cpp
	@mkdir -p $(OBJDIR)
	@echo "CXX $<"
	@$(CXX)  $(CFLAGS) -c $< -o $@


$(OBJDIR)/plxx.o: plxx.h
$(OBJDIR)/plbridge.o: plbridge.h plxx.h mqtt.h pltag.h hardware.h
$(OBJDIR)/mqtt.o: mqtt.h
$(OBJDIR)/pltag.o: pltag.h
$(OBJDIR)/hardware.o: hardware.h

read: $(OBJDIR)/plxx.o $(OBJDIR)/plxx_read.o
	$(CXX) -o $(BIN_READ) $(OBJDIR)/plxx.o $(OBJDIR)/plxx_read.o $(LDFLAGS)

bridge: $(OBJDIR)/plxx.o $(OBJDIR)/plbridge.o $(OBJDIR)/mqtt.o $(OBJDIR)/pltag.o $(OBJDIR)/hardware.o
	$(CXX) -o $(BIN_BRIDGE) $(OBJDIR)/plxx.o $(OBJDIR)/plbridge.o $(OBJDIR)/mqtt.o $(OBJDIR)/pltag.o $(OBJDIR)/hardware.o $(LDFLAGS) $(LIBS)

#	nothing to do but will print info
nothing:
	$(info OBJS ="$(OBJS)")
	$(info DONE)

clean:
	rm -f $(OBJDIR)/*.o
#	rm -f $(OBJS)

install:
ifneq ($(shell id -u), 0)
	@echo "!!!! install requires root !!!!"
else
	@install -o root $(BIN_READ) $(BINDIR)$(BIN_READ)
	@install -o root $(BIN_BRIDGE) $(BINDIR)$(BIN_BRIDGE)
	@echo ++++++++++++++++++++++++++++++++++++++++++++
	@echo ++ $(BIN_READ) and $(BIN_BRIDGE) has been installed in $(BINDIR)
	@echo ++ sudo systemctl restart $(BIN_BRIDGE)
endif

# make systemd service
service:
ifneq ($(shell id -u), 0)
	@echo "!!!! service requires root !!!!"
else
	install -m 644 -o root $(SERVICE) $(SERVICEDIR)
	@systemctl daemon-reload
	@systemctl enable $(SERVICE)
	@echo $(BIN_BRIDGE) is now available a systemd service
endif
