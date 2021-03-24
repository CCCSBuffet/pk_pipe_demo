SYS := $(shell g++ -dumpmachine)
ifneq (, $(findstring apple, $(SYS)))
CFLAGS	= -Wall -std=c++11 -g
else
CFLAGS	= -Wall -std=c++11 -g
endif

CC	    = g++
LFLAGS	= 

srcs = $(wildcard *.cpp)
objs = $(srcs:.cpp=.o)
deps = $(srcs:.cpp=.d)

parent: parent.o
	$(CC) $^ -o $@ $(LFLAGS) -lncurses

child: child.o
	$(CC) $^ -o $@ $(LFLAGS)

%.o: %.cpp
	$(CC) -MMD -MP $(CFLAGS) -c $< -o $@

.PHONY: clean

# $(RM) is rm -f by default
clean:
	$(RM) $(objs) $(deps) parent child core

-include $(deps)
