#
# Makefile
#
BIN_READ = plxx_read
BIN_BRIDGE = plbridge
BINDIR = /usr/local/sbin/
DESTDIR = /usr
PREFIX = /local
SERVICE = plxxd.service
SERVICEDIR = /etc/systemd/system

# modbus header files could be located in different directories
#INC += -I$(DESTDIR)/include/modbuspp -I$(DESTDIR)/include/modbus
#INC += -I$(DESTDIR)$(PREFIX)/include/modbuspp -I$(DESTDIR)$(PREFIX)/include/modbus

CC=gcc
CXX=g++
CFLAGS = -Wall -Wshadow -Wundef -Wmaybe-uninitialized -Wno-unknown-pragmas
CFLAGS += -O3 $(INC)

# directory for local libs
LDFLAGS = -L$(DESTDIR)$(PREFIX)/lib
#LIBS += -lstdc++ -lm -lmosquitto -lconfig++ -lmodbus
LIBS += -lstdc++ -lm -lmosquitto -lconfig++
#LIBS_READ += -lstdc++

#VPATH =

#$(info LDFLAGS ="$(LDFLAGS)")
#$(info INC="$(INC)")

# folder for our object files
OBJDIR = ./obj

#CSRCS += $(wildcard *.c)
#CPPSRCS += $(wildcard *.cpp)

#COBJS = $(patsubst %.c,$(OBJDIR)/%.o,$(CSRCS))
#CPPOBJS = $(patsubst %.cpp,$(OBJDIR)/%.o,$(CPPSRCS))

#SRCS = $(CSRCS) $(CPPSRCS)
#OBJS = $(COBJS) $(CPPOBJS)

.PHONY: clean

all: read

#$(OBJDIR)/%.o: %.c
#	@mkdir -p $(OBJDIR)
#	@echo "CC $<"
#	@$(CC)  $(CFLAGS) -c $< -o $@

$(OBJDIR)/%.o: %.cpp
	@mkdir -p $(OBJDIR)
	@echo "CXX $<"
	@$(CXX)  $(CFLAGS) -c $< -o $@

#read: $(OBJS)
#	$(CC) -o $(BIN_READ) $(OBJS) $(LDFLAGS) $(LIBS_READ)

$(OBJDIR)/plxx.o: plxx.h

$(OBJDIR)/plbridge.o: plbridge.h

$(OBJDIR)/mqtt.o: mqtt.h

$(OBJDIR)/modbustag.o: modbustag.h

read: $(OBJDIR)/plxx.o $(OBJDIR)/plxx_read.o
	$(CXX) -o $(BIN_READ) $(OBJDIR)/plxx.o $(OBJDIR)/plxx_read.o $(LDFLAGS)

bridge: $(OBJDIR)/plxx.o $(OBJDIR)/plbridge.o $(OBJDIR)/mqtt.o $(OBJDIR)/mmodbustag.o
	$(CXX) -o $(BIN_BRIDGE) $(OBJDIR)/plxx.o $(OBJDIR)/plbridge.o $(OBJDIR)/mqtt.o $(OBJDIR)/mmodbustag.o $(LDFLAGS) $(LIBS)

#default: $(OBJS)
#	$(CC) -o $(BIN) $(OBJS) $(LDFLAGS) $(LIBS)

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
	install -o root $(BIN_READ) $(BINDIR)$(BIN_READ)
	@echo ++++++++++++++++++++++++++++++++++++++++++++
	@echo ++ $(BIN_READ) has been installed in $(BINDIR)
#	@echo ++ systemctl start $(BIN)
#	@echo ++ systemctl stop $(BIN)
endif

# make systemd service
service:
ifneq ($(shell id -u), 0)
	@echo "!!!! service requires root !!!!"
else
	install -o root $(SERVICE) $(SERVICEDIR)
	@systemctl daemon-reload
	@systemctl enable $(SERVICE)
	@echo $(BIN_BRIDGE) is now available a systemd service
endif
