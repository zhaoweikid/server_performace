#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <linux/if.h>
#include <linux/tcp.h>
#include <sys/un.h>
#include <time.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include "config.h"

#define PORT  20000

//#define LOG(format,args...) fprintf(stderr, "%u %s:%d " format, (int)time(NULL), __FILE__, __LINE__, ##args)
#define LOG(format,args...) fprintf(stderr, "%u %04d " format, (int)time(NULL), __LINE__, ##args)

int make_date(char *buffer, int buflen)
{
	time_t timep;   
    struct tm * timeinfo;

    time(&timep);
    timeinfo = gmtime (&timep);

    strftime (buffer, buflen, "%a, %d %b %Y %H:%M:%S GMT", timeinfo);
	
	return 0;
}

void setnonblocking(int sock)
{
    int opts;
    opts = fcntl(sock,F_GETFL);
    if (opts < 0) {
        perror("fcntl(sock,GETFL)");
        exit(1);
    }
    opts = opts|O_NONBLOCK;
    if (fcntl(sock, F_SETFL, opts) < 0) {
        perror("fcntl(sock,SETFL,opts)");
        exit(1);
    }
}

int read_req(int fd, int *keepalive) 
{
	char readbuf[4096] = {0};
	int  rlen;
   
	rlen = read(fd, readbuf, 1024);
	//printf("readlen: %d\n", rlen);
	if (rlen <= 0) {
		LOG("%d read:%d\n", fd, rlen);
		return rlen;
	}

	readbuf[rlen] = 0;
	//LOG("read:|%d|%d|%s|\n\n", fd, rlen, readbuf);
	LOG("%d read:|%d|\n", fd, rlen);

	if (strstr(readbuf, "Keep-Alive") != NULL || strstr(readbuf, "keep-alive") != NULL) {
		*keepalive = 1;
	}else{
		*keepalive = 0;
	}

	return rlen;
}



int write_resp(int fd, int keepalive)
{
	char *resp0 = "HTTP/1.1 200 OK\r\nServer: c\r\nContent-Type: text/html;charset=utf-8\r\nContent-Length: 6\r\nConnection: close\r\nDate: %s\r\n\r\nGO%04d";
	char *resp1 = "HTTP/1.1 200 OK\r\nServer: c\r\nContent-Type: text/html;charset=utf-8\r\nContent-Length: 6\r\nConnection: Keep-Alive\r\nDate: %s\r\n\r\nGO%04d";

	char datebuf[128] = {0};
	make_date(datebuf, sizeof(datebuf));
	//printf("date: %s\n", datebuf);
	int n = random() % 10000;

	char resp2[1024] = {0};

	if (keepalive) {
		sprintf(resp2, resp1, datebuf, n);
	}else{
		sprintf(resp2, resp0, datebuf, n);
	}
	int resplen = strlen(resp2);

	int ret = write(fd, resp2, resplen);
	if (ret < 0) {
		LOG("%d write %d\n", fd, ret);
		return ret;
	}
	//LOG("write:|%d|%d|%s|\n\n", fd, ret, resp2);
	LOG("%d write:|%d|\n", fd, ret);
	
	return resplen;
}

 
int main()
{
	int sockfd, new_fd;
	struct sockaddr_in my_addr;
	
	LOG("multiplex\n");

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd == -1){
		LOG("socket failed: %d\n",errno);
		return -1;
	}
	my_addr.sin_family = AF_INET;
	my_addr.sin_port   = htons(PORT);
	my_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	bzero(&(my_addr.sin_zero),8);

	int n = 1;
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const void*)&n, sizeof(int)) == -1) {
		LOG("set socket addr %d reuse error! %s\n", sockfd, strerror(errno));
		return -1;
	}

	if (bind(sockfd, (struct sockaddr*)&my_addr, sizeof(struct sockaddr)) < 0){
		LOG("bind error\n");
		return -1;
	}
    
	listen(sockfd, 1024);

	struct linger ling = {0, 0}; 
	int ret = setsockopt(sockfd, SOL_SOCKET, SO_LINGER, (void *)&ling, sizeof(ling));
	if (ret != 0) { 
		LOG("setsockopt LINGER error: %s\n",  strerror(errno));
		return -1; 
	}

	int flag = 1; 
	if (setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(int)) == -1) {
		LOG("set_tcp_nodelay error! %s\n", strerror(errno));
		return -1;
	}

	//char *resp = "HTTP/1.1 200 OK\r\nServer: cserver\r\nContent-Type: text/html;charset=utf-8\r\nContent-Length: 2\r\nConnection: close\r\n\r\nGO";
	//int  resplen = strlen(resp);

	int keepalive = 1;

	struct epoll_event ev, events[20];
	int epfd = epoll_create(256);

	ev.data.fd = sockfd;
	//ev.events = EPOLLIN | EPOLLET;	
	ev.events = EPOLLIN;	
	epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev);

	int8_t avail;
	int nfds;
	while (1) {
		nfds = epoll_wait(epfd, events, 20, 1000);

		for (int i=0; i<nfds; i++) {
			int myfd = events[i].data.fd;
			if (events[i].data.fd == sockfd) {
				struct sockaddr_in client_addr;
				socklen_t sin_size = sizeof(struct sockaddr_in);
				new_fd = accept(sockfd, (struct sockaddr*)&client_addr, &sin_size);
				LOG("%d newfd\n", new_fd);
				
				setnonblocking(new_fd);

				ev.data.fd = new_fd;
				ev.events = EPOLLIN|EPOLLET;
				
				epoll_ctl(epfd, EPOLL_CTL_ADD, new_fd, &ev);
			}else if (events[i].events & EPOLLIN) {
				LOG("%d pollin\n", myfd);

				//ioctl(myfd, FIONREAD, &avail);
				//LOG("%d read buffer:%d\n", myfd, avail);
	
				ret = read_req(myfd, &keepalive);
				if (ret <= 0) {
					ev.data.fd = myfd;
					ev.events = EPOLLIN | EPOLLET;
					epoll_ctl(epfd, EPOLL_CTL_DEL, myfd, &ev);
					close(myfd);
					LOG("%d close fd\n", myfd);
					continue;
				}


		
				if (SLEEP_TIME > 0) {
					usleep(SLEEP_TIME);
				}
				ret = write_resp(myfd, keepalive);
				if (ret < 0 || !keepalive) {
					ev.data.fd = myfd;
					ev.events = EPOLLIN | EPOLLET;
					epoll_ctl(epfd, EPOLL_CTL_DEL, myfd, &ev);
					close(myfd);
					LOG("%d close fd\n", myfd);
				}


				//ioctl(myfd, FIONREAD, &avail);
				//LOG("%d read buffer:%d\n", myfd, avail);

			}else if (events[i].events & EPOLLOUT) {
				LOG("%d pollout\n", myfd);
				/*ret = write_resp(myfd, keepalive);
				if (ret < 0) {
					ev.data.fd = myfd;
					ev.events = EPOLLIN | EPOLLET;
					epoll_ctl(epfd, EPOLL_CTL_DEL, myfd, &ev);
				}*/
			}
		}
	}
		

	return 0;
} 


