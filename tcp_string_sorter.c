#ifndef _POSIX_C_SOURCE
	#define _POSIX_C_SOURCE 199310L
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <limits.h>
#include <errno.h>

#include "lib_solution.h"
#include "tcp_string_sorter.h"

const char* end_msg = "OFF";
const char* stop_msg = "STOP";
int end_msg_len;
int stop_msg_len;
	
struct sort_context* CreateSortContext(){
	struct sort_context* sc = (struct sort_context*)malloc(sizeof(struct sort_context));

	sc->proc_res = 0; // result of processing
	sc->buf_idx = 0; // index in buffer, used while sort data
	sc->sort_idx = 0; // index in sort, used while prepare send_buf to send
	sc->snd_len = 0; // length of the send buffer
	sc->rb = 0; // count of have read bytes
	sc->pos_send = 0; // count of have sent bytes
	sc->state = SInit; // state of poscess
	sc->flStartOfTheString = 1; // it is start of the string
	sc->continue_listen = 1;
	
	return sc;
}


struct socket_descr* AddSocketDescr(int fd, struct sockaddr_in* cl_addr,
			socklen_t cl_addr_len, int create_sort_context)
{
	struct socket_descr* skd = (struct socket_descr*)malloc(sizeof(struct socket_descr));
	if(NULL == skd) return NULL;
	
	skd->fd = fd;
	
	if(NULL != cl_addr) memcpy(&(skd->cl_addr_len), cl_addr, cl_addr_len);
	
	if(create_sort_context)
		skd->context = CreateSortContext();
	else
		skd->context = NULL;
	
	return skd;
}


int AddEvent(struct node** head, int epfd, int event_code, struct socket_descr* skd){
	struct epoll_event event;
	event.events = event_code;
	event.data.ptr = skd;
	
	push(head, skd->fd, skd);
	
	return epoll_ctl(epfd, EPOLL_CTL_ADD, skd->fd, &event);
}


struct node* RemoveSocket(struct node** head, int epfd, struct socket_descr* skd){
	int fd = skd->fd;
	
	struct epoll_event event; // cpoll_ctl(DEL) ignored event content but needs it
	event.events = EPOLLIN;
	epoll_ctl(epfd, EPOLL_CTL_DEL, fd, &event);
	
	shutdown(fd, SHUT_RDWR);
	close(fd);
	
	if(NULL != skd->context) free(skd->context);
	free(skd);
	
	struct node* next = NULL;
	struct node* deleted = deleteNode(head, fd);
	if(NULL != deleted){
		next = deleted->next;
		free(deleted);
	}
	
	return next;
}


void RemoveAllConnections(int epfd, struct node** head){
	while(NULL != *head){
		RemoveSocket(head, epfd, (struct socket_descr*)((*head)->data));
	}
	
	close(epfd);
}


// process client buffers (sort of fill for send)
// stop working, if recieve "OFF" string
// return:
//	0 if server need to continue listen to new clients
//	< 0 if server need to end working (received STOP message)
int process_client(struct socket_descr* skd){
	struct sort_context* sc = skd->context;
	if(DBG) printf("Process socket %d. Sort end %d; sort size %lu\n", skd->fd, sort_end, sizeof(sc->sort));
	
	if(!sc->continue_listen) return -1; // close client

	switch(sc->state){
	case SInit: // init data to parce new string in buffer
		if(DBG) printf("SInit\n");
		memset(sc->sort, 0, (sort_end+1) * sizeof(unsigned int));
		if(sc->buf_idx < sc->rb) sc->state = SCheckEnd;
		else sc->state = SReadToBuf;
		sc->flStartOfTheString = 1;
		return 0;
		
	case SCheckEnd: // check if received control message
		if(DBG) printf("SCheckEnd\n");
		if(sc->buf_idx >= sc->rb)
			// not start of the string
			sc->state = SReadToBuf;
		else{
			if(memcmp((sc->buf)+(sc->buf_idx), end_msg, end_msg_len) == 0){
				sc->continue_listen = 0;
				return 1; // close client
			}
			else if(memcmp((sc->buf)+(sc->buf_idx), stop_msg, stop_msg_len) == 0){
				sc->continue_listen = 0;
				return -1; // stop server
			}
			else sc->state = SSortBuf;
		}
		sc->flStartOfTheString = 0;
		return 0;

	case SSortBuf: // 2 sort data in buf
		if(DBG) printf("SSortBuf\n");
		while(sc->buf_idx < sc->rb){
			if(sc->buf[sc->buf_idx] == '\0') {
				sc->buf_idx++;
				sc->sort_idx = sort_end;
				sc->snd_len = 0;
				sc->state = SFillBufFromSort;
				break;
			}
			
			unsigned code = (unsigned)(sc->buf[sc->buf_idx]);
			if(DBG) printf("  Got code %u, for sort %u\n", code, sc->sort[code]);
			sc->sort[code]++;
			if(DBG) printf("  Set sort to %u\n", sc->sort[code]);
			sc->buf_idx++;
		}
		sc->flStartOfTheString = 0;
		sc->state = SReadToBuf;
		return 0;
		
	case SFillBufFromSort: // 3 fill bufer to send
		if(DBG) printf("SFillBufFromSort\n");
		while(sc->sort_idx > 0 && sc->sort[sc->sort_idx] == 0) sc->sort_idx--;
		if(sc->sort_idx == 0 || sc->snd_len >= MAX_BUF-1){
			sc->send_buf[sc->snd_len] = '\0';
			sc->state = SSendBuf;
			sc->pos_send = 0;
		}
		else{
			unsigned int cnt = sc->sort[sc->sort_idx];
			if(cnt){
				if(DBG) printf("    Found char %c with count %u\n", (unsigned char)(sc->sort_idx), cnt);
				if(sc->snd_len+cnt >= MAX_BUF-1){
					cnt = MAX_BUF-1 - sc->snd_len;
					sc->sort[sc->sort_idx] -= cnt;
				}
				
				memset(sc->send_buf+sc->snd_len, (unsigned char)(sc->sort_idx), cnt);
				sc->snd_len += cnt;
			}
			sc->sort_idx--;
		}
		return 0;
	}
		
	return 0;
}


int process_client_read(struct socket_descr* skd){
	struct sort_context* sc = skd->context;
	if(DBG) printf("We can read from socket %d\n", skd->fd);
	
	if(!sc->continue_listen) return -1; // close client
	
	if(sc->state != SReadToBuf) return 0; // it should do nothing
	
	// read data from socket
	if(DBG) printf("SReadToBuf\n");
	
	int rb = recv(skd->fd, sc->buf, MAX_BUF, 0);
	if(rb <= 0) {
		if(rb == 0 || errno == EAGAIN) return 0; // try later
		sc->continue_listen = 0;
		return -1; // error. close connection
	}
	
	sc->rb = rb;
	sc->buf_idx = 0;
	if(sc->flStartOfTheString){
		sc->state = SCheckEnd;
	}
	else sc->state = SSortBuf;
	
	return 0;
}


int process_client_write(struct socket_descr* skd){
	struct sort_context* sc = skd->context;
	if(DBG) printf("We can write to socket %d\n", skd->fd);
	
	if(!sc->continue_listen) return -1; // close client
	
	if(sc->state != SSendBuf) return 0; // it should do nothing
	
	// send send_buf to client
	if(DBG) printf("SSendBuf\n");
	
	if(sc->pos_send < sc->snd_len){
		int sent_cnt = send(skd->fd, sc->send_buf + sc->pos_send, sc->snd_len, 0);
		if(sent_cnt < 0){
			if(sent_cnt == 0 || errno == EAGAIN) return 0; // try later
			sc->continue_listen = 0;
			return -1;
		}
		sc->pos_send += sent_cnt;
		return 0;
	}
	sc->snd_len = 0;
	sc->state = SInit;
		
	return 0;
}


int runServer(int port){
	struct sockaddr_in serveraddr;
	
	int sfd = socket(AF_INET, SOCK_STREAM, 0);
	if(sfd < 0){
		printf("Error connection socket\n");
		return -1;
	}
	
	memset(&serveraddr, 0, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = INADDR_ANY;
	serveraddr.sin_port = htons((unsigned short)port);

	if(bind(sfd, (struct sockaddr *) &serveraddr, sizeof(serveraddr)) < 0){
		printf("ERROR on binding\n");
		return -1;
	}
		
	set_nonblock(sfd);
	
	int epfd; // epoll descriptor
	struct node* dlist = NULL; // head of socket descriptions
	struct epoll_event* events = (struct epoll_event*)malloc(MAX_EVENTS*sizeof(struct epoll_event)); // buffer to events wait
	
	epfd = epoll_create(8);
	if(epfd < 0){
		perror("Epoll create");
	}
	
	// add server description to epoll
	struct socket_descr* server_skd =
		AddSocketDescr(sfd, &serveraddr, sizeof(serveraddr), 0);
	AddEvent(&dlist, epfd, EPOLLIN, server_skd);
	
	if(listen(sfd, SOMAXCONN) < 0){
		perror("Error listening");
		return -1;
	}
	
	// used in while
	struct sockaddr_in cl_addr; 
	socklen_t cl_addr_len;
	struct socket_descr* skd = NULL;
	int res = 0;
	
	while(1){
		int num = epoll_wait(epfd, events, MAX_EVENTS, WAIT_TIMEOUT);
		if(num < 0){
			perror("Epoll wait");
			return -1;
		}
		
		// process events
		for(int i = 0; i < num; ++i){
			skd = (struct socket_descr*)events[i].data.ptr;
			int fd = skd->fd;
			if(fd == sfd) {
				// new client
				cl_addr_len = sizeof(struct sockaddr_in);
				int cfd = accept(sfd, (struct sockaddr*)&cl_addr, &cl_addr_len);
				if(cfd < 0){
					perror("Error accepting incoming connection");
				}
				else{
					if(DBG) printf("Get incoming connection\n");
					
					set_nonblock(cfd);
					
					// add server description to dlist
					skd = AddSocketDescr(fd, &cl_addr, cl_addr_len, 1);
					// add epoll wait for read from client
					AddEvent(&dlist, epfd, EPOLLIN, skd);
				}
			}
			else {
				if(events[i].events == EPOLLIN){
					// client ready to send us a data
					res = process_client_read(events[i].data.ptr);
					if(res < 0){ // error
						RemoveSocket(&dlist, epfd, events[i].data.ptr);
					}
				}
				else if(events[i].events == EPOLLOUT){
					// client ready to read data from us
					res = process_client_write(events[i].data.ptr);
					if(res < 0){ // error
						RemoveSocket(&dlist, epfd, events[i].data.ptr);
					}
				}
			}
		}
		
		// sort client buffers or fill the buffers to send
		struct node* ptr = dlist;
		while(ptr != NULL) {
		  	res = process_client(ptr->data);
		  	if(res < 0) break; // received STOP signal. Stop server.
		  	if(res > 0){
		  		// received OFF signal. Close client connection.
		  		ptr = RemoveSocket(&dlist, epfd, (struct socket_descr*)ptr->data);
		  	}
		  	else ptr = ptr->next;
		}
		
		if(res < 0) break; // received STOP signal
	}
	
	RemoveAllConnections(epfd, &dlist);
	free(events);
	
    return 0;
}

int main(int argc, char** argv){
	
	if(argc < 2){
		printf("Specify port to listen to\n");
		return -1;
	}
	
	end_msg_len = strlen(end_msg);
	stop_msg_len = strlen(stop_msg);
	
	return runServer(atoi(argv[1]));
}