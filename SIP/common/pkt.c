// 文件名: common/pkt.c
// 创建日期: 2013年1月

#include "pkt.h"
#include "stdio.h"

// son_sendpkt()由SIP进程调用, 其作用是要求SON进程将报文发送到重叠网络中. SON进程和SIP进程通过一个本地TCP连接互连.
// 在son_sendpkt()中, 报文及其下一跳的节点ID被封装进数据结构sendpkt_arg_t, 并通过TCP连接发送给SON进程. 
// 参数son_conn是SIP进程和SON进程之间的TCP连接套接字描述符.
// 当通过SIP进程和SON进程之间的TCP连接发送数据结构sendpkt_arg_t时, 使用'!&'和'!#'作为分隔符, 按照'!& sendpkt_arg_t结构 !#'的顺序发送.
// 如果发送成功, 返回1, 否则返回-1.
int son_sendpkt(int nextNodeID, sip_pkt_t* pkt, int son_conn)
{
	char begin[] = "!&";
	char end[] = "!#";
	sendpkt_arg_t sendbuf;
	sendbuf.nextNodeID = nextNodeID;
	sendbuf.pkt = *pkt;
	if(send(son_conn, &begin, 2, 0) == -1)
		return -1;
	if(send(son_conn, &sendbuf, sizeof sendbuf, 0) == -1)
		return -1;
	if(send(son_conn, &end, 2, 0) == -1)
		return -1;
	return 1;
}

// son_recvpkt()函数由SIP进程调用, 其作用是接收来自SON进程的报文. 
// 参数son_conn是SIP进程和SON进程之间TCP连接的套接字描述符. 报文通过SIP进程和SON进程之间的TCP连接发送, 使用分隔符!&和!#. 
// 为了接收报文, 这个函数使用一个简单的有限状态机FSM
// PKTSTART1 -- 起点 
// PKTSTART2 -- 接收到'!', 期待'&' 
// PKTRECV -- 接收到'&', 开始接收数据
// PKTSTOP1 -- 接收到'!', 期待'#'以结束数据的接收 
// 如果成功接收报文, 返回1, 否则返回-1.
int son_recvpkt(sip_pkt_t* pkt, int son_conn)
{
	enum {PKTSTART1, PKTSTART2, PKTRECV, PKTSTOP1, PKTSTOP2} state;
	state = PKTSTART1;
	char buf;
	char *recvbuf = (char *)pkt;
	int i = 0, n;

	while( (n = recv(son_conn, &buf, 1, 0)) > 0){
		switch(state){
			case PKTSTART1:
				if(buf == '!')
					state = PKTSTART2;
				break;
			case PKTSTART2:
				if(buf == '&')
					state = PKTRECV;
				else
					state = PKTSTART1;
				break;
			case PKTRECV:
				if(buf != '!')
					recvbuf[i++] = buf;
				else
					state = PKTSTOP1;
				break;
			case PKTSTOP1:
				if(buf == '#')
					state = PKTSTOP2;
				else{
					recvbuf[i++] = '!';
					recvbuf[i++] = buf;
					state = PKTRECV;
				}
				break;
			case PKTSTOP2:
				break;
		}
		if(state == PKTSTOP2)
			break;
	}
	
	if(n <= 0)
		return -1;
	return 1;
}

// 这个函数由SON进程调用, 其作用是接收数据结构sendpkt_arg_t.
// 报文和下一跳的节点ID被封装进sendpkt_arg_t结构.
// 参数sip_conn是在SIP进程和SON进程之间的TCP连接的套接字描述符. 
// sendpkt_arg_t结构通过SIP进程和SON进程之间的TCP连接发送, 使用分隔符!&和!#. 
// 为了接收报文, 这个函数使用一个简单的有限状态机FSM
// PKTSTART1 -- 起点 
// PKTSTART2 -- 接收到'!', 期待'&' 
// PKTRECV -- 接收到'&', 开始接收数据
// PKTSTOP1 -- 接收到'!', 期待'#'以结束数据的接收
// 如果成功接收sendpkt_arg_t结构, 返回1, 否则返回-1.
int getpktToSend(sip_pkt_t* pkt, int* nextNode,int sip_conn)
{
	enum {PKTSTART1, PKTSTART2, PKTRECV, PKTSTOP1, PKTSTOP2} state;
	state = PKTSTART1;
	char buf;
	sendpkt_arg_t buffer;
	char *recvbuf = (char *)&buffer;
	int i = 0, n;

	while( (n = recv(sip_conn, &buf, 1, 0)) > 0){
		switch(state){
			case PKTSTART1:
				if(buf == '!')
					state = PKTSTART2;
				break;
			case PKTSTART2:
				if(buf == '&')
					state = PKTRECV;
				else
					state = PKTSTART1;
				break;
			case PKTRECV:
				if(buf != '!')
					recvbuf[i++] = buf;
				else
					state = PKTSTOP1;
				break;
			case PKTSTOP1:
				if(buf == '#')
					state = PKTSTOP2;
				else{
					recvbuf[i++] = '!';
					recvbuf[i++] = buf;
					state = PKTRECV;
				}
				break;
			case PKTSTOP2:
				break;
		}
		if(state == PKTSTOP2)
			break;
	}
	
	memcpy(pkt, &(buffer.pkt), sizeof (sip_pkt_t));
	*nextNode = buffer.nextNodeID;

	if(n <= 0)
		return -1;
	return 1;
}

// forwardpktToSIP()函数是在SON进程接收到来自重叠网络中其邻居的报文后被调用的. 
// SON进程调用这个函数将报文转发给SIP进程. 
// 参数sip_conn是SIP进程和SON进程之间的TCP连接的套接字描述符. 
// 报文通过SIP进程和SON进程之间的TCP连接发送, 使用分隔符!&和!#, 按照'!& 报文 !#'的顺序发送. 
// 如果报文发送成功, 返回1, 否则返回-1.
int forwardpktToSIP(sip_pkt_t* pkt, int sip_conn)
{
	char begin[] = "!&";
	char end[] = "!#";
	if(send(sip_conn, &begin, 2, 0) == -1){
		perror("can't send !&");
		return -1;
	}
	if(send(sip_conn, pkt, sizeof (sip_pkt_t), 0) == -1){
		printf("can't send sip_pkt_t \n");
		return -1;
	}
	if(send(sip_conn, &end, 2, 0) == -1){
		printf("can't send !# \n");
		return -1;
	}
	return 1;
}


// sendpkt()函数由SON进程调用, 其作用是将接收自SIP进程的报文发送给下一跳.
// 参数conn是到下一跳节点的TCP连接的套接字描述符.
// 报文通过SON进程和其邻居节点之间的TCP连接发送, 使用分隔符!&和!#, 按照'!& 报文 !#'的顺序发送. 
// 如果报文发送成功, 返回1, 否则返回-1.
int sendpkt(sip_pkt_t* pkt, int conn)
{
	char begin[] = "!&";
	char end[] = "!#";
	if(send(conn, &begin, 2, 0) == -1)
		return -1;
	if(send(conn, pkt, sizeof (sip_pkt_t), 0) == -1)
		return -1;
	if(send(conn, &end, 2, 0) == -1)
		return -1;
	return 1;
}

// recvpkt()函数由SON进程调用, 其作用是接收来自重叠网络中其邻居的报文.
// 参数conn是到其邻居的TCP连接的套接字描述符.
// 报文通过SON进程和其邻居之间的TCP连接发送, 使用分隔符!&和!#. 
// 为了接收报文, 这个函数使用一个简单的有限状态机FSM
// PKTSTART1 -- 起点 
// PKTSTART2 -- 接收到'!', 期待'&' 
// PKTRECV -- 接收到'&', 开始接收数据
// PKTSTOP1 -- 接收到'!', 期待'#'以结束数据的接收 
// 如果成功接收报文, 返回1, 否则返回-1.
int recvpkt(sip_pkt_t* pkt, int conn)
{
	enum {PKTSTART1, PKTSTART2, PKTRECV, PKTSTOP1, PKTSTOP2} state;
	state = PKTSTART1;
	char buf;
	char *recvbuf = (char *)pkt;
	int i = 0, n;

	printf("before recvpkt \n");
	printf("conn : %d \n", conn);
	while( (n = recv(conn, &buf, 1, 0)) > 0){
		switch(state){
			case PKTSTART1:
				if(buf == '!')
					state = PKTSTART2;
				break;
			case PKTSTART2:
				if(buf == '&')
					state = PKTRECV;
				else
					state = PKTSTART1;
				break;
			case PKTRECV:
				if(buf != '!')
					recvbuf[i++] = buf;
				else
					state = PKTSTOP1;
				break;
			case PKTSTOP1:
				if(buf == '#')
					state = PKTSTOP2;
				else{
					recvbuf[i++] = '!';
					recvbuf[i++] = buf;
					state = PKTRECV;
				}
				break;
			case PKTSTOP2:
				break;
		}
		if(state == PKTSTOP2)
			break;
	}
	
	printf("after recvpkt \n");
	if(n <= 0)
		return -1;
	return 1;
}
