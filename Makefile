SRCINC = /home/ahti/src/inc
QTDIR ?= /usr

VPATH = $(SRCINC)/unix:$(SRCINC)

MOC_CPP_SRCS = main.cpp
CPP_SRCS = Misc.cpp Except.cpp File.cpp cmdline.cpp

PROG = aide

INCPATH  = -I$(SRCINC)/unix -I$(SRCINC) -I$(QTDIR)/include -I$(QTDIR)/mkspecs/default

CFLAGS += -g -O2
CFLAGS += -Wall -fno-for-scope
CFLAGS += -D_GNU_SOURCE -D_THREAD_SAFE -enable-threads $(INCPATH)

CFLAGS += -I/usr/include/freetype2 -D_REENTRANT  -DQT_NO_DEBUG -DQT_THREAD_SUPPORT
LDADD += -Wl,-rpath,$(QTDIR)/lib -L$(QTDIR)/lib -L/usr/X11R6/lib -lqt-mt -lpthread -lXext -lX11 -lm

CPP=g++
LD=gcc
MOC=$(QTDIR)/bin/moc

.SUFFIXES: .moc .o

.cpp.o:
	$(CPP) -c $(CFLAGS) $<
.cpp.moc:
	$(MOC) $< -o $@ 

MOCS = $(MOC_CPP_SRCS:%.cpp=%.moc)
OBJS = $(CPP_SRCS:%.cpp=%.o) $(MOC_CPP_SRCS:%.cpp=%.o)

all:: $(PROG)

$(PROG): $(MOCS) $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $(OBJS) $(LDADD)

clean::
	@rm -f *.o *.so *.a *.moc $(PROG)
