CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -pthread

scheduler: scheduler.cpp
	$(CXX) $(CXXFLAGS) -o scheduler scheduler.cpp

run: scheduler
	./scheduler ../data/employees.txt

clean:
	rm -f scheduler

.PHONY: run clean
