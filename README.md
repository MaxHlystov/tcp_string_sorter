## tcp_string_sorter

It is simple TCP server to sort incoming characters. It receives strings with zero on the end, sorts chars in it by descending of ASCII-codes, and sends sorted string back with zero on the end.

Connection with client finished if server gives '*OFF*' string.

To stopp server send to it string '*STOP*'.

To start server used:

	`tcp_string_sorter port_number`
	
where port_number - is port to listen.

Server is not daemon. If you want to start it in background use &:

	`tcp_string_sorter port_number &`
	
For example:

	`tcp_string_sorter 55555 &`
	
See also test.sh script.
