/*
tcp_string_sorter is simple TCP server to sortion incoming characters.
It receives strings with zero on the end,
sorts chars in it by descending of ASCII-codes,
and sends sorted string back with zero on the end.

Connection with client finished if server gives 'OFF\0' string.
To stopp server send to it string 'STOP\0'.

To start server used:
	tcp_string_sorter port_number
where port_number - is port to listen.
Server is not daemon. If you want to start it in background use &:
	tcp_string_sorter port_number &
For example:
	tcp_string_sorter 55555 &
	
See also test.sh script.
*/

#ifndef __TCP_SORT_SERVER
#define __TCP_SORT_SERVER
	
	#ifndef _POSIX_C_SOURCE
		#define _POSIX_C_SOURCE 199310L
	#endif
	
	#include <sys/types.h>
	#include <sys/stat.h>
	#include <sys/socket.h>
	#include <arpa/inet.h>
	#include <netdb.h>
	#include <fcntl.h>
	#include <unistd.h>
	#include <stdio.h>
	#include <stdlib.h>
	#include <ctype.h>
	#include <string.h>
	#include <limits.h>

	#include "tcp_string_sorter.h"

	
	#define MAX_BUF 512
	#define DBG 0

	// process states
	enum States {
		  SInit // init data to read
		, SReadToBuf // 1 read data from 
		, SCheckEnd // 1.5 check if recieved "OFF" message
		, SSortBuf // 2 sort data in buf
		, SFillBufFomSort // 3 fill bufer to send
		, SSendBuf // 5 send buf to client
	};

#endif
