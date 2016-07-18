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
	
	#define MAX_EVENTS 100
	#define MAX_BUF 512
	#define WAIT_TIMEOUT -1
	#define DBG 1

	const int sort_end = UCHAR_MAX; // max index in sort array
	
	struct socket_descr {
		int fd; // socket descriptor
		int events; //epoll events assigning for socket (EPOLLIN, EPOLLUOUT)
		struct sockaddr_in cl_addr; // IP address
		socklen_t cl_addr_len; // length of IP address
		
		struct sort_context* context; // for server listen socket == NULL
	};
	
	struct sort_context{
		int proc_res; // result of processing
		
		unsigned int sort[UCHAR_MAX+1]; // sort array
		char buf[MAX_BUF]; // recieve buffer
		char send_buf[MAX_BUF]; // send buffer
		
		int buf_idx; // index in buffer, used while sort data
		int	sort_idx; // index in sort, used while prepare send_buf to send
		int snd_len; // length of the send buffer
		ssize_t rb; // count of have read bytes
		ssize_t pos_send; // count of have sent bytes
		int state; // state of poscess
		int flStartOfTheString; // it is start of the string
		int continue_listen;
	};
	
	// process states
	enum States {
		  SInit // 0 init data to read
		, SReadToBuf // 1 read data from 
		, SCheckEnd // 2 check if recieved "OFF" message
		, SSortBuf // 3 sort data in buf
		, SFillBufFromSort // 4 fill bufer to send
		, SSendBuf // 5 send buf to client
	};

	// runs string-sorter server listens on the port
	int runServer(int port);
	
	// create and initialize sort context
	struct sort_context* CreateSortContext();

	// create struct with socket description
	struct socket_descr* AddSocketDescr(struct node** head, int fd, struct sockaddr_in* cl_addr, socklen_t cl_addr_len, int create_sort_context);
	
	// remove descriptor from epoll and dlist and close socket. frees sort context and socket description
	// returns next node if exists, or NULL
	struct node* RemoveSocket(struct node** head, int epfd, struct socket_descr* skd);
	
	// close all clients and server socket, close epoll descriptor
	void RemoveAllConnections(int epfd, struct node** head);
	
	// Add or modify epoll event type
	// event_code {EPOLLIN, EPOLLOUT, EPOLLPRI}
	// flAMR -
	//	0 - Add new event and set it event type;
	//	1 - Modify event, by adding new event type;
	//	2 - Remove event type, but not event
	int SetEventType(int epfd, int event_code, struct socket_descr* skd, int flAMR);
	
	// init client context.
	// process client buffers (sort of fill for send)
	// stop working, if recieve "OFF" string
	// return:
	//   > 0 if server should close connection with client (received OFF signal);
	//	 == 0 if server need to continue listening to client;
	//   < 0 if server need to end working (received STOP message)
	int process_client(int epfd, struct socket_descr* skd);
	
	// read data from client
	// return:
	//	>= 0 if server needs to continue working with client;
	//	< 0 if server should close connection with client
	int process_client_read(struct socket_descr* skd);
	
	// write data to client
		// return:
	//	>= 0 if server needs to continue working with client;
	//	< 0 if server should close connection with client
	int process_client_write(int epfd, struct socket_descr* skd);
	
#endif
