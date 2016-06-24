/*Разработайте TCP сервер, предназначенный для сортировки символов
в строке. Сервер получает на вход строки, оканчивающиеся
символом '\0', сортирует в них символы в порядке убывания ASCII-кодов
и отсылает обратно на клиент, в виде строки заканчивающейся '\0'.
Завершение работы сервера происходит после получения строки,
содержащей только 'OFF'. 

При старте сервер получает на вход номер порта в качестве параметра
командной строки. bind производится на адресе 127.0.0.1

Пример вызова
./solution 13555
*/

#define _POSIX_C_SOURCE 199310L

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


#define MAX_BUF 512
#define DBG 0

enum States {
	  SInit // init data to read
	, SReadToBuf // 1 read data from 
	, SCheckEnd // 1.5 check if recieved "OFF" message
	, SSortBuf // 2 sort data in buf
	, SFillBufFomSort // 3 fill bufer to send
	, SSendBuf // 5 send buf to client
};

// process client socket (cfd)
// recieve data from socket, to \0.
// sort them and send to client.
// stop working, if recieve "OFF" string
// return:
//	0 if server need to continue listen to new clients
//	1 if server need to end working
int process_client(int cfd){
	int proc_res = 0;
	const char* end_msg = "OFF";
	int end_msg_len = strlen(end_msg);
	int sort_end = UCHAR_MAX;
	unsigned int sort[UCHAR_MAX+1];
	char buf[MAX_BUF];
	char send_buf[MAX_BUF];
	
	int buf_idx = 0; // index in buffer, used while sort data
	int	sort_idx = 0; // index in sort, used while prepare send_buf to send
	int snd_len = 0; // length of the send buffer
	ssize_t rb = 0;
	int state = SInit;
	int flStartOfTheString = 1;
	
	if(DBG) printf("Sort end %d; sort size %lu\n", sort_end, sizeof(sort));
	
	int continue_listen = 1;
	while(continue_listen){
		switch(state){
		case SInit: // init data to parce new string in buffer
			if(DBG) printf("SInit\n");
			memset(sort, 0, (sort_end+1) * sizeof(unsigned int));
			if(buf_idx < rb) state = SCheckEnd;
			else state = SReadToBuf;
			flStartOfTheString = 1;
			break;
			
		case SCheckEnd: // check if received OFF message
			if(DBG) printf("SCheckEnd\n");
			if(buf_idx >= rb)
				state = SReadToBuf;
			else{
				if(memcmp(buf+buf_idx, end_msg, end_msg_len) == 0)
					continue_listen = 0;
				else state = SSortBuf;
			}
			flStartOfTheString = 0;
			break;
			
		case SReadToBuf: // 1 read data from 
			if(DBG) printf("SReadToBuf\n");
			rb = recv(cfd, buf, MAX_BUF, 0);
			if(rb > 0){
				buf_idx = 0;
				if(flStartOfTheString){
					state = SCheckEnd;
				}
				else state = SSortBuf;
			}
			else continue_listen = 0;
			break;
			
		case SSortBuf: // 2 sort data in buf
			if(DBG) printf("SSortBuf\n");
			if(buf_idx < rb){
				if(buf[buf_idx] == '\0') {
					buf_idx++;
					sort_idx = sort_end;
					snd_len = 0;
					state = SFillBufFomSort;
				}
				else{
					unsigned code = (unsigned)buf[buf_idx];
					if(DBG) printf("  Got code %u, for sort %u\n", code, sort[code]);
					sort[code]++;
					if(DBG) printf("  Set sort to %u\n", sort[code]);
					buf_idx++;
				}
			}
			else{
				flStartOfTheString = 0;
				state = SReadToBuf;
			}
			break;

			
		case SFillBufFomSort: // 3 fill bufer to send
			if(DBG) printf("SFillBufFomSort\n");
			while(sort_idx > 0 && sort[sort_idx] == 0) sort_idx--;
			if(sort_idx == 0 || snd_len >= MAX_BUF-1){
				send_buf[snd_len] = '\0';
				state = SSendBuf;
			}
			else{
				unsigned int cnt = sort[sort_idx];
				if(cnt){
					if(DBG) printf("    Found char %c with count %u\n", (unsigned char)sort_idx, cnt);
					if(snd_len+cnt >= MAX_BUF-1){
						cnt = MAX_BUF-1 - snd_len;
						sort[sort_idx] -= cnt;
					}
					
					memset(send_buf+snd_len, (unsigned char)sort_idx, cnt);
					snd_len += cnt;
				}
				sort_idx--;
			}
			break;
			
		case SSendBuf: ;// 5 send send_buf to client
			if(DBG) printf("SSendBuf\n");
			int pos_send = 0;
			while(pos_send < snd_len){
				int sent_cnt = send(cfd, send_buf+pos_send, snd_len, 0);
				if(sent_cnt < 0){
					continue_listen = 0;
					break;
				}
				else break;
				pos_send += sent_cnt;
			}
			snd_len = 0;
			state = SInit;
			break;
		}
	}
		
	return proc_res;
}


int main(int argc, char** argv){
	
	if(argc < 2){
		printf("Specify port to listen to\n");
		return -1;
	}
	
	struct sockaddr_in serveraddr;
	int port = atoi(argv[1]);
	int sfd = socket(AF_INET, SOCK_STREAM, 0);
	if(sfd < 0){
		printf("Error connection socket\n");
		return -1;
	}
	
	memset(&serveraddr, 0, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	serveraddr.sin_port = htons((unsigned short)port);

	if(bind(sfd, (struct sockaddr *) &serveraddr, sizeof(serveraddr)) < 0){
		printf("ERROR on binding\n");
		return -1;
	}
	
	struct sockaddr_in cl_addr;
	socklen_t cl_addr_len;
	
	while(1){
		int res = listen(sfd, SOMAXCONN);
		if(res != 0){
			perror("Error listening");
			return -1;
		}
		
		cl_addr_len = sizeof(struct sockaddr_in);
		int cfd = accept(sfd, (struct sockaddr*)&cl_addr, &cl_addr_len);
		if(cfd < 0){
			perror("Error accepting incoming connection");
			break;
		}
		
		if(DBG) printf("Get incoming connection\n");
		res = process_client(cfd);
		close(cfd);
		
		if(!res) break;
	}
	
	close(sfd);
	
    return 0;
}

