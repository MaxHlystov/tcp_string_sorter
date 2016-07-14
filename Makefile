exe: tcp_string_sorter.c lib_solution.h
	    gcc -Wall -std=c99 -o tcp_string_sorter tcp_string_sorter.c

clean:
	rm tcp_string_sorter 2> /dev/null
