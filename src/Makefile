#!/bin/make
CC = gcc
CFLAGS = -O2 -Wall -DGSFOPT
CXX = g++
CXXFLAGS = -O2 -Wall -DGSFOPT
LDFLAGS = -lm -lz
TARGET = gsfopt
SRCS = $(TARGET).cpp PSFFile.cpp ZlibReader.cpp ZlibWriter.cpp vbam/apu/Blip_Buffer.cpp vbam/apu/Effects_Buffer.cpp vbam/apu/Gb_Apu.cpp vbam/apu/Gb_Oscs.cpp vbam/apu/Multi_Buffer.cpp vbam/gba/Sound.cpp vbam/gba/bios.cpp vbam/gba/GBA.cpp vbam/gba/GBA-arm.cpp vbam/gba/GBA-thumb.cpp
OBJS := $(SRCS:.cpp=.o)

.PHONY: all clean
.SUFFIXES: .c .cpp .o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) -o $@ $(OBJS) $(LDFLAGS)

clean:
	$(RM) $(TARGET)
	$(RM) $(OBJS)

.c.o:
	$(CC) $(CFLAGS) -o $@ -c $^
.cpp.o:
	$(CXX) $(CXXFLAGS) -o $@ -c $^
