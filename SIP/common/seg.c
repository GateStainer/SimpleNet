
#include "seg.h"

//STCP进程使用这个函数发送sendseg_arg_t结构(包含段及其目的节点ID)给SIP进程.
//参数sip_conn是在STCP进程和SIP进程之间连接的TCP描述符.
//如果sendseg_arg_t发送成功,就返回1,否则返回-1.
int sip_sendseg(int sip_conn, int dest_nodeID, seg_t* segPtr)
{
	char begin[] = "!&";
	char end[] = "!#";
	segPtr->header.checksum = 0;
	segPtr->header.checksum = checksum(segPtr);
	sendseg_arg_t sendbuf;
	sendbuf.nodeID = dest_nodeID;
	sendbuf.seg = *segPtr;
	if(send(sip_conn, &begin, 2, 0) == -1){
		printf("sip_sendseg: can't send !& \n");
		return -1;
	}
	if(send(sip_conn, &sendbuf, sizeof(sendbuf), 0) == -1){
		printf("sip_sendseg: can't send sendseg_arg_t \n");
		return -1;
	}
	if(send(sip_conn, &end, 2, 0) == -1){
		printf("sip_sendseg: can't send sendseg_arg_t \n");
		return -1;
	}
	return 1;

}

//STCP进程使用这个函数来接收来自SIP进程的包含段及其源节点ID的sendseg_arg_t结构.
//参数sip_conn是STCP进程和SIP进程之间连接的TCP描述符.
//当接收到段时, 使用seglost()来判断该段是否应被丢弃并检查校验和.
//如果成功接收到sendseg_arg_t就返回0, 否则返回-1.
int sip_recvseg(int sip_conn, int* src_nodeID, seg_t* segPtr)
{
	sendseg_arg_t  seg_buf;
	enum {PKTSTART1, PKTSTART2, PKTRECV, PKTSTOP1, PKTSTOP2} state;
	state = PKTSTART1;
	char buf;
	char *recvbuf = (char *)&seg_buf;
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
	
	*segPtr = seg_buf.seg;
	*src_nodeID = seg_buf.nodeID;
	if(n <= 0)
		return -1;
	int result = seglost(segPtr);
	if(result == 1 || checkchecksum(segPtr) == -1)
		return 1;
	return 0;
}

//SIP进程使用这个函数接收来自STCP进程的包含段及其目的节点ID的sendseg_arg_t结构.
//参数stcp_conn是在STCP进程和SIP进程之间连接的TCP描述符.
//如果成功接收到sendseg_arg_t就返回1, 否则返回-1.
int getsegToSend(int stcp_conn, int* dest_nodeID, seg_t* segPtr)
{
	memset(segPtr, 0, sizeof(seg_t));
	enum {PKTSTART1, PKTSTART2, PKTRECV, PKTSTOP1, PKTSTOP2} state;
	state = PKTSTART1;
	char buf;
	sendseg_arg_t buffer;
	char *recvbuf = (char *)&buffer;
	int i = 0, n;

	while( (n = recv(stcp_conn, &buf, 1, 0)) > 0){
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
	
	memcpy(segPtr, &(buffer.seg), sizeof (seg_t));
	*dest_nodeID = buffer.nodeID;

	if(n <= 0)
		return -1;
	return 1;
}

//SIP进程使用这个函数发送包含段及其源节点ID的sendseg_arg_t结构给STCP进程.
//参数stcp_conn是STCP进程和SIP进程之间连接的TCP描述符.
//如果sendseg_arg_t被成功发送就返回1, 否则返回-1.
int forwardsegToSTCP(int stcp_conn, int src_nodeID, seg_t* segPtr)
{
 	char begin[] = "!&";
	char end[] = "!#";
	sendseg_arg_t sendbuf;
	sendbuf.nodeID = src_nodeID;
	sendbuf.seg = *segPtr;

	if(send(stcp_conn, &begin, 2, 0) == -1){
		perror("forwardsegToSTCP: can't send !&");
		return -1;
	}
	if(send(stcp_conn, &sendbuf, sizeof (sendbuf), 0) == -1){
		printf("forwardsegToSTCP: can't send sip_pkt_t \n");
		return -1;
	}
	if(send(stcp_conn, &end, 2, 0) == -1){
		printf("forwardsegToSTCP: can't send !# \n");
		return -1;
	}
	return 1;
}

// 一个段有PKT_LOST_RATE/2的可能性丢失, 或PKT_LOST_RATE/2的可能性有着错误的校验和.
// 如果数据包丢失了, 就返回1, 否则返回0. 
// 即使段没有丢失, 它也有PKT_LOST_RATE/2的可能性有着错误的校验和.
// 我们在段中反转一个随机比特来创建错误的校验和.
int seglost(seg_t* segPtr)
{
	int random = rand()%100;
	if(random<PKT_LOSS_RATE*100) {
		//50%可能性丢失段
		if(rand()%2==0) {
			printf("seg lost!!!\n");
			return 1;
		}
		//50%可能性是错误的校验和
		else {
			printf("wrong checksum\n");
			//获取数据长度
			int len = sizeof(stcp_hdr_t)+segPtr->header.length;
			//获取要反转的随机位
			int errorbit = rand()%(len*8);
			//反转该比特
			char* temp = (char*)segPtr;
			temp = temp + errorbit/8;
			*temp = *temp^(1<<(errorbit%8));
			return 0;
		}
	}
	return 0;
}

//这个函数计算指定段的校验和.
//校验和计算覆盖段首部和段数据. 你应该首先将段首部中的校验和字段清零, 
//如果数据长度为奇数, 添加一个全零的字节来计算校验和.
//校验和计算使用1的补码.
unsigned short checksum(seg_t* segment)
{
	int size = sizeof(seg_t);
	u_short *buffer = (u_short *)segment;
	u_long csum = 0;
	segment->header.checksum = 0;
	while(size > 1){
		csum += *buffer++;
		size -= sizeof(u_short);
	}
	if(size)
		csum += *(u_char *)buffer;
	
	csum = (csum >> 16) + (csum & 0xffff);
	csum += (csum >> 16);
	return (u_short)(~csum);
}

//这个函数检查段中的校验和, 正确时返回1, 错误时返回-1.
int checkchecksum(seg_t* segment)
{
	int size = sizeof(seg_t);
	u_short *buffer = (u_short *)segment;
	u_long csum = 0;
	while(size > 1){
		csum += *buffer++;
		size -= sizeof(u_short);
	}
	if(size)
		csum += *(u_char *)buffer;
	
	csum = (csum >> 16) + (csum & 0xffff);
	csum += (csum >> 16);
	u_short result = (u_short)(~csum);
	if(result == 0)
		return 1;
	return -1;
}
