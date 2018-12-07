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
#include <pthread.h>
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

int read_req(int fd, int *keepalive, int tid) 
{
	char readbuf[4096] = {0};
	int  rlen;
   
	rlen = read(fd, readbuf, 1024);
	//printf("readlen: %d\n", rlen);
	if (rlen <= 0) {
		LOG("%d read:%d\n", tid, rlen);
		return rlen;
	}

	readbuf[rlen] = 0;
	//LOG("read:|%d|%d|%s|\n\n", fd, rlen, readbuf);
	LOG("%d read:|%d|%d|\n", tid, fd, rlen);

	if (strstr(readbuf, "Keep-Alive") != NULL || strstr(readbuf, "keep-alive") != NULL) {
		*keepalive = 1;
	}else{
		*keepalive = 0;
	}

	return rlen;
}

int write_resp(int fd, int keepalive, int tid)
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
		LOG("%d write:%d\n", tid, ret);
		return ret;
	}
	//LOG("write:|%d|%d|%s|\n\n", fd, ret, resp2);
	LOG("%d write:|%d|%d|\n", tid, fd, ret);
	
	return resplen;
}


void* run(void *args)
{
	int keepalive = 1;
	int new_fd = (int)args;
	int ret;
	int tid = (int)pthread_self();

	while (1) {	
		ret = read_req(new_fd, &keepalive, tid);
		if (ret <= 0) {
			LOG("%d read close fd %d\n", tid, new_fd);
			close(new_fd);
			break;
		}

		if (SLEEP_TIME > 0) {
			usleep(SLEEP_TIME);
		}
		ret = write_resp(new_fd, keepalive, tid);
		if (ret < 0) {
			LOG("%d write close fd %d\n", tid, new_fd);
			close(new_fd);
			break;
		}

		// not keep-alive
		if (!keepalive) {
			close(new_fd);
			LOG("%d close fd %d\n", tid, new_fd);
			break;
		}
	}
}

 
int main()
{
	int sockfd, new_fd;
	struct sockaddr_in my_addr;
	
	LOG("thread\n");

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


 	while (1) {
		struct sockaddr_in remote_addr;
    	socklen_t sin_size = sizeof(struct sockaddr_in);
    	new_fd = accept(sockfd, (struct sockaddr*)&remote_addr, &sin_size);
		LOG("newfd: %d\n", new_fd);

    	if (new_fd == -1){
    		LOG("receive failed\n");
			continue;
		}
		
		struct timeval tv; 
		tv.tv_sec = 1; 
		tv.tv_usec = 0;
		ret = setsockopt(new_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
		if (ret < 0) {
			LOG("set recv timeout error:%s", strerror(errno));
			return -2;
		}

		pthread_t	pt;
		pthread_attr_t attr;

		ret = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		if (ret < 0) {
			LOG("create thread attr error: %s", strerror(errno));
			return -3;
		}

		ret = pthread_create(&pt, &attr, run, (void*)new_fd);
		if (ret < 0) {
			LOG("thread create error:%s", strerror(errno));
			return -4;
		}
	
	}
	return 0;
} 


