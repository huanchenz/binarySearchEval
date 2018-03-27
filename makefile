all: binary_search

binary_search: binary_search.cpp
	g++ -O3 -Wall binary_search.cpp -o binary_search -L./ARTSynchronized -l ARTSynchronized

clean:
	rm -f binary_search binary_search.o
