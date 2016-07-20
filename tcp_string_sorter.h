/*
tcp_string_sorter is simple TCP server to sorting incoming characters.
It receives strings with ZERO_CHAR ('\n' by default) at the end,
sorts chars in it by descending of ASCII-codes,
and sends sorted string back with ZERO_CHAR at the end.

Connection with client finished if server gives
OFF_MSG + ZERO_CHAR string ("OFF\n" by default).

To stop server send to it string
STOP_MSG + ZERO_CHAR ("STOP\n" by default).

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

	
	#define STOP_MSG "STOP"
	#define OFF_MSG "OFF"
	#define ZERO_CHAR '\n'
	#define MAX_EVENTS 100
	#define MAX_BUF 512
	#define WAIT_TIMEOUT -1
	#define DBG 1


	// sort process states
	enum SortStates {
		  SInit // 0 init data to read
		, SReadToBuf // 1 read data from 
		, SCheckEnd // 2 check if recieved "OFF" message
		, SSortBuf // 3 sort data in buf
		, SFillBufFromSort // 4 fill bufer to send
		, SSendBuf // 5 send buf to client
	};

	// Operations types with events in client context event
	// see setEventType()
	enum EventOpType {
		  ETAdd //	0 - Add new event and set it event type;
		, ETModify //	1 - Modify event, by adding new event type;
		, ETRemove //	2 - Remove event type, but not event
	};
	
	// server context
	struct str_sorter_server {
		// server constants
		char* end_msg; // end of client message
		char* stop_msg; // stop server message
		int end_msg_len; // length of end msg (calculate)
		int stop_msg_len; // length of stop msg (calculate)
		int sort_end; // max index in sort array (UCHAR_MAX+1)
		
		// net constants
		int epfd; // epoll descriptor
		struct epoll_event* events; // array to catch epoll events
		int maxevents; // epoll events array size
		
		int fd; // server socket descriptor
		
		int port; // server port
		struct sockaddr_in addr; // IP address
		socklen_t addr_len; // length of IP address
		
		// list of clients sockets descriptinos
		struct node* dlist; 
	};
	
	// client socket and sort procedure context (skd)
	struct socket_descr {
		int fd; // socket descriptor
		int events; //epoll events assigning for socket (EPOLLIN, EPOLLUOUT)
		
		int port; // client port
		struct sockaddr_in addr; // IP address
		socklen_t addr_len; // length of IP address
		
		int circles; // count of number epoll wait without reading data
		
		unsigned int sort[UCHAR_MAX+1]; // sort array
		char buf[MAX_BUF]; // recieve buffer
		char send_buf[MAX_BUF]; // send buffer
		
		int buf_idx; // index in buffer, used while sort data
		unsigned int sort_idx; // index in sort, used while prepare send_buf to send
		int snd_len; // length of the send buffer
		ssize_t rb; // count of read bytes
		ssize_t pos_send; // count of sent bytes
		enum SortStates state; // state of poscess (see States enum)
		int flStartOfTheString; // it is start of the string state
		int continue_listen; // flag to stop working
	};
	
	//////////////////////////////////////////////////////////////////////////
	// Server functions
	
	// create server context
	struct str_sorter_server* createServer(const char* ip_str, const int port);
	
	// runs string-sorter server listens on the port
	int runServer(struct str_sorter_server* sss);
	
	// remove all clients and server socket
	// close epoll descriptor
	// free server context
	void removeServer(struct str_sorter_server*);
	
	// create struct with client context, add it to list (head)
	struct socket_descr* addSocketDescr(struct str_sorter_server* sss,
			int fd, struct sockaddr_in* cl_addr, socklen_t cl_addr_len);
	
	// remove descriptor from epoll and dlist and close socket.
	// frees sort context and socket description
	// returns next node if exists, or NULL
	struct node* removeSocketDescr(struct str_sorter_server* sss,
									struct socket_descr* skd);
	
	// Add or modify epoll event type
	// event_code {EPOLLIN, EPOLLOUT, EPOLLPRI}
	// flAMR - type of event operation (add, modify, remove)
	int setEventType(struct str_sorter_server* sss,
			int event_code, struct socket_descr* skd, enum EventOpType flAMR);
	
	
	///////////////////////////////////////////////////////////////////////////
	// Client functions
	
	// init client context.
	// process client buffers (sort of fill for send)
	// stop working, if recieve "OFF" string
	// return:
	//   > 0 if server should close connection with client
	//			(received OFF signal);
	//	 == 0 if server need to continue listening to client;
	//   < 0 if server need to end working (received STOP message)
	int processClient(struct str_sorter_server* sss, struct socket_descr* skd);
	
	// read data from client
	// return:
	//	> 0 if client end sending;
	//  = 0 if server needs to continue working with client;
	//	< 0 error. server should close connection with client.
	int readClient(struct str_sorter_server* sss, struct socket_descr* skd);
	
	// write data to client
	// return:
	//	> 0 delete socket from OUT events;
	//	= 0 if server needs to continue writing to client;
	//	< 0 error. server should close connection with client.
	int writeClient(struct str_sorter_server* sss, struct socket_descr* skd);
	
#endif
