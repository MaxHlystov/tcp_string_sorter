#ifndef _POSIX_C_SOURCE
	#define _POSIX_C_SOURCE 199310L
#endif

#ifndef _BSD_SOURCE
	#define _BSD_SOURCE
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <stdarg.h>

#include "lib_solution.h"
#include "tcp_string_sorter.h"

#ifdef DBG
	// show indent before log message
	#define DBG_MAX_INDENT 10
	#define DBG_INDENT_CHAR '\t'
	
	char dbg_indent_str[DBG_MAX_INDENT+1] = ""; // indent string to show
	int dbg_indent = 0; // current indent
	
	void dbgAddIndent(int delta){
		if(delta == 0) return;
		if(delta < 0){
			dbg_indent += delta;
			if(dbg_indent < 0) dbg_indent = 0;
			dbg_indent_str[dbg_indent] = '\0';
		}
		else{
			int indent = dbg_indent + delta;
			if(indent >= DBG_MAX_INDENT) delta -= indent-DBG_MAX_INDENT;
			memset(dbg_indent_str+dbg_indent, DBG_INDENT_CHAR, delta);
			dbg_indent += delta;
			dbg_indent_str[dbg_indent] = '\0';
		}
	}
	
	void dbgShowIndent(){
		if(dbg_indent == 0) return;
		printf("%s", dbg_indent_str);
	}
	
	// increase indent before printf message.
	// or decreas indent after printf message.
	void dbgPrintf(int delta_indent, const char* format, ...){
		va_list ap;
		va_start(ap, format);
	
		if(delta_indent < 0) dbgAddIndent(delta_indent);
		
		dbgShowIndent();
		vprintf(format, ap);

		if(delta_indent > 0) dbgAddIndent(delta_indent);
	}
#endif


struct socket_descr* addSocketDescr(struct str_sorter_server* sss,
			int fd, struct sockaddr_in* cl_addr, socklen_t cl_addr_len){
	#ifdef DBG
		dbgPrintf(1, "+AddSocketDescr(%d)\n", fd);
	#endif
	
	struct socket_descr* skd =
		(struct socket_descr*)malloc(sizeof(struct socket_descr));
	if(NULL == skd){
		#ifdef DBG
			dbgPrintf(1, "Error memory allocation\n");
		#endif
		
		return NULL;
	}
	
	skd->fd = fd;
	skd->events = 0;
	
	if(NULL != cl_addr){
		memcpy(&(skd->addr), cl_addr, cl_addr_len);
		skd->addr_len = cl_addr_len;
	}
	else skd->addr_len = 0;
	
	skd->circles = 0;
	
	skd->buf_idx = 0; // index in buffer, used while sort data
	skd->sort_idx = 0; // index in sort, used while prepare send_buf to send
	skd->snd_len = 0; // length of the send buffer
	skd->rb = 0; // count of have read bytes
	skd->pos_send = 0; // count of have sent bytes
	skd->state = SInit; // state of poskdess
	skd->flStartOfTheString = 1; // it is start of the string
	skd->continue_listen = 1;

	push(&(sss->dlist), skd->fd, skd);
	
	#ifdef DBG
		dbgPrintf(0,
			"Sort size %lu. Read buffer size %lu. Send buffer size %lu\n",
			sizeof(skd->sort), sizeof(skd->buf), sizeof(skd->send_buf));
	#endif
	
	#ifdef DBG
		dbgPrintf(-1, "-AddSocketDescr\n");
	#endif
	
	return skd;
}


int setEventType(struct str_sorter_server* sss, int event_code,
					struct socket_descr* skd, enum EventOpType flAMR){
	#ifdef DBG
		dbgPrintf(1, "SetEventType(socket=%d, event code= %d, flAMR=%d)\n",
			skd->fd, event_code, flAMR);
	#endif
	
	// if event code has been set, do nothing
	if(((skd->events & event_code) > 0) == (flAMR != ETRemove)){
		#ifdef DBG
			dbgPrintf(-1, "Do not need to change events. Event type has been set\n");
		#endif
		return 0;
	}
	
	struct epoll_event event;
	event.data.ptr = skd;
	
	int op;
	
	if(flAMR == ETAdd) {
		// Add new event
		skd->events = event_code;
		op = EPOLL_CTL_ADD;
	}
	else{
		op = EPOLL_CTL_MOD;
		
		if(flAMR == ETModify){
			// Modify event type
			skd->events += event_code;
		}
		else {
			// Remove event type
			skd->events -= event_code;
		}
	}
	
	event.events = skd->events;
	
	#ifdef DBG
		dbgPrintf(-1, "-SetEventType\n");
	#endif
	
	return epoll_ctl(sss->epfd, op, skd->fd, &event);
}


struct node* removeSocketDescr(struct str_sorter_server* sss,
			struct socket_descr* skd){
	#ifdef DBG
		dbgPrintf(1, "+removeSocketDescr(%d)\n", skd->fd);
	#endif
	
	if(NULL == skd){
		#ifdef DBG
			dbgPrintf(-1, "Socket context is NULL\n");
		#endif
		
		return NULL;
	}
	
	int fd = skd->fd;
	
	#ifdef DBG
		dbgPrintf(0, "Try to delete event from eopll\n");
	#endif
	
	struct epoll_event event; // cpoll_ctl(DEL) ignored event content but needs it
	event.events = EPOLLIN;
	epoll_ctl(sss->epfd, EPOLL_CTL_DEL, fd, &event);
	
	#ifdef DBG
		dbgPrintf(0, "Close soket\n");
	#endif
		
	shutdown(fd, SHUT_RDWR);
	close(fd);

	#ifdef DBG
		dbgPrintf(0, "Try to delete socket description\n");
	#endif
	
	free(skd);
	
	#ifdef DBG
		dbgPrintf(0, "Try to delete description from dlist\n");
	#endif
	
	struct node* next = NULL;
	struct node* deleted = deleteNode(&(sss->dlist), fd);
	if(NULL != deleted){
		#ifdef DBG
			dbgPrintf(0, "Try to delete list item\n");
		#endif
		
		next = deleted->next;
		free(deleted);
	}
	
	#ifdef DBG
		dbgPrintf(-1, "-removeSocketDescr\n");
	#endif
	
	return next;
}


int processClient(struct str_sorter_server* sss,
		struct socket_descr* skd){
	#ifdef DBG
		dbgPrintf(1, "+processClient(socket %d; state %d; continuel_listen %d)\n",
				skd->fd, skd->state, skd->continue_listen);
				
	#endif
	
	if(!skd->continue_listen){
		#ifdef DBG
			dbgPrintf(-1, "Do not continue listen\n");
		#endif
		return -1; // close client
	}
	
	switch(skd->state){
	case SInit: // 0 init data to parse new string in buffer
		#ifdef DBG
			dbgPrintf(0, "SInit\n");
		#endif
		
		memset(skd->sort, 0, (sss->sort_end+1) * sizeof(unsigned int));
		if(skd->buf_idx < skd->rb) skd->state = SCheckEnd;
		else skd->state = SReadToBuf;
		skd->flStartOfTheString = 1;
		break;
	
	case SReadToBuf: // 1 read data from
		#ifdef DBG
			dbgPrintf(0, "SReadToBuf\n");
		#endif
		break;
		
	case SCheckEnd: // 2 check if received control message
		#ifdef DBG
			dbgPrintf(0, "SCheckEnd\n");
		#endif
		
		if(skd->buf_idx >= skd->rb)
			// not start of the string
			skd->state = SReadToBuf;
		else{
			if(memcmp((skd->buf)+(skd->buf_idx),
					sss->end_msg, sss->end_msg_len) == 0){
				#ifdef DBG
					dbgPrintf(-1, "Found end msg\n");
				#endif
				skd->continue_listen = 0;
				return 1; // close client
			}
			else if(memcmp((skd->buf)+(skd->buf_idx),
					sss->stop_msg, sss->stop_msg_len) == 0){
				#ifdef DBG
					dbgPrintf(-1, "Found stop msg\n");
				#endif
				skd->continue_listen = 0;
				return -1; // stop server
			}
			else skd->state = SSortBuf;
		}
		skd->flStartOfTheString = 0;
		break;

	case SSortBuf: // 3 sort data in buf
		#ifdef DBG
			dbgPrintf(0, "SSortBuf\n");
		#endif
		
		while(skd->buf_idx < skd->rb){
			if(skd->buf[skd->buf_idx] == ZERO_CHAR) {
				#ifdef DBG
					dbgPrintf(0, "Find zero char\n");
				#endif
				
				skd->buf_idx++;
				skd->sort_idx = sss->sort_end;
				skd->snd_len = 0;
				skd->state = SFillBufFromSort;
				break;
			}
			
			unsigned code = (unsigned)(skd->buf[skd->buf_idx]);
			
			#ifdef DBG
				dbgPrintf(0, "  Got code %u, for sort %u\n", code, skd->sort[code]);
			#endif
			
			skd->sort[code]++;
			
			#ifdef DBG
				dbgPrintf(0, "  Set sort to %u\n", skd->sort[code]);
			#endif
				
			skd->buf_idx++;
		}
		if(skd->state == SFillBufFromSort) break;
		
		#ifdef DBG
			dbgPrintf(0, "Not find zero. We need to read again\n");
		#endif
		
		skd->flStartOfTheString = 0;
		skd->state = SReadToBuf;
		break;
		
	case SFillBufFromSort: // 4 fill bufer to send
		#ifdef DBG
			dbgPrintf(0, "SFillBufFromSort(sort_idx=%d; snd_len=%d)\n",
				skd->sort_idx, skd->snd_len);
		#endif
		
		while(skd->sort_idx > 0 && skd->snd_len < MAX_BUF){
			unsigned int cnt = skd->sort[skd->sort_idx];
			if(cnt) {
				#ifdef DBG
					dbgPrintf(0, "Found byte %u with count %u. Idx=%u\n",
							(unsigned char)(skd->sort_idx), cnt, skd->sort_idx);
				#endif
				
				if(skd->snd_len+cnt >= MAX_BUF-1){
					cnt = MAX_BUF-1 - skd->snd_len;
					skd->sort[skd->sort_idx] -= cnt;
				}
				
				memset(skd->send_buf+skd->snd_len,
					(unsigned char)(skd->sort_idx), cnt);
				skd->snd_len += cnt;
			}
			skd->sort_idx--;
		}
		
		#ifdef DBG
			dbgPrintf(0, "We filled sort array. Send it. Add socket to epoll out events.\n");
		#endif
		
		if(skd->snd_len < MAX_BUF) skd->send_buf[skd->snd_len] = ZERO_CHAR;
		skd->state = SSendBuf;
		skd->pos_send = 0;
		
		break;
		
	case SSendBuf: // 5 send buf to client
		#ifdef DBG
			dbgPrintf(0, "SSendBuf\n");
		#endif
		break;
	
	}

	#ifdef DBG
		dbgPrintf(-1, "-processClient(state %d)\n", skd->state);
	#endif

	return 0;
}


int readClient(struct str_sorter_server* sss, struct socket_descr* skd){
	#ifdef DBG
		dbgPrintf(1, "+readClient(socket %d;  continue flag %d; state %d)\n",
			skd->fd, skd->continue_listen, skd->state);
	#endif
	
	if(!skd->continue_listen){
		#ifdef DBG
			dbgPrintf(-1, "Don't continue listen\n");
		#endif
		return 1; // close client without error
	}
	
	if(skd->state != SReadToBuf){
		#ifdef DBG
			dbgPrintf(-1, "State is not SReadToBuf\n");
		#endif
		return 0; // it should do nothing
	}
	
	// read data from socket
	#ifdef DBG
		dbgPrintf(0, "SReadToBuf\n");
	#endif
	
	int rb = recv(skd->fd, skd->buf, MAX_BUF, 0);

	// if(rb == 0){
	// 	#ifdef DBG
	// 		dbgPrintf(-1, "Need to close connection: rb == 0\n");
	// 	#endif
	// 	return 1; // close connection
	// }
	if(rb <= 0) {
		if(skd->circles < 10 && (rb == 0 || errno == EAGAIN)){
			#ifdef DBG
				dbgPrintf(-1, "Try read again rb == %d, circles %d\n", rb, skd->circles);
			#endif
			
			skd->circles++;
			return 0; // try later
		}
		skd->continue_listen = 0;
		#ifdef DBG
			dbgPrintf(-1, "Error reading\n");
		#endif
		
		return -1; // error. close connection
	}
	
	#ifdef DBG
		dbgPrintf(0, "Read %d: ", rb);
		for(int i=0; i<rb; ++i)
			printf("%d ", skd->buf[i]);
		dbgPrintf(0, "\n", rb);
	#endif
	
	skd->circles = 0;
	
	skd->rb = rb;
	skd->buf_idx = 0;
	if(skd->flStartOfTheString) skd->state = SCheckEnd;
	else skd->state = SSortBuf;
	
	#ifdef DBG
		dbgPrintf(-1, "-readClient(state %d, flStartOfTheString %d)\n",
				skd->state, skd->flStartOfTheString);
	#endif
	
	return 0;
}


int writeClient(struct str_sorter_server* sss, struct socket_descr* skd){
	#ifdef DBG
		dbgPrintf(1, "writeClient(socket %d; continue flag %d; state: %d)\n",
			skd->fd, skd->continue_listen, skd->state);
	#endif
	
	if(!skd->continue_listen || skd->state != SSendBuf){
		#ifdef DBG
			dbgPrintf(-1, "Don't continue writing\n");
		#endif
		return 1; // end write
	}
	
	// send send_buf to client
	#ifdef DBG
		dbgPrintf(0, "SSendBuf\n");
	#endif
	
	if(skd->pos_send < skd->snd_len){
		int sent_cnt =
			send(skd->fd, skd->send_buf + skd->pos_send, skd->snd_len, 0);
		if(sent_cnt < 0){
			if(sent_cnt == 0 || errno == EAGAIN){
				#ifdef DBG
					dbgPrintf(0, "Try to read next time\n");
				#endif
				return 0; // try later
			}
			skd->continue_listen = 0;
			#ifdef DBG
				dbgPrintf(-1, "Error writing %d\n", sent_cnt);
			#endif
			return -1;
		}
		#ifdef DBG
			dbgPrintf(0, "Sent %d bytes\n", sent_cnt);
		#endif
		
		skd->pos_send += sent_cnt;
	}
	
	if(skd->pos_send >= skd->snd_len){
		#ifdef DBG
			dbgPrintf(-1, "Sent all data. Remove socket from epoll out events.\n");
		#endif
		
		skd->snd_len = 0;
		skd->state = SInit;
		
		// remove from epoll out events
		return 1;
	}
	
	#ifdef DBG
		dbgPrintf(-1, "-writeClient(try to do additional write )\n");
	#endif
		
	return 0;
}


struct str_sorter_server* createServer(const char* ip_str, const int port){
	#ifdef DBG
		dbgPrintf(1, "+createServer(port %d)\n", port);
	#endif
	
	struct str_sorter_server* sss =
		(struct str_sorter_server*)malloc(sizeof(struct str_sorter_server));

	sss->end_msg = OFF_MSG;
	sss->stop_msg = STOP_MSG;
	sss->end_msg_len = strlen(sss->end_msg);
	sss->stop_msg_len = strlen(sss->stop_msg);
	sss->sort_end = UCHAR_MAX;
	
	sss->epfd = 0;
	sss->events = NULL;
	sss->maxevents = MAX_EVENTS;
	
	sss->port = port;
	sss->fd = 0;
	
	memset(&(sss->addr), 0, sizeof(struct sockaddr_in));
	(sss->addr).sin_family = AF_INET;
	(sss->addr).sin_port = htons((unsigned short)(sss->port));
	
	if(NULL != ip_str){
		if(inet_aton(ip_str,
				(struct in_addr*)&((sss->addr).sin_addr.s_addr)) == 0){
        	#ifdef DBG
				dbgPrintf(-1, "Error: invalid address: %s\n", ip_str);
			#endif
        	free(sss);
        	return NULL;
    	}
	}
	else
		(sss->addr).sin_addr.s_addr = INADDR_ANY;
	
	sss->addr_len = sizeof(sss->addr);
	
	sss->dlist = NULL;
	
	#ifdef DBG
		dbgPrintf(0, "Address: %s:%d\n", ip_str, port);
		dbgPrintf(0, "End client message: %s.\n", sss->end_msg);
		dbgPrintf(0, "Stop server message: %s.\n", sss->stop_msg);
		dbgPrintf(0, "Sort end: %lu\n", sss->sort_end);
	#endif
	
	#ifdef DBG
		dbgPrintf(-1, "-createServer\n");
	#endif
	
	return sss;
}


int runServer(struct str_sorter_server* sss){
	#ifdef DBG
		dbgPrintf(1, "runServer\n");
	#endif
	
	if(NULL == sss){
		#ifdef DBG
			dbgPrintf(-1, "Error: server has not been created\n");
		#else
			printf("Error: server has not been created\n");
		#endif

		return -1;
	}
	
	if(NULL != sss->events || sss->epfd != 0 || sss->fd != 0){
		#ifdef DBG
			dbgPrintf(-1, "Error: server has been run\n");
		#else
			printf("Error: server has been run\n");
		#endif
		
		return -1;
	}
	
	sss->fd = socket(AF_INET, SOCK_STREAM, 0);
	if(sss->fd < 0){
		#ifdef DBG
			dbgPrintf(-1, "Error connection socket\n");
		#else
			printf("Error connection socket\n");
		#endif

		return -1;
	}

	if(bind(sss->fd, (struct sockaddr *) &(sss->addr), sss->addr_len) < 0){
		#ifdef DBG
			dbgPrintf(-1, "Error on binding\n");
		#else
			printf("Error on binding\n");
		#endif
		
		return -1;
	}
		
	set_nonblock(sss->fd);
	
	sss->epfd = epoll_create(10);
	if(sss->epfd < 0){
		#ifdef DBG
			dbgPrintf(-1, "Error epoll create\n");
		#else
			printf("Error epoll create\n");
		#endif
		return -1;
	}
	
	#ifdef DBG
		dbgPrintf(0, "Epoll create\n");
	#endif
	
	// add server description to epoll
	struct epoll_event event;
	event.events = EPOLLIN;
	event.data.ptr = sss;
	if(epoll_ctl(sss->epfd, EPOLL_CTL_ADD, sss->fd, &event)){
		#ifdef DBG
			dbgPrintf(-1, "Error addint socket descriptor to epoll\n");
		#else
			printf("Error addint socket descriptor to epoll\n");
		#endif
		
		return -1;
	}
	
	#ifdef DBG
		dbgPrintf(0, "Created and added server socket\n");
	#endif
	
	if(listen(sss->fd, SOMAXCONN) < 0){
		#ifdef DBG
			dbgPrintf(-1, "Error listening\n");
		#else
			printf("Error listening\n");
		#endif
		
		return -1;
	}
	
	#ifdef DBG
		dbgPrintf(0, "Started to listen\n");
	#endif
	
	// create array for epoll events
	sss->events =
		(struct epoll_event*)malloc(sss->maxevents * sizeof(struct epoll_event));
	if(NULL == sss->events){	
		#ifdef DBG
			dbgPrintf(-1, "Error memory allocation for events array\n");
		#else
			printf("Error memory allocation for events array\n");
		#endif
		
		return -1;
	}
	
	// used in main circle
	struct sockaddr_in cl_addr;
	socklen_t cl_addr_len = sizeof(struct sockaddr_in);
	int fd;
	struct socket_descr* skd = NULL;
	int res = 0;
	int we_need_sort = 0; // exists sort context for feather sort
	int wait_timeout = WAIT_TIMEOUT;
	
	#ifdef DBG
		dbgPrintf(0, "Start server circle\n");
	#endif
	
	while(1){
		wait_timeout = we_need_sort == 0 ? WAIT_TIMEOUT : 0;
		#ifdef DBG
			dbgPrintf(0, "Epoll_wait timeout = %d\n", wait_timeout);
		#endif
		
		int num = epoll_wait(sss->epfd, sss->events,
			sss->maxevents, wait_timeout);
		if(num < 0){
			#ifdef DBG
				dbgPrintf(-1, "Epoll wait\n");
			#else
				printf("Epoll wait\n");
			#endif
			
			return -1;
		}
		
		#ifdef DBG
			dbgPrintf(0, "Get %d events from epoll_wait() call\n", num);
		#endif
		
		// process events
		for(int i = 0; i < num; ++i){
			if(sss->events[i].data.ptr == sss) {
				// new client
				#ifdef DBG
					dbgPrintf(0, "New incoming connection. Try to accept it\n");
				#endif
				
				fd = accept(sss->fd, (struct sockaddr*)&cl_addr, &cl_addr_len);
				if(fd < 0){
					perror("Error accepting incoming connection");
				}
				else{
					#ifdef DBG
						dbgPrintf(0, "Accepted descriptor %d\n", fd);
					#endif
					
					set_nonblock(fd);
					
					// add server description to dlist
					skd = addSocketDescr(sss, fd, &cl_addr, cl_addr_len);
					if(NULL == skd) continue;
					
					// add epoll wait for read from client
					setEventType(sss, EPOLLIN, skd, ETAdd);
				}
			}
			else {
				skd = (struct socket_descr*)((sss->events)[i].data.ptr);
				fd = skd->fd;
				
				#ifdef DBG
					dbgPrintf(0, "Epoll event %d\n", sss->events[i].events);
				#endif
				
				// Write events
				if(sss->events[i].events & EPOLLOUT){
					#ifdef DBG
						dbgPrintf(0, "Try to write to socket %d\n", fd);
					#endif
					
					// client ready to read data from us
					res = writeClient(sss, skd);
					if(res > 0) setEventType(sss, EPOLLOUT, skd, ETRemove);
					else if(res < 0){ // error
						#ifdef DBG
							dbgPrintf(0, "Error writing to socket #%d. Remove it.\n", fd);
						#endif
						
						removeSocketDescr(sss, skd);
					}
				}
				
				// Read events
				if((sss->events)[i].events & EPOLLIN){
					#ifdef DBG
						dbgPrintf(0, "Try to read from socket %d\n", fd);
					#endif
					
					// client ready to send us a data
					res = readClient(sss, skd);
					if(res != 0){ // eof socket or error
						#ifdef DBG
							if(res < 0)
								dbgPrintf(0, "Error reading socket #%d. Remove it.\n", fd);
						#endif
						
						removeSocketDescr(sss, skd);
					}
				}
			}
		}
		
		// sort client buffers or fill the buffers to send
		res = 0; // reset 
		we_need_sort = 0;
		struct node* ptr = sss->dlist;
		while(ptr != NULL) {
			skd = (struct socket_descr*)ptr->data;
			if(skd->fd != sss->fd) {
				// it is not the server socket context
				#ifdef DBG
					dbgPrintf(0, "Start process of client socket %d\n", skd->fd);
				#endif
				
			  	res = processClient(sss, skd);
			  	if(res < 0){
			  		// received STOP signal. Stop server.
			  		#ifdef DBG
						dbgPrintf(0, "Receiev STOP signal. End work.\n");
					#endif
					
			  		break;
			  	} 
			  	if(res > 0){
			  		// received OFF signal. Close client connection.
			  		#ifdef DBG
						dbgPrintf(0,
							"Client send OFF signal. Close client connection\n");
					#endif
			  		
			  		ptr = removeSocketDescr(sss, skd);
			  	}
			  	// all right
			  	if(skd->state == SSendBuf)
			  		// add socket to epoll out events
			  		setEventType(sss, EPOLLOUT, skd, ETModify);
			  	else if(skd->state != SReadToBuf)
			  		//we will not wait for epoll events cose we have enough work
			  		we_need_sort=1;

			}
		  	if(ptr) ptr = ptr->next;
		}
		
		#ifdef DBG
			dbgPrintf(0, "Finish process of all clients\n");
		#endif
		
		if(res < 0) break; // received STOP signal
	}

	#ifdef DBG
		dbgPrintf(-1, "-runServer\n");
	#endif
	
    return 0;
}


void removeServer(struct str_sorter_server* sss){
	#ifdef DBG
		dbgPrintf(1, "+removeServer\n");
	#endif
	
	if(NULL != sss){
		while(NULL != sss->dlist){
			removeSocketDescr(sss, (struct socket_descr*)(sss->dlist->data));
		}
		
		shutdown(sss->fd, SHUT_RDWR);
		close(sss->fd);
		
		close(sss->epfd);
		
		if(sss->events) free(sss->events);
		free(sss);
	}

	#ifdef DBG
		dbgPrintf(-1, "-removeServer\n");
	#endif
}


int main(int argc, char** argv){
	
	if(argc < 2){
		printf("Use: tcp_string_sorter [ip adress] port\n");
		return -1;
	}
	
	int port = 0;
	char* address = NULL;
	
	if(argc == 2) port = atoi(argv[1]);
	else{
		address = argv[1];
		port = atoi(argv[2]);
	}
	
	struct str_sorter_server* sss = createServer(address, port);
	if(NULL == sss) return -1;
	int res = runServer(sss);
	removeServer(sss);
	
	return res;
}
