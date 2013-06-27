#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>
#include "stcp_client.h"

//
//  �����������ṩ��ÿ���������õ�ԭ�Ͷ����ϸ��˵��, ����Щֻ��ָ���Ե�, ����ȫ���Ը����Լ����뷨����ƴ���.
//
//  ע��: ��ʵ����Щ����ʱ, ����Ҫ����FSM�����п��ܵ�״̬, �����ʹ��switch�����ʵ��.
//
//  Ŀ��: ������������Ʋ�ʵ������ĺ���ԭ��.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

// ���������ʼ��TCB��, ��������Ŀ���ΪNULL.  
// ��������ص�����TCP�׽���������conn��ʼ��һ��STCP���ȫ�ֱ���, �ñ�����Ϊsip_sendseg��sip_recvseg���������.
// ���, �����������seghandler�߳�����������STCP��. �ͻ���ֻ��һ��seghandler.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//

client_tcb_t* tcb[MAX_TRANSPORT_CONNECTIONS];
int sipfd;				//ȫ��tcp socket������

void stcp_client_init(int conn)
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

// ����������ҿͻ���TCB�����ҵ���һ��NULL��Ŀ, Ȼ��ʹ��malloc()Ϊ����Ŀ����һ���µ�TCB��Ŀ.
// ��TCB�е������ֶζ�����ʼ��. ����, TCB state������ΪCLOSED���ͻ��˶˿ڱ�����Ϊ�������ò���client_port. 
// TCB������Ŀ��������Ӧ��Ϊ�ͻ��˵����׽���ID�������������, �����ڱ�ʶ�ͻ��˵�����. 
// ���TCB����û����Ŀ����, �����������-1.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
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

// ��������������ӷ�����. �����׽���ID�ͷ������Ķ˿ں���Ϊ�������. �׽���ID�����ҵ�TCB��Ŀ.  
// �����������TCB�ķ������˿ں�,  Ȼ��ʹ��sip_sendseg()����һ��SYN�θ�������.  
// �ڷ�����SYN��֮��, һ����ʱ��������. �����SYNSEG_TIMEOUTʱ��֮��û���յ�SYNACK, SYN �ν����ش�. 
// ����յ���, �ͷ���1. ����, ����ش�SYN�Ĵ�������SYN_MAX_RETRY, �ͽ�stateת����CLOSED, ������-1.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
int stcp_client_connect(int sockfd, unsigned int server_port)
{
	seg_t sendbuf;
	memset(&sendbuf, 0, sizeof(sendbuf));
	tcb[sockfd]->server_portNum = server_port;
	sendbuf.header.src_port = tcb[sockfd]->client_portNum;
	sendbuf.header.dest_port = tcb[sockfd]->server_portNum;
	sendbuf.header.type = SYN;
	sendbuf.header.seq_num = 0;
	printf("==========client port %d send SYN===========\n", tcb[sockfd]->client_portNum);
	sip_sendseg(sipfd, &sendbuf);
	tcb[sockfd]->state = SYNSENT;
	tcb[sockfd]->next_seqNum = 1;

	int i = 0;
	for(i = 0; i < SYN_MAX_RETRY; i++){
		usleep(SYN_TIMEOUT / 1000);					//sleep and then judge
		if(tcb[sockfd]->state == CONNECTED)
			break;
		printf("==========client port %d send SYN==========\n", tcb[sockfd]->client_portNum);
		sip_sendseg(sipfd, &sendbuf);
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


// �������ݸ�STCP������. �������ʹ���׽���ID�ҵ�TCB���е���Ŀ. 
// Ȼ����ʹ���ṩ�����ݴ���segBuf, �������ӵ����ͻ�����������. 
// ������ͻ������ڲ�������֮ǰΪ��, һ����Ϊsendbuf_timer���߳̾ͻ�����. 
// ÿ��SENDBUF_POLLING_INTERVALʱ���ѯ���ͻ������Լ���Ƿ��г�ʱ�¼�����.
// ��������ڳɹ�ʱ����1�����򷵻�-1. 
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
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
			pthread_create(&tid, NULL, sendBuf_timer, (void *)tcb[sockfd]);
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
			sip_sendseg(sipfd, &(tcb[sockfd]->sendBufunSent->seg));
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

// ����������ڶϿ���������������. �����׽���ID��Ϊ�������. �׽���ID�����ҵ�TCB���е���Ŀ.  
// �����������FIN�θ�������. �ڷ���FIN֮��, state��ת����FINWAIT, ������һ����ʱ��.
// ��������ճ�ʱ֮ǰstateת����CLOSED, �����FINACK�ѱ��ɹ�����. ����, ����ھ���FIN_MAX_RETRY�γ���֮��,
// state��ȻΪFINWAIT, state��ת����CLOSED, ������-1.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
int stcp_client_disconnect(int sockfd)
{
	seg_t sendbuf;
	memset(&sendbuf, 0, sizeof(sendbuf));
	sendbuf.header.src_port = tcb[sockfd]->client_portNum;
	sendbuf.header.dest_port = tcb[sockfd]->server_portNum;
	sendbuf.header.type = FIN;
	tcb[sockfd]->state = FINWAIT;
	printf("===========client port %d send FIN===========\n", tcb[sockfd]->client_portNum);
	sip_sendseg(sipfd, &sendbuf);

	int i = 0;
	for(i = 0; i < FIN_MAX_RETRY; i++){
		usleep(FIN_TIMEOUT / 1000);					//sleep and then judge
		if(tcb[sockfd]->state == CLOSED)
			break;
		printf("try to disconnect times %d\n", i+1);
		tcb[sockfd]->state = FINWAIT;
		printf("==========client port %d send FIN===========\n", tcb[sockfd]->client_portNum);
		sip_sendseg(sipfd, &sendbuf);
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


// �����������free()�ͷ�TCB��Ŀ. ��������Ŀ���ΪNULL, �ɹ�ʱ(��λ����ȷ��״̬)����1,
// ʧ��ʱ(��λ�ڴ����״̬)����-1.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
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


// ������stcp_client_init()�������߳�. �������������Է������Ľ����. 
// seghandler�����Ϊһ������sip_recvseg()������ѭ��. ���sip_recvseg()ʧ��, ��˵���ص����������ѹر�,
// �߳̽���ֹ. ����STCP�ε���ʱ����������״̬, ���Բ�ȡ��ͬ�Ķ���. ��鿴�ͻ���FSM���˽����ϸ��.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
void *seghandler(void* arg)
{
	seg_t recvbuf;
	int i = 0, n;
	while(1){
		if( (n = sip_recvseg(sipfd, &recvbuf)) == 0){
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
							printf("client send DATA %d \n", tcb[i]->sendBufunSent->seg.header.seq_num);
							sip_sendseg(sipfd, &(tcb[i]->sendBufunSent->seg));
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

// ����̳߳�����ѯ���ͻ������Դ�����ʱ�¼�. ������ͻ������ǿ�, ��Ӧһֱ����.
// ���(��ǰʱ�� - ��һ���ѷ��͵�δ��ȷ�϶εķ���ʱ��) > DATA_TIMEOUT, �ͷ���һ�γ�ʱ�¼�.
// ����ʱ�¼�����ʱ, ���·��������ѷ��͵�δ��ȷ�϶�. �����ͻ�����Ϊ��ʱ, ����߳̽���ֹ.
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
void* sendBuf_timer(void* clienttcb)
{
	client_tcb_t *tmp = (client_tcb_t *)clienttcb;
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
				printf("RE SEND SEG%d----------\n", p->seg.header.seq_num);
				sip_sendseg(sipfd, &(p->seg));
				p = p->next;
			}
		}
		pthread_mutex_unlock(tmp->bufMutex);
	}
	pthread_exit(NULL);
}
