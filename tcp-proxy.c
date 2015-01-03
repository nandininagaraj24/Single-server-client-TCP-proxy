/*
 * Copyright (C) 2014 Ki Suh Lee (kslee@cs.cornell.edu)
 * 
 * TCP Proxy skeleton.
 * 
 */
 
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <signal.h>
#include <pthread.h>
#include <semaphore.h>
#include "list.h"
#include<fcntl.h>
#include<sys/stat.h>
#define MAX_LISTEN_BACKLOG 5
#define MAX_ADDR_NAME	32
#define ONE_K	1024

/* Data that can pile up to be sent before reading must stop temporarily */
#define MAX_CONN_BACKLOG	(8*ONE_K)
#define GRACE_CONN_BACKLOG	(MAX_CONN_BACKLOG / 2)

/* Watermarks for number of active connections. Lower to 2 for testing */
#define MAX_CONN_HIGH_WATERMARK	(256)
#define MAX_CONN_LOW_WATERMARK	(MAX_CONN_HIGH_WATERMARK - 1)

#define MAX_THREAD_NUM	4

struct sockaddr_in remote_addr; /* The address of the target server */

#define BUF_SIZE 8192
char buf_cs[BUF_SIZE]; /* Buffer for client to server */
char buf_sc[BUF_SIZE]; /* Buffer for server to client */
int flag=0;
int flag_clientEOF=0, flag_serverEOF=0;

void transfer(int *client_fd, int *server_fd, int *read_endcs, int *write_endcs,
			int *read_endsc, int *write_endsc, fd_set *r, fd_set *w)
{
	int ret = 0;
	int buf_len = 0;
	if(*client_fd >0)
	{
		
		/* Reading from the Client */
		if(FD_ISSET(*client_fd,r))
		{
			buf_len = BUF_SIZE-*read_endcs;
			ret = recv(*client_fd,buf_cs+*read_endcs,buf_len,0);
			if(ret < 0)
			{
				
				close(*client_fd);
				*client_fd = -1;
				if(*server_fd>0)
				{
					close(*server_fd);
					*server_fd = -1;
				}
			} 
			
			if(ret==0)
			{
				flag_clientEOF=1;
			}

			if(ret>0)
			{
				*read_endcs = *read_endcs+ret;
			}
		}
		
		/* Client is ready to be read - bufsc */
		if(FD_ISSET(*client_fd,w)){
			buf_len = *read_endsc-*write_endsc;
			ret = send(*client_fd,buf_sc+*write_endsc,buf_len,0);
			if(ret<0){
				
				close(*client_fd);
				*client_fd = -1;
				if(*server_fd>0)
				{
					close(*server_fd);
					*server_fd = -1;
				}
			} 
			if(ret>0)
			{
				*write_endsc = *write_endsc + ret;
			}	
			
		}
	}
	
	if(*server_fd >0)
	{
		
		/* Server is ready to be read -bufsc*/
		if(FD_ISSET(*server_fd,r)){
			buf_len = BUF_SIZE-*read_endsc;
			ret = recv(*server_fd,buf_sc+*read_endsc,buf_len,0);
			if(ret==0)
			{
				close(*server_fd);
				*server_fd=-1;
				flag_serverEOF=1;
				
			}	 

			if(ret < 0)
			{
				if(*client_fd>0)
				{
					close(*client_fd);
					*client_fd=-1;
				}
				close(*server_fd);
				*server_fd = -1;
			} 
			if(ret>0)
			{
				*read_endsc = *read_endsc +ret;
			}
		}	
		
		/* Server is ready to be  written to - bufcs*/
		if(FD_ISSET(*server_fd,w)){
			buf_len = *read_endcs-*write_endcs;
			ret = send(*server_fd,buf_cs+*write_endcs,buf_len,0);
			if(ret<0){
				
				close(*server_fd);
				*server_fd = -1;
				if(*client_fd>0)
				{
					close(*client_fd);
					*client_fd=-1;
				}
			} 
			if(ret>0) 
			{
				*write_endcs = *write_endcs + ret;
			}
		}
	}
	
}

void __loop(int proxy_fd)
{
	struct sockaddr_in client_addr;
	socklen_t addr_size;
	int client_fd = -1, server_fd = -1;
	struct client *client;
	int cur_thread=0;
	struct worker_thread *thread;
	char client_hname[MAX_ADDR_NAME+1];
	char server_hname[MAX_ADDR_NAME+1];
	int read_endcs,write_endcs;
	int read_endsc,write_endsc;
	int return_val=-1;
	
	while(1) 
	{
		int newfd =0,ret;
		fd_set r,w;
		struct timeval tv;
		//10 Second timeout
		tv.tv_sec=10;
		tv.tv_sec=0;
		struct timeval *tv1 = NULL;

		FD_ZERO(&r);
		FD_ZERO(&w);
		flag_clientEOF=0; flag_serverEOF=0;
		
		FD_SET(proxy_fd,&r);
		newfd=proxy_fd;

		// Client - ready to write data - buf is empty 
		if(client_fd > 0 && read_endcs < BUF_SIZE)
		{
			FD_SET(client_fd,&r);
			tv1=NULL;
		}
		// Client - ready to receive data - buf is non-empty
		if(client_fd > 0 && read_endsc-write_endsc > 0)
		{
			FD_SET(client_fd,&w);
			tv1=&tv;
		}
		// Server - ready to write data - (buf is empty)
		if(server_fd > 0 && read_endsc < BUF_SIZE)
		{
			tv1=NULL;
			FD_SET(server_fd,&r);
		}
		// Server - ready to read - write into it (buf non-empty)
		if(server_fd > 0 && read_endcs-write_endcs > 0)
		{
			tv1=&tv;
			FD_SET(server_fd,&w);
		}
		
		if (client_fd > server_fd) 
			newfd = client_fd;
       		else if (client_fd < server_fd)
        		newfd = server_fd;
		
		return_val=select(newfd + 1, &r, &w, NULL, tv1);
		
		if(return_val < 0)
		{	
			if(client_fd >0)
			{
				close(client_fd);
			}	
			if(server_fd >0)
			{
				close(server_fd);
			}
			exit(1);
	        }
		if(return_val==0)
		{
			close(client_fd);
			close(server_fd);
			client_fd=-1;
			server_fd=-1;
		}

		if(FD_ISSET(proxy_fd,&r))
		{
			memset(&client_addr, 0, sizeof(struct sockaddr_in));
			addr_size = sizeof(client_addr);
			client_fd = accept(proxy_fd, (struct sockaddr *)&client_addr,
					&addr_size);
			if(client_fd == -1) {
				fprintf(stderr, "accept error %s\n", strerror(errno));
				continue;
			}
			
			read_endcs=write_endcs =0;
			read_endsc=write_endsc =0;			
			
			// For debugging purpose
			if (getpeername(client_fd, (struct sockaddr *) &client_addr, &addr_size) < 0) {
				fprintf(stderr, "getpeername error %s\n", strerror(errno));
			}
	
			strncpy(client_hname, inet_ntoa(client_addr.sin_addr), MAX_ADDR_NAME);
			strncpy(server_hname, inet_ntoa(remote_addr.sin_addr), MAX_ADDR_NAME);

			// TODO: Disable following printf before submission
			//printf("Connection proxied: %s:%d --> %s:%d\n",
					//client_hname, ntohs(client_addr.sin_port),
					//server_hname, ntohs(remote_addr.sin_port));
			
			// Connect to the server
			if ((server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
				fprintf(stderr, "socket error %s\n", strerror(errno));
				continue;
			}
	
			if (connect(server_fd, (struct sockaddr *) &remote_addr, 
				sizeof(struct sockaddr_in)) <0) {
				if (errno != EINPROGRESS) {
					fprintf(stderr, "connect error %s\n", strerror(errno));
					continue;
				}		
			}
                }
			
		transfer(&client_fd,&server_fd,&read_endcs,&write_endcs,&read_endsc,&write_endsc,&r,&w);
		

		if(read_endcs == write_endcs)
		{
			read_endcs = write_endcs = 0;
		}

		if(read_endcs ==0 && write_endcs==0 && flag_clientEOF==1)
		{
			shutdown(server_fd,O_WRONLY);
		}
		

		if(read_endsc == write_endsc)
		{
			read_endsc = write_endsc = 0;
		}

		if(read_endsc ==0 && write_endsc==0 && flag_serverEOF==1)
		{
			close(client_fd);
			client_fd=-1;
		}
	}
}

int main(int argc, char **argv)
{
	char *remote_name;
	struct sockaddr_in proxy_addr;
	unsigned short local_port, remote_port;
	struct hostent *h;
	int arg_idx = 1, proxy_fd;
	
	if (argc != 4)
	{
		fprintf(stderr, "Usage %s <remote-target> <remote-target-port> "
			"<local-port>\n", argv[0]);
		exit(1);
	}

	remote_name = argv[arg_idx++];
	remote_port = atoi(argv[arg_idx++]);
	local_port = atoi(argv[arg_idx++]);
	

	/* Lookup server name and establish control connection */
	
	if ((h = gethostbyname(remote_name)) == NULL) {
		fprintf(stderr, "gethostbyname(%s) failed %s\n", remote_name, 
			strerror(errno));
		exit(1);
	}
	memset(&remote_addr, 0, sizeof(struct sockaddr_in));
	remote_addr.sin_family = AF_INET;
	memcpy(&remote_addr.sin_addr.s_addr, h->h_addr_list[0], sizeof(in_addr_t));
	remote_addr.sin_port = htons(remote_port);
	
	/* open up the TCP socket the proxy listens on */
	if ((proxy_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
		fprintf(stderr, "socket error %s\n", strerror(errno));
		exit(1);
	}
	/* bind the socket to all local addresses */
	memset(&proxy_addr, 0, sizeof(struct sockaddr_in));
	proxy_addr.sin_family = AF_INET;
	proxy_addr.sin_addr.s_addr = INADDR_ANY; /* bind to all local addresses */
	proxy_addr.sin_port = htons(local_port);
	if (bind(proxy_fd, (struct sockaddr *) &proxy_addr, 
			sizeof(proxy_addr)) < 0) {
		fprintf(stderr, "bind error %s\n", strerror(errno));
		exit(1);
	}

	listen(proxy_fd, MAX_LISTEN_BACKLOG);

	__loop(proxy_fd);

	return 0;
}
