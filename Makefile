exe: tcp_string_sorter.c lib_solution.h
	    gcc -Wall -std=c99 -o tcp_string_sorter tcp_string_sorter.c

<<<<<<< HEAD
clean:
	rm tcp_string_sorter 2> /dev/null
=======
exe: tcp_string_sorter.c, lib_solution.c
    gcc tcp_string_sorter.c -o tcp_string_sorter
  
>>>>>>> c0110ee79476a441f8d67a39f4f418efc09a1611
