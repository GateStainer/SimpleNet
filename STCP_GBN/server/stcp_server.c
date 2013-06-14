// 文件名: stcp_server.c
//
// 描述: 这个文件包含服务器STCP套接字接口定义. 你需要实现所有这些接口.
//
// 创建日期: 2013年1月
//

#include <stdlib.h>
#include <sys/socket.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include "stcp_server.h"
#include "../common/constants.h"

//
//  用于服务器程序的STCP套接字API. 
//  ===================================
//
//  我们在下面提供了每个函数调用的原型定义和细节说明, 但这些只是指导性的, 你完全可以根据自己的想法来设计代码.
//
//  注意: 当实现这些函数时, 你需要考虑FSM中所有可能的状态, 这可以使用switch语句来实现. 
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//

// 这个函数初始化TCB表, 将所有条目标记为NULL. 它还针对重叠网络TCP套接字描述符conn初始化一个STCP层的全局变量, 
// 该变量作为sip_sendseg和sip_recvseg的输入参数. 最后, 这个函数启动seghandler线程来处理进入的STCP段.
// 服务器只有一个seghandler.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
server_tcb_t* tcb[MAX_TRANSPORT_CONNECTIONS];
int sipfd;

void stcp_server_init(int conn)
{
	sipfd = conn;
	int i;
	pthread_t tid;
	for(i = 0; i < MAX_TRANSPORT_CONNECTIONS; i++){
		tcb[i] = NULL;
	}
	pthread_create(&tid, NULL, seghandler, NULL);
	return;
}

// 这个函数查找服务器TCB表以找到第一个NULL条目, 然后使用malloc()为该条目创建一个新的TCB条目.
// 该TCB中的所有字段都被初始化, 例如, TCB state被设置为CLOSED, 服务器端口被设置为函数调用参数server_port. 
// TCB表中条目的索引应作为服务器的新套接字ID被这个函数返回, 它用于标识服务器的连接. 
// 如果TCB表中没有条目可用, 这个函数返回-1.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
int stcp_server_sock(unsigned int server_port)
{
	int i;
	for(i = 0; i < MAX_TRANSPORT_CONNECTIONS; i++){
		if(tcb[i] == NULL){
			tcb[i] = (server_tcb_t *)malloc(sizeof (server_tcb_t));
			tcb[i]->state = CLOSED;
			tcb[i]->server_portNum = server_port;
			tcb[i]->recvBuf = (char *)malloc(RECEIVE_BUF_SIZE);
			tcb[i]->usedBufLen = 0;
			tcb[i]->bufMutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
			pthread_mutex_init(tcb[i]->bufMutex, NULL);
			return i;
		}
	}
	return -1;
}

// 这个函数使用sockfd获得TCB指针, 并将连接的state转换为LISTENING. 它然后启动定时器进入忙等待直到TCB状态转换为CONNECTED 
// (当收到SYN时, seghandler会进行状态的转换). 该函数在一个无穷循环中等待TCB的state转换为CONNECTED,  
// 当发生了转换时, 该函数返回1. 你可以使用不同的方法来实现这种阻塞等待.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
int stcp_server_accept(int sockfd)
{
	tcb[sockfd]->state = LISTENING;
	while(tcb[sockfd]->state != CONNECTED)
		usleep(ACCEPT_POLLING_INTERVAL / 1000);
	printf("accept success, connection established\n");
	return 1;
}

// 接收来自STCP客户端的数据. 请回忆STCP使用的是单向传输, 数据从客户端发送到服务器端.
// 信号/控制信息(如SYN, SYNACK等)则是双向传递. 这个函数每隔RECVBUF_POLLING_INTERVAL时间
// 就查询接收缓冲区, 直到等待的数据到达, 它然后存储数据并返回1. 如果这个函数失败, 则返回-1.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
int stcp_server_recv(int sockfd, void* buf, unsigned int length)
{
	while(1){
		if(tcb[sockfd]->usedBufLen >= length){
			pthread_mutex_lock(tcb[sockfd]->bufMutex);
			memcpy(buf, tcb[sockfd]->recvBuf, length);
			int i;
			for(i = 0; i < tcb[sockfd]->usedBufLen - length; i++){
				tcb[sockfd]->recvBuf[i] = tcb[sockfd]->recvBuf[i + length];
			}
			tcb[sockfd]->usedBufLen -= length;
			pthread_mutex_unlock(tcb[sockfd]->bufMutex);
			return 1;
		}
		sleep(RECVBUF_POLLING_INTERVAL);
	}
	return -1;
}

// 这个函数调用free()释放TCB条目. 它将该条目标记为NULL, 成功时(即位于正确的状态)返回1,
// 失败时(即位于错误的状态)返回-1.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
int stcp_server_close(int sockfd)
{
	if(tcb[sockfd]->state == CLOSEWAIT){
		sleep(CLOSEWAIT_TIMEOUT);
		pthread_mutex_lock(tcb[sockfd]->bufMutex);
		tcb[sockfd]->usedBufLen = 0;
		tcb[sockfd]->state = CLOSED;
		pthread_mutex_unlock(tcb[sockfd]->bufMutex);
	}
	if(tcb[sockfd]->state == CLOSED){
		pthread_mutex_lock(tcb[sockfd]->bufMutex);
		free(tcb[sockfd]->recvBuf);
		pthread_mutex_unlock(tcb[sockfd]->bufMutex);
		pthread_mutex_destroy(tcb[sockfd]->bufMutex);
		free(tcb[sockfd]);
		tcb[sockfd] = NULL;
		return 1;
	}
	return -1;
}

// 这是由stcp_server_init()启动的线程. 它处理所有来自客户端的进入数据. seghandler被设计为一个调用sip_recvseg()的无穷循环, 
// 如果sip_recvseg()失败, 则说明重叠网络连接已关闭, 线程将终止. 根据STCP段到达时连接所处的状态, 可以采取不同的动作.
// 请查看服务端FSM以了解更多细节.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
void* seghandler(void* arg)
{
	seg_t recvbuf;
	seg_t sendbuf;

	int i = 0, n;
	while(1){
		memset(&recvbuf, 0, sizeof recvbuf);
		if( (n = sip_recvseg(sipfd, &recvbuf)) == 0){
			for(i = 0; i <MAX_TRANSPORT_CONNECTIONS; i++){
				if(tcb[i] != NULL && tcb[i]->server_portNum == recvbuf.header.dest_port){
					switch(tcb[i]->state){
						case CLOSED:
							break;
						case LISTENING:
							printf("server listening\n");
							if(recvbuf.header.type == SYN){
								tcb[i]->client_portNum = recvbuf.header.src_port;
								tcb[i]->state = CONNECTED;
								tcb[i]->expect_seqNum = recvbuf.header.seq_num + 1;
								memset(&sendbuf, 0, sizeof(sendbuf));
								sendbuf.header.src_port = tcb[i]->server_portNum;
								sendbuf.header.dest_port = tcb[i]->client_portNum;
								sendbuf.header.type = SYNACK;
								sendbuf.header.ack_num = tcb[i]->expect_seqNum;
								printf("------server port %d recv SYN send SYNACK-------\n",
										tcb[i]->server_portNum);
								sip_sendseg(sipfd, &sendbuf);
							}
							break;
						case CONNECTED:
							if(recvbuf.header.type == SYN){
								tcb[i]->state = CONNECTED;
								tcb[i]->expect_seqNum = recvbuf.header.seq_num + 1;
								memset(&sendbuf, 0, sizeof(sendbuf));
								sendbuf.header.src_port = tcb[i]->server_portNum;
								sendbuf.header.dest_port = tcb[i]->client_portNum;
								sendbuf.header.type = SYNACK;
								printf("server port %d recv SYN send SYNACK\n",
										tcb[i]->server_portNum);
								sip_sendseg(sipfd, &sendbuf);
							}
							else if(recvbuf.header.type == FIN){
								printf("server port %d receive FIN, prepare to close\n",
										tcb[i]->server_portNum);
								tcb[i]->state = CLOSEWAIT;
								memset(&sendbuf, 0, sizeof(sendbuf));
								sendbuf.header.src_port = tcb[i]->server_portNum;
								sendbuf.header.dest_port = tcb[i]->client_portNum;
								sendbuf.header.type = FINACK;
								printf("server port  %d recv FIN send FINACK\n",
										tcb[i]->server_portNum);
								sip_sendseg(sipfd, &sendbuf);
							}else if(recvbuf.header.type == DATA){
								int data_len = recvbuf.header.length;
								if(tcb[i]->usedBufLen + data_len > RECEIVE_BUF_SIZE){
									printf("exceed RECEIVE_BUF_SIZE\n");
									;
								}
								else if(tcb[i]->expect_seqNum == recvbuf.header.seq_num){
									pthread_mutex_lock(tcb[i]->bufMutex);
									memcpy(tcb[i]->recvBuf + tcb[i]->usedBufLen,
											recvbuf.data, data_len);
									tcb[i]->expect_seqNum += data_len;
									tcb[i]->usedBufLen += data_len;
									memset(&sendbuf, 0, sizeof(sendbuf));
									sendbuf.header.src_port = tcb[i]->server_portNum;
									sendbuf.header.dest_port = tcb[i]->client_portNum;
									sendbuf.header.ack_num = tcb[i]->expect_seqNum;
									sendbuf.header.type = DATAACK;
									printf("server port  %d recv DATA %d send DATAACK %d\n",
											tcb[i]->server_portNum, recvbuf.header.seq_num,
											sendbuf.header.ack_num);
									pthread_mutex_unlock(tcb[i]->bufMutex);
									sip_sendseg(sipfd, &sendbuf);
								}
								else if(tcb[i]->expect_seqNum != recvbuf.header.seq_num){
									memset(&sendbuf, 0, sizeof(sendbuf));
									sendbuf.header.src_port = tcb[i]->server_portNum;
									sendbuf.header.dest_port = tcb[i]->client_portNum;
									sendbuf.header.ack_num = tcb[i]->expect_seqNum;
									sendbuf.header.type = DATAACK;
									printf("server port  %d recv DATA %d send DATAACK %d\n",
											tcb[i]->server_portNum, recvbuf.header.seq_num,
											sendbuf.header.ack_num);
									sip_sendseg(sipfd, &sendbuf);
								}
							}

							break;
						case CLOSEWAIT:
							printf("server closewait\n");
							if(recvbuf.header.type == FIN){
								memset(&sendbuf, 0, sizeof(sendbuf));
								sendbuf.header.src_port = tcb[i]->server_portNum;
								sendbuf.header.dest_port = tcb[i]->client_portNum;
								sendbuf.header.type = FINACK;
								printf("-------server port %d recv FIN send FINACK----------\n",
										tcb[i]->server_portNum);
								sip_sendseg(sipfd, &sendbuf);
							}
							break;
					}
					break;
				}
			}
		}
		else if(n == -1){
			//printf("server-----------SON is closed\n");
			pthread_exit(NULL);
		}
	}
	pthread_exit(NULL);
}

