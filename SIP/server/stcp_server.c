//�ļ���: server/stcp_server.c
//
//����: ����ļ�����STCP�������ӿ�ʵ��. 
//
//��������: 2013��1��

#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <stdio.h>
#include <sys/select.h>
#include <strings.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>
#include "stcp_server.h"
#include "../topology/topology.h"
#include "../common/constants.h"

//����tcbtableΪȫ�ֱ���
server_tcb_t* tcb[MAX_TRANSPORT_CONNECTIONS];
//������SIP���̵�����Ϊȫ�ֱ���
int sipfd;

/*********************************************************************/
//
//STCP APIʵ��
//
/*********************************************************************/

// ���������ʼ��TCB��, ��������Ŀ���ΪNULL. �������TCP�׽���������conn��ʼ��һ��STCP���ȫ�ֱ���, 
// �ñ�����Ϊsip_sendseg��sip_recvseg���������. ���, �����������seghandler�߳�����������STCP��.
// ������ֻ��һ��seghandler.
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

// ����������ҷ�����TCB�����ҵ���һ��NULL��Ŀ, Ȼ��ʹ��malloc()Ϊ����Ŀ����һ���µ�TCB��Ŀ.
// ��TCB�е������ֶζ�����ʼ��, ����, TCB state������ΪCLOSED, �������˿ڱ�����Ϊ�������ò���server_port. 
// TCB������Ŀ������Ӧ��Ϊ�����������׽���ID�������������, �����ڱ�ʶ�������˵�����. 
// ���TCB����û����Ŀ����, �����������-1.
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

// �������ʹ��sockfd���TCBָ��, �������ӵ�stateת��ΪLISTENING. ��Ȼ��������ʱ������æ�ȴ�ֱ��TCB״̬ת��ΪCONNECTED 
// (���յ�SYNʱ, seghandler�����״̬��ת��). �ú�����һ������ѭ���еȴ�TCB��stateת��ΪCONNECTED,  
// ��������ת��ʱ, �ú�������1. �����ʹ�ò�ͬ�ķ�����ʵ�����������ȴ�.
int stcp_server_accept(int sockfd) 
{
	tcb[sockfd]->state = LISTENING;
	while(tcb[sockfd]->state != CONNECTED)
		usleep(ACCEPT_POLLING_INTERVAL / 1000);
	printf("accept success, connection established\n");
	return 1;
}

// ��������STCP�ͻ��˵�����. �������ÿ��RECVBUF_POLLING_INTERVALʱ��
// �Ͳ�ѯ���ջ�����, ֱ���ȴ������ݵ���, ��Ȼ��洢���ݲ�����1. ����������ʧ��, �򷵻�-1.
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

// �����������free()�ͷ�TCB��Ŀ. ��������Ŀ���ΪNULL, �ɹ�ʱ(��λ����ȷ��״̬)����1,
// ʧ��ʱ(��λ�ڴ����״̬)����-1.
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

// ������stcp_server_init()�������߳�. �������������Կͻ��˵Ľ�������. seghandler�����Ϊһ������sip_recvseg()������ѭ��, 
// ���sip_recvseg()ʧ��, ��˵����SIP���̵������ѹر�, �߳̽���ֹ. ����STCP�ε���ʱ����������״̬, ���Բ�ȡ��ͬ�Ķ���.
// ��鿴�����FSM���˽����ϸ��.
void* seghandler(void* arg) 
{
	seg_t recvbuf;
	seg_t sendbuf;
	int srcnodeID;

	int i = 0, n;
	while(1){
		memset(&recvbuf, 0, sizeof recvbuf);
		if( (n = sip_recvseg(sipfd, &srcnodeID, &recvbuf)) == 0){
			for(i = 0; i <MAX_TRANSPORT_CONNECTIONS; i++){
				if(tcb[i] != NULL && tcb[i]->server_portNum == recvbuf.header.dest_port){
					switch(tcb[i]->state){
						case CLOSED:
							break;
						case LISTENING:
							printf("server listening\n");
							if(recvbuf.header.type == SYN){
								tcb[i]->client_portNum = recvbuf.header.src_port;
								tcb[i]->client_nodeID = srcnodeID;
								tcb[i]->state = CONNECTED;
								tcb[i]->expect_seqNum = recvbuf.header.seq_num + 1;
								memset(&sendbuf, 0, sizeof(sendbuf));
								sendbuf.header.src_port = tcb[i]->server_portNum;
								sendbuf.header.dest_port = tcb[i]->client_portNum;
								sendbuf.header.type = SYNACK;
								sendbuf.header.ack_num = tcb[i]->expect_seqNum;
								printf("------server port %d recv SYN send SYNACK-------\n",
										tcb[i]->server_portNum);
								sip_sendseg(sipfd, srcnodeID, &sendbuf);
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
								sip_sendseg(sipfd, tcb[i]->client_nodeID, &sendbuf);
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
								sip_sendseg(sipfd, tcb[i]->client_nodeID, &sendbuf);
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
									sip_sendseg(sipfd, tcb[i]->client_nodeID,  &sendbuf);
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
									sip_sendseg(sipfd, tcb[i]->client_nodeID, &sendbuf);
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
								sip_sendseg(sipfd, tcb[i]->client_nodeID, &sendbuf);
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


