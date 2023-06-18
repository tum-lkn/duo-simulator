CC = g++
CFLAGS = -Wall -g -std=c++1z -O3
CRT=`pwd`
INCLUDE= -I/$(CRT)/.. -I$(CRT) 
#-I$(CRT)/ksp -I$(CRT)/ksp/boost
LIB=-L.. 
#-Lksp

all: htsim_mixed_expander

htsim_mixed_expander: main_mixed_expander.o ../libhtsim.a expander_topology.o
	$(CC) $(CFLAGS) main_mixed_expander.o expander_topology.o $(LIB) -lhtsim -o htsim_mixed_expander

main_mixed_expander.o: main_mixed_expander.cpp
	$(CC) $(INCLUDE) $(CFLAGS) -c main_mixed_expander.cpp

expander_topology.o: expander_topology.cpp expander_topology.h topology.h
	$(CC) $(INCLUDE) $(CFLAGS) -c expander_topology.cpp

clean:	
	rm -f *.o htsim_ndp* htsim_tcp* htsim_dctcp* htsim_mixed*
