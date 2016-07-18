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
	#ifdef DBG
		printf("AddSocketDescr #%d\n", fd);
	#endif
	
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
	#ifdef DBG
		printf("Add event #%d with code %d\n", skd->fd, event_code);
	#endif
	
	struct epoll_event event;
	event.events = event_code;
	event.data.ptr = skd;
	
	push(head, skd->fd, skd);
	
	return epoll_ctl(epfd, EPOLL_CTL_ADD, skd->fd, &event);
}


struct node* RemoveSocket(struct node** head, int epfd, struct socket_descr* skd){
	#ifdef DBG
		printf("Removing socket #%d\n", skd->fd);
	#endif
	
	int fd = skd->fd;
	
	#ifdef DBG
		printf("Try to delete event from eopll\n");
	#endif
	
	struct epoll_event event; // cpoll_ctl(DEL) ignored event content but needs it
	event.events = EPOLLIN;
	epoll_ctl(epfd, EPOLL_CTL_DEL, fd, &event);
	
	#ifdef DBG
		printf("Close soket\n");
	#endif
		
	shutdown(fd, SHUT_RDWR);
	close(fd);
	
	
	if(NULL != skd->context){
		#ifdef DBG
			printf("Try to delete context\n");
		#endif
		
		free(skd->context);
	}
	#ifdef DBG
		printf("Try to delete socket description\n");
	#endif
	
	free(skd);
	
	#ifdef DBG
		printf("Try to delete description from dlist\n");
	#endif
	
	struct node* next = NULL;
	struct node* deleted = deleteNode(head, fd);
	if(NULL != deleted){
		#ifdef DBG
			printf("Try to delete list item\n");
		#endif
		
		next = deleted->next;
		free(deleted);
	}
	
	return next;
}


void RemoveAllConnections(int epfd, struct node** head){
	#ifdef DBG
		printf("Try to remove all connections\n");
	#endif
	
	while(NULL != *head){
		RemoveSocket(head, epfd, (struct socket_descr*)((*head)->data));
	}
	
	#ifdef DBG
		printf("Close epoll descriptor\n");
	#endif
	
	close(epfd);
}


// process client buffers (sort of fill for send)
// stop working, if recieve "OFF" string
// return:
//	0 if server need to continue listen to new clients
//	< 0 if server need to end working (received STOP message)
int process_client(struct socket_descr* skd){
	struct sort_context* sc = skd->context;
	#ifdef DBG
		printf("Process socket %d. Sort end %d; sort size %lu\n", skd->fd, sort_end, sizeof(sc->sort));
	#endif
	
	if(!sc->continue_listen) return -1; // close client

	switch(sc->state){
	case SInit: // 0 init data to parse new string in buffer
		#ifdef DBG
			printf("SInit\n");
		#endif
		
		memset(sc->sort, 0, (sort_end+1) * sizeof(unsigned int));
		if(sc->buf_idx < sc->rb) sc->state = SCheckEnd;
		else sc->state = SReadToBuf;
		sc->flStartOfTheString = 1;
		break;
		
	case SCheckEnd: // 2 check if received control message
		#ifdef DBG
			printf("SCheckEnd\n");
		#endif
		
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
		break;

	case SSortBuf: // 3 sort data in buf
		#ifdef DBG
			printf("SSortBuf\n");
		#endif
		
		while(sc->buf_idx < sc->rb){
			if(sc->buf[sc->buf_idx] == '\0') {
				#ifdef DBG
					printf("Find zero char\n");
				#endif
				
				sc->buf_idx++;
				sc->sort_idx = sort_end;
				sc->snd_len = 0;
				sc->state = SFillBufFromSort;
				break;
			}
			
			unsigned code = (unsigned)(sc->buf[sc->buf_idx]);
			
			#ifdef DBG
				printf("  Got code %u, for sort %u\n", code, sc->sort[code]);
			#endif
			
			sc->sort[code]++;
			
			#ifdef DBG
				printf("  Set sort to %u\n", sc->sort[code]);
			#endif
				
			sc->buf_idx++;
		}
		if(sc->state == SFillBufFromSort) break;
		
		#ifdef DBG
			printf("Not find zero. We need to read again\n");
		#endif
		
		sc->flStartOfTheString = 0;
		sc->state = SReadToBuf;
		break;
		
	case SFillBufFromSort: // 4 fill bufer to send
		#ifdef DBG
			printf("SFillBufFromSort\n");
		#endif
		
		while(sc->sort_idx > 0 && sc->snd_len < MAX_BUF){
			unsigned int cnt = sc->sort[sc->sort_idx];
			if(cnt) {
				#ifdef DBG
					printf("    Found char %c with count %u\n",
							(unsigned char)(sc->sort_idx), cnt);
				#endif
				
				if(sc->snd_len+cnt >= MAX_BUF-1){
					cnt = MAX_BUF-1 - sc->snd_len;
					sc->sort[sc->sort_idx] -= cnt;
				}
				
				memset(sc->send_buf+sc->snd_len,
					(unsigned char)(sc->sort_idx), cnt);
				sc->snd_len += cnt;
			}
			sc->sort_idx--;
		}
		
		#ifdef DBG
			printf("We filled sort array. Send it.\n");
		#endif
		
		if(sc->snd_len < MAX_BUF) sc->send_buf[sc->snd_len] = '\0';
		sc->state = SSendBuf;
		sc->pos_send = 0;
		
		break;
	}
	
	#ifdef DBG
		printf("Finish process with state %d\n", sc->state);
	#endif
	
	return 0;
}


int process_client_read(struct socket_descr* skd){
	struct sort_context* sc = skd->context;
	#ifdef DBG
		printf("process_client_read #%d, continue flag %d, state: %d\n",
			skd->fd, sc->continue_listen, sc->state);
	#endif
	
	if(!sc->continue_listen) return -1; // close client
	
	if(sc->state != SReadToBuf) return 0; // it should do nothing
	
	// read data from socket
	#ifdef DBG
		printf("SReadToBuf\n");
	#endif
	
	int rb = recv(skd->fd, sc->buf, MAX_BUF, 0);
	if(rb <= 0) {
		if(rb == 0 || errno == EAGAIN){
			#ifdef DBG
				printf("Try read again\n");
			#endif
			return 0; // try later
		}
		sc->continue_listen = 0;
		#ifdef DBG
			printf("Error readin\n");
		#endif
		
		return -1; // error. close connection
	}
	#ifdef DBG
		printf("Read %d bytes\n", rb);
	#endif
	
	sc->rb = rb;
	sc->buf_idx = 0;
	if(sc->flStartOfTheString) sc->state = SCheckEnd;
	else sc->state = SSortBuf;
	
	#ifdef DBG
		printf("Change to state %d \n", sc->state);
	#endif
	
	return 0;
}


int process_client_write(struct socket_descr* skd){
	struct sort_context* sc = skd->context;
	#ifdef DBG
		printf("process_client_write #%d, continue flag %d, state: %d\n",
			skd->fd, sc->continue_listen, sc->state);
	#endif
	
	if(!sc->continue_listen) return -1; // close client
	
	if(sc->state != SSendBuf) return 0; // it should do nothing
	
	// send send_buf to client
	#ifdef DBG
		printf("SSendBuf\n");
	#endif
	
	if(sc->pos_send < sc->snd_len){
		int sent_cnt = send(skd->fd, sc->send_buf + sc->pos_send, sc->snd_len, 0);
		if(sent_cnt < 0){
			if(sent_cnt == 0 || errno == EAGAIN) return 0; // try later
			sc->continue_listen = 0;
			return -1;
		}
		#ifdef DBG
			printf("Sent %d bytes\n", sent_cnt);
		#endif
		
		sc->pos_send += sent_cnt;
	}
	
	if(sc->pos_send >= sc->snd_len){
		#ifdef DBG
			printf("Sent all data\n");
		#endif
		
		sc->snd_len = 0;
		sc->state = SInit;
	}
	return 0;
}


int runServer(int port){
	struct sockaddr_in serveraddr;
	
	#ifdef DBG
		printf("Server works on port %d\n", port);
	#endif
	
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
	
	#ifdef DBG
		printf("Epoll create\n");
	#endif
	
	// add server description to epoll
	struct socket_descr* server_skd =
		AddSocketDescr(sfd, &serveraddr, sizeof(serveraddr), 0);
	AddEvent(&dlist, epfd, EPOLLIN, server_skd);
	
	#ifdef DBG
		printf("Created and added server socket\n");
	#endif
	
	if(listen(sfd, SOMAXCONN) < 0){
		perror("Error listening");
		return -1;
	}
	
	#ifdef DBG
		printf("Started to listen\n");
	#endif
	
	// used in while
	struct sockaddr_in cl_addr; 
	socklen_t cl_addr_len;
	struct socket_descr* skd = NULL;
	int res = 0;
	int clientsForProcess = 0;

	#ifdef DBG
		printf("Start server cicle\n");
	#endif
	
	while(1){
		int wait_timeout = clientsForProcess > 0 ? WAIT_TIMEOUT : -1;
		int num = epoll_wait(epfd, events, MAX_EVENTS, wait_timeout);
		if(num < 0){
			perror("Epoll wait");
			return -1;
		}
		
		#ifdef DBG
			printf("Get %d events from epoll_wait() call\n", num);
		#endif
		
		// process events
		for(int i = 0; i < num; ++i){
			skd = (struct socket_descr*)events[i].data.ptr;
			int fd = skd->fd;
			if(fd == sfd) {
				// new client
				#ifdef DBG
					printf("New incoming connection. Try to accept it\n");
				#endif
				
				cl_addr_len = sizeof(struct sockaddr_in);
				fd = accept(sfd, (struct sockaddr*)&cl_addr, &cl_addr_len);
				if(fd < 0){
					perror("Error accepting incoming connection");
				}
				else{
					#ifdef DBG
						printf("Get incoming connection descriptor %d\n", fd);
					#endif
					
					set_nonblock(fd);
					
					// add server description to dlist
					skd = AddSocketDescr(fd, &cl_addr, cl_addr_len, 1);
					// add epoll wait for read from client
					AddEvent(&dlist, epfd, EPOLLIN + EPOLLOUT, skd);
				}
			}
			else {
				#ifdef DBG
					printf("Epoll event %d\n", events[i].events);
				#endif
				
				if(events[i].events & EPOLLIN){
					#ifdef DBG
						printf("Try to read from socket %d\n", fd);
					#endif
					
					// client ready to send us a data
					res = process_client_read(skd);
					if(res < 0){ // error
						if(DBG)
							printf("Error reading socket #%d. Remove it.\n", fd);
						RemoveSocket(&dlist, epfd, skd);
					}
				}
				if(events[i].events & EPOLLOUT){
					#ifdef DBG
						printf("Try to write to socket %d\n", fd);
					#endif
					
					// client ready to read data from us
					res = process_client_write(skd);
					if(res < 0){ // error
						if(DBG)
							printf("Error writing to socket #%d. Remove it.\n", fd);
						RemoveSocket(&dlist, epfd, skd);
					}
				}
			}
		}
		
		// sort client buffers or fill the buffers to send
		struct node* ptr = dlist;
		clientsForProcess = 0; // we will calculate count of clients to further process
		while(ptr != NULL) {
			skd = (struct socket_descr*)ptr->data;
			if(skd->fd != sfd) {
				// it is not the server socket context
				#ifdef DBG
					printf("Try sort client descriptor %d\n", skd->fd);
				#endif
				
			  	res = process_client(skd);
			  	if(res < 0){
			  		// received STOP signal. Stop server.
			  		#ifdef DBG
						printf("Receiev STOP signal. End work.\n");
					#endif
					
			  		break;
			  	} 
			  	if(res > 0){
			  		// received OFF signal. Close client connection.
			  		#ifdef DBG
						printf("Client send OFF signal. Close client connection\n");
					#endif
			  		
			  		ptr = RemoveSocket(&dlist, epfd, skd);
			  	}
			  	else {
			  		int state = skd->context->state;
			  		if(state != SReadToBuf && state != SSendBuf)
			  			clientsForProcess++;
			  		//else
			  		// 	if(state == SSendBuf){
			  		// 		#ifdef DBG
					//			printf("Add socket #%d to epoll\n", skd->fd);
					//		#endif
					//		AddEvent(&dlist, epfd, EPOLLOUT, skd);
			  		// 	}
			  	}
			}
		  	ptr = ptr->next;
		}
		
		#ifdef DBG
			printf("Finish process all clients\n");
		#endif
		
		if(res < 0) break; // received STOP signal
	}
	
	#ifdef DBG
		printf("Close server\n");
	#endif
	
	RemoveAllConnections(epfd, &dlist);
	
	#ifdef DBG
		printf("Free events array\n");
	#endif
	
	free(events);
	
	#ifdef DBG
		printf("Server stoped working\n");
	#endif
	
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