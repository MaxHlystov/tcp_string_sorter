## tcp_string_sorter

It is simple TCP server to sort incoming characters. It receives strings with '\n' on the end, sorts chars in it by descending of ASCII-codes, and sends sorted string back with '\n' at the end.

Connection with client finished if server gives '*OFF*' string (with '\n' at the end).

To stop server send to it string '*STOP*' (with '\n' at the end).

To start server used:  
	`tcp_string_sorter [ip address] port_number`,  
	where `ip address` (optional) is ip address of server;  
	`port_number` is port to listen.

Server is not daemon. If you want to start it in background use & character. For example:  
	`tcp_string_sorter 55555 &`

For test use: `test.sh`
