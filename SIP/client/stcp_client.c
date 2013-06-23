//文件名: client/stcp_client.c
//
//描述: 这个文件包含STCP客户端接口实现 
//
//创建日期: 2013年1月

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <assert.h>
#include <strings.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>
#include "../topology/topology.h"
#include "stcp_client.h"
#include "../common/seg.h"

//声明tcbtable为全局变量
client_tcb_t* tcb[MAX_TRANSPORT_CONNECTIONS];
//声明到SIP进程的TCP连接为全局变量
int sip_conn;

/*********************************************************************/
//
//STCP API实现
//
/*********************************************************************/

// 这个函数初始化TCB表, 将所有条目标记为NULL.  
// 它还针对TCP套接字描述符conn初始化一个STCP层的全局变量, 该变量作为sip_sendseg和sip_recvseg的输入参数.
// 最后, 这个函数启动seghandler线程来处理进入的STCP段. 客户端只有一个seghandler.
void stcp_client_init(int conn) 
{
	sip_conn = conn;
	int i;
	pthread_t tid;
	for(i = 0; i < MAX_TRANSPORT_CONNECTIONS; i++){
		tcb[i] = NULL;
	}

	pthread_create(&tid, NULL, seghandler, NULL);
	return;
}

// 这个函数查找客户端TCB表以找到第一个NULL条目, 然后使用malloc()为该条目创建一个新的TCB条目.
// 该TCB中的所有字段都被初始化. 例如, TCB state被设置为CLOSED，客户端端口被设置为函数调用参数client_port. 
// TCB表中条目的索引号应作为客户端的新套接字ID被这个函数返回, 它用于标识客户端的连接. 
// 如果TCB表中没有条目可用, 这个函数返回-1.
int stcp_client_sock(unsigned int client_port) 
{
	int i;
	for(i = 0; i < MAX_TRANSPORT_CONNECTIONS; i++){
		if(tcb[i] == NULL){
			tcb[i] = (client_tcb_t *)malloc(sizeof (client_tcb_t));
			tcb[i]->state = CLOSED;
			tcb[i]->client_portNum = client_port;
			tcb[i]->bufMutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
			tcb[i]->sendBufHead = NULL;
			tcb[i]->sendBufTail = NULL;
			tcb[i]->sendBufunSent = NULL;
			tcb[i]->unAck_segNum = 0;
			pthread_mutex_init(tcb[i]->bufMutex, NULL);
			return i;
		}
	}
	return -1;
}

// 这个函数用于连接服务器. 它以套接字ID, 服务器节点ID和服务器的端口号作为输入参数. 套接字ID用于找到TCB条目.  
// 这个函数设置TCB的服务器节点ID和服务器端口号,  然后使用sip_sendseg()发送一个SYN段给服务器.  
// 在发送了SYN段之后, 一个定时器被启动. 如果在SYNSEG_TIMEOUT时间之内没有收到SYNACK, SYN 段将被重传. 
// 如果收到了, 就返回1. 否则, 如果重传SYN的次数大于SYN_MAX_RETRY, 就将state转换到CLOSED, 并返回-1.
int stcp_client_connect(int sockfd, int nodeID, unsigned int server_port) 
{
	seg_t sendbuf;
	memset(&sendbuf, 0, sizeof(sendbuf));
	tcb[sockfd]->server_portNum = server_port;
	tcb[sockfd]->server_nodeID = nodeID;
	sendbuf.header.src_port = tcb[sockfd]->client_portNum;
	sendbuf.header.dest_port = tcb[sockfd]->server_portNum;
	sendbuf.header.type = SYN;
	sendbuf.header.seq_num = 0;
	printf("==========client port %d send SYN===========\n", tcb[sockfd]->client_portNum);
	sip_sendseg(sip_conn, nodeID,  &sendbuf);
	tcb[sockfd]->state = SYNSENT;
	tcb[sockfd]->next_seqNum = 1;

	int i = 0;
	for(i = 0; i < SYN_MAX_RETRY; i++){
		usleep(SYN_TIMEOUT / 1000);					//sleep and then judge
		if(tcb[sockfd]->state == CONNECTED)
			break;
		printf("==========client port %d send SYN==========\n", tcb[sockfd]->client_portNum);
		sip_sendseg(sip_conn, nodeID, &sendbuf);
		tcb[sockfd]->state = SYNSENT;
	}
	switch(tcb[sockfd]->state){
		case CONNECTED:
			return 1;
		default:
			tcb[sockfd]->state = CLOSED;
			return -1;
	}
	return -1;
}

// 发送数据给STCP服务器. 这个函数使用套接字ID找到TCB表中的条目.
// 然后它使用提供的数据创建segBuf, 将它附加到发送缓冲区链表中.
// 如果发送缓冲区在插入数据之前为空, 一个名为sendbuf_timer的线程就会启动.
// 每隔SENDBUF_ROLLING_INTERVAL时间查询发送缓冲区以检查是否有超时事件发生. 
// 这个函数在成功时返回1，否则返回-1. 
// stcp_client_send是一个非阻塞函数调用.
// 因为用户数据被分片为固定大小的STCP段, 所以一次stcp_client_send调用可能会产生多个segBuf
// 被添加到发送缓冲区链表中. 如果调用成功, 数据就被放入TCB发送缓冲区链表中, 根据滑动窗口的情况,
// 数据可能被传输到网络中, 或在队列中等待传输.
int stcp_client_send(int sockfd, void* data, unsigned int length) 
{
	pthread_t tid;
	int flag = 0;
	while(tcb[sockfd]->state == CONNECTED){
		pthread_mutex_lock(tcb[sockfd]->bufMutex);
		segBuf_t *sendbuf = malloc(sizeof(segBuf_t));
		memset(sendbuf, 0, sizeof(sendbuf));
		sendbuf->seg.header.src_port = tcb[sockfd]->client_portNum;
		sendbuf->seg.header.dest_port = tcb[sockfd]->server_portNum;
		sendbuf->seg.header.type = DATA;
		sendbuf->seg.header.seq_num = tcb[sockfd]->next_seqNum;
		sendbuf->next = NULL;
		sendbuf->sentTime = time(NULL);
		if(tcb[sockfd]->sendBufHead == NULL){
			tcb[sockfd]->sendBufHead = sendbuf;
			tcb[sockfd]->sendBufunSent = sendbuf;
			tcb[sockfd]->sendBufTail = sendbuf;
			tcb[sockfd]->unAck_segNum = 0;
			int *tmpfd = (int *)malloc(sizeof(int));
			*tmpfd = sockfd;
			pthread_create(&tid, NULL, sendBuf_timer, (void *)tmpfd);
		}else{
			tcb[sockfd]->sendBufTail->next = sendbuf;
			tcb[sockfd]->sendBufTail = sendbuf;
			//if unSent is null ,then the added is unSent
			if(tcb[sockfd]->sendBufunSent == NULL)
				tcb[sockfd]->sendBufunSent = sendbuf;
		}

		if(length > MAX_SEG_LEN){
			sendbuf->seg.header.length = MAX_SEG_LEN;
			memcpy(&(sendbuf->seg.data), data, MAX_SEG_LEN );

			data += MAX_SEG_LEN;
			length -= MAX_SEG_LEN;
			tcb[sockfd]->next_seqNum += MAX_SEG_LEN;
		}
		else{
			sendbuf->seg.header.length = length;
			memcpy(&(sendbuf->seg.data), data, length );
			tcb[sockfd]->next_seqNum += length;
			flag = 1;
		}


		//send right now or later?
		while(tcb[sockfd]->unAck_segNum < GBN_WINDOW && tcb[sockfd]->sendBufunSent != NULL){
			tcb[sockfd]->sendBufunSent->sentTime = time(NULL);
			printf("client send DATA %d \n", tcb[sockfd]->sendBufunSent->seg.header.seq_num);
			sip_sendseg(sip_conn, tcb[sockfd]->server_nodeID, &(tcb[sockfd]->sendBufunSent->seg));
			tcb[sockfd]->sendBufunSent = tcb[sockfd]->sendBufunSent->next;

			tcb[sockfd]->unAck_segNum++;
			printf("unAck_segNum: %d\n", tcb[sockfd]->unAck_segNum);
		}
		pthread_mutex_unlock(tcb[sockfd]->bufMutex);
		if(flag == 1)
			break;
	}
	if(tcb[sockfd]->state != CONNECTED)
		return -1;
	return 1;
}

// 这个函数用于断开到服务器的连接. 它以套接字ID作为输入参数. 套接字ID用于找到TCB表中的条目.  
// 这个函数发送FIN段给服务器. 在发送FIN之后, state将转换到FINWAIT, 并启动一个定时器.
// 如果在最终超时之前state转换到CLOSED, 则表明FINACK已被成功接收. 否则, 如果在经过FIN_MAX_RETRY次尝试之后,
// state仍然为FINWAIT, state将转换到CLOSED, 并返回-1.
int stcp_client_disconnect(int sockfd) 
{
	seg_t sendbuf;
	memset(&sendbuf, 0, sizeof(sendbuf));
	sendbuf.header.src_port = tcb[sockfd]->client_portNum;
	sendbuf.header.dest_port = tcb[sockfd]->server_portNum;
	sendbuf.header.type = FIN;
	tcb[sockfd]->state = FINWAIT;
	printf("===========client port %d send FIN===========\n", tcb[sockfd]->client_portNum);
	sip_sendseg(sip_conn, tcb[sockfd]->server_nodeID, &sendbuf);

	int i = 0;
	for(i = 0; i < FIN_MAX_RETRY; i++){
		usleep(FIN_TIMEOUT / 1000);					//sleep and then judge
		if(tcb[sockfd]->state == CLOSED)
			break;
		printf("try to disconnect times %d\n", i+1);
		tcb[sockfd]->state = FINWAIT;
		printf("==========client port %d send FIN===========\n", tcb[sockfd]->client_portNum);
		sip_sendseg(sip_conn, tcb[sockfd]->server_nodeID, &sendbuf);
	}
	pthread_mutex_lock(tcb[sockfd]->bufMutex);
	segBuf_t *p = tcb[sockfd]->sendBufHead;
	switch(tcb[sockfd]->state){
		case CLOSED:
			while(tcb[sockfd]->sendBufHead != NULL){
				p = tcb[sockfd]->sendBufHead;
				tcb[sockfd]->sendBufHead = tcb[sockfd]->sendBufHead->next;
				free(p);
			}
			pthread_mutex_unlock(tcb[sockfd]->bufMutex);
			return 1;
		case FINWAIT:
			tcb[sockfd]->state = CLOSED;
			pthread_mutex_lock(tcb[sockfd]->bufMutex);
			while(tcb[sockfd]->sendBufHead != NULL){
				p = tcb[sockfd]->sendBufHead;
				tcb[sockfd]->sendBufHead = tcb[sockfd]->sendBufHead->next;
				free(p);
			}
			pthread_mutex_unlock(tcb[sockfd]->bufMutex);
			return -1;
		default:
			pthread_mutex_unlock(tcb[sockfd]->bufMutex);
			printf("can't be here in stcp_client_disconnect\n");
	}
	return -1;
}

// 这个函数调用free()释放TCB条目. 它将该条目标记为NULL, 成功时(即位于正确的状态)返回1,
// 失败时(即位于错误的状态)返回-1.
int stcp_client_close(int sockfd) 
{
	if(tcb[sockfd]->state == CLOSED){
		pthread_mutex_destroy(tcb[sockfd]->bufMutex);
		free(tcb[sockfd]);
		tcb[sockfd] = NULL;
		return 1;
	}
	return -1;
}


// 这是由stcp_client_init()启动的线程. 它处理所有来自服务器的进入段. 
// seghandler被设计为一个调用sip_recvseg()的无穷循环. 如果sip_recvseg()失败, 则说明到SIP进程的连接已关闭,
// 线程将终止. 根据STCP段到达时连接所处的状态, 可以采取不同的动作. 请查看客户端FSM以了解更多细节.
void* seghandler(void* arg) 
{
	seg_t recvbuf;
	int i = 0, n;
	int srcnodeID;
	while(1){
		if( (n = sip_recvseg(sip_conn, &srcnodeID, &recvbuf)) == 0){
			for(i = 0; i <MAX_TRANSPORT_CONNECTIONS ; i++){
				if(tcb[i] != NULL && tcb[i]->client_portNum == recvbuf.header.dest_port
						&& tcb[i]->server_portNum == recvbuf.header.src_port){
					if(tcb[i]->state == SYNSENT && recvbuf.header.type == SYNACK){
						printf("client port %d receive SYNACK\n",
								tcb[i]->client_portNum);
						tcb[i]->state = CONNECTED;
					}
					//only recv DATAACK when connected
					if(tcb[i]->state == CONNECTED && recvbuf.header.type == DATAACK){
						int ack = recvbuf.header.ack_num;
						printf("client port %d receive DATAACK %d\n",
								tcb[i]->client_portNum, ack);
						pthread_mutex_lock(tcb[i]->bufMutex);
						assert(tcb[i]->sendBufHead != NULL);
						int num = tcb[i]->sendBufHead->seg.header.seq_num;
						printf("unAck_segNum %d \n", tcb[i]->unAck_segNum);
						while(num < ack){
							segBuf_t *tmp = tcb[i]->sendBufHead;
							tcb[i]->sendBufHead = tmp->next;
							printf("delete seg %d \n", tmp->seg.header.seq_num);
							free(tmp);
							tcb[i]->unAck_segNum--;
							printf("unAck_segNum -- %d\n", tcb[i]->unAck_segNum);
							if(tcb[i]->sendBufHead != NULL)
								num = tcb[i]->sendBufHead->seg.header.seq_num;
							else
								break;
						}
						while(tcb[i]->unAck_segNum < GBN_WINDOW && tcb[i]->sendBufunSent != NULL){
							tcb[i]->sendBufunSent->sentTime = time(NULL);
							sip_sendseg(sip_conn, tcb[i]->server_nodeID,  &(tcb[i]->sendBufunSent->seg));
							tcb[i]->sendBufunSent = tcb[i]->sendBufunSent->next;
							tcb[i]->unAck_segNum++;
							printf("unAck_segNum ++ %d\n", tcb[i]->unAck_segNum);
						}
						pthread_mutex_unlock(tcb[i]->bufMutex);
					}

					if(tcb[i]->state == FINWAIT && recvbuf.header.type == FINACK){
						printf("---------client port %d receive FINACK-----------\n",
								tcb[i]->client_portNum);
						tcb[i]->state = CLOSED;
					}
					break;
				}
			}
		}
		else if(n == -1){
//			printf("SON is closed\n");
			pthread_exit(NULL);
		}
	}
	pthread_exit(NULL);
}



//这个线程持续轮询发送缓冲区以触发超时事件. 如果发送缓冲区非空, 它应一直运行.
//如果(当前时间 - 第一个已发送但未被确认段的发送时间) > DATA_TIMEOUT, 就发生一次超时事件.
//当超时事件发生时, 重新发送所有已发送但未被确认段. 当发送缓冲区为空时, 这个线程将终止.
void* sendBuf_timer(void* clientfd) 
{
	int sockfd = *(int *)clientfd;
	free(clientfd);
	client_tcb_t *tmp = tcb[sockfd];
	printf("sendBuf_timer start running \n");
//	while(tmp->state == CONNECTED){
	while(1){
		usleep(SENDBUF_POLLING_INTERVAL/1000);
		pthread_mutex_lock(tmp->bufMutex);
		if(tmp->sendBufHead == NULL){
			printf("sendBuf_timer exit\n");
			pthread_mutex_unlock(tmp->bufMutex);
			pthread_exit(NULL);
		}
		else if(time(NULL) - tmp->sendBufHead->sentTime > DATA_TIMEOUT) {
//			printf("last sent time %ld\n", tmp->sendBufHead->sentTime);
			segBuf_t *p = tmp->sendBufHead;
			int un_ack = tmp->unAck_segNum;
//			printf("sendBuf_timer unack %d \n", un_ack);
			while(p != tmp->sendBufunSent){
				p->sentTime = time(NULL);
				printf("re send seg%d----------\n", p->seg.header.seq_num);
				sip_sendseg(sip_conn, tmp->server_nodeID,  &(p->seg));
				p = p->next;
			}
		}
		pthread_mutex_unlock(tmp->bufMutex);
	}
	pthread_exit(NULL);
}

