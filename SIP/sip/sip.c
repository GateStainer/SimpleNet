//文件名: sip/sip.c
//
//描述: 这个文件实现SIP进程  
//
//创建日期: 2013年1月

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <strings.h>
#include <arpa/inet.h>
#include <signal.h>
#include <netdb.h>
#include <assert.h>
#include <sys/utsname.h>
#include <pthread.h>
#include <unistd.h>

#include "../common/constants.h"
#include "../common/pkt.h"
#include "../common/seg.h"
#include "../topology/topology.h"
#include "sip.h"
#include "nbrcosttable.h"
#include "dvtable.h"
#include "routingtable.h"

//SIP层等待这段时间让SIP路由协议建立路由路径. 
#define SIP_WAITTIME 60

/**************************************************************/
//声明全局变量
/**************************************************************/
int son_conn; 			//到重叠网络的连接
int stcp_conn;			//到STCP的连接
nbr_cost_entry_t* nct;			//邻居代价表
dv_t* dv;				//距离矢量表
pthread_mutex_t* dv_mutex;		//距离矢量表互斥量
routingtable_t* routingtable;		//路由表
pthread_mutex_t* routingtable_mutex;	//路由表互斥量

/**************************************************************/
//实现SIP的函数
/**************************************************************/

//SIP进程使用这个函数连接到本地SON进程的端口SON_PORT.
//成功时返回连接描述符, 否则返回-1.
int connectToSON() 
{ 
	if((son_conn = socket(AF_INET, SOCK_STREAM, 0)) < 0){
		printf("error in creating socket\n");
		return -1;
	}

	struct sockaddr_in servaddr;
	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
	servaddr.sin_port = htons(SON_PORT);
	if(connect(son_conn, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0){
		printf("Can Not establish connect from sip to son\n");
		return -1;
	}
	printf("sip: connected to local SON\n");
	return son_conn;
}

//这个线程每隔ROUTEUPDATE_INTERVAL时间发送路由更新报文.路由更新报文包含这个节点
//的距离矢量.广播是通过设置SIP报文头中的dest_NodeID为BROADCAST_NODEID,并通过son_sendpkt()发送报文来完成的.
void* routeupdate_daemon(void* arg) 
{
	int node_num = topology_getNodeNum();
	int nbr_num = topology_getNbrNum();
	pkt_routeupdate_t routemsg;

	while(1){
		sleep(ROUTEUPDATE_INTERVAL);
		sip_pkt_t sendbuf;
		memset(&sendbuf, 0, sizeof sendbuf);
		sendbuf.header.type = ROUTE_UPDATE;
		sendbuf.header.dest_nodeID = BROADCAST_NODEID;
		sendbuf.header.src_nodeID = topology_getMyNodeID();
		sendbuf.header.length = sizeof(pkt_routeupdate_t);

		//add the content of route update packet	
		int i = 0;
		routemsg.entryNum = node_num;
		pthread_mutex_lock(dv_mutex);
		for(i = 0; i < node_num; i++){
			//dv[nbr_num] is the node itself's distance vector
			routemsg.entry[i].nodeID = dv[nbr_num].dvEntry[i].nodeID;
			routemsg.entry[i].cost = dv[nbr_num].dvEntry[i].cost;
		}
		pthread_mutex_unlock(dv_mutex);
		memcpy(sendbuf.data, &routemsg, sizeof routemsg);

		if(son_sendpkt(BROADCAST_NODEID, &sendbuf, son_conn) == -1){
			printf("sip:  can't send routeupdate msg\n");
		//	break;
		}
	}
	pthread_exit(NULL);
}


//这个线程处理来自SON进程的进入报文. 它通过调用son_recvpkt()接收来自SON进程的报文.
//如果报文是SIP报文,并且目的节点就是本节点,就转发报文给STCP进程. 如果目的节点不是本节点,
//就根据路由表转发报文给下一跳.如果报文是路由更新报文,就更新距离矢量表和路由表.
void* pkthandler(void* arg) 
{
	sip_pkt_t pkt;
	int level = topology_getMyNodeID();
	int nbr_num = topology_getNbrNum();
	int node_num = topology_getNodeNum();

	while(son_recvpkt(&pkt,son_conn)>0) {
		printf("Routing: received a packet from neighbor %d\n",pkt.header.src_nodeID);
		switch(pkt.header.type){
			case ROUTE_UPDATE:
				printf("receive ROUTE UPDATE pkt\n");
				int i=0;
				for(i = 0; i< nbr_num + 1;i++)
				{
					if(dv[i].nodeID == pkt.header.src_nodeID)
						break;
				}
				int key = i;//renew dv[key]
				int entrynum = *(int *)(pkt.data);
				routeupdate_entry_t * entry = malloc(sizeof(routeupdate_entry_t)*entrynum);
				memcpy(entry,&(pkt.data[4]),entrynum*sizeof(routeupdate_entry_t));

				int j=0;
				for(i=0; i < entrynum; i++){
					for(j = 0; j < node_num; j++){
						if(dv[key].dvEntry[j].nodeID == entry[i].nodeID)
							dv[key].dvEntry[j].cost = entry[i].cost;
					}
				}

				int local =0;
				for(i=0; i< nbr_num + 1; i++){
					if(dv[i].nodeID == level) {
						local = i;
						break;
					}
				}

				int slotnum = makehash(level);// route table
				for(i=0; i < node_num; i++)
					for(j=0; j < node_num; j++)
					{
						if(dvtable_getcost(dv,level,dv[local].dvEntry[i].nodeID)>dvtable_getcost(dv,level,dv[local].dvEntry[j].nodeID)+dvtable_getcost(dv,dv[local].dvEntry[j].nodeID,dv[local].dvEntry[i].nodeID))
						{
							dvtable_setcost(dv,level,dv[local].dvEntry[i].nodeID,dvtable_getcost(dv,level,dv[local].dvEntry[j].nodeID)+dvtable_getcost(dv,dv[local].dvEntry[j].nodeID,dv[local].dvEntry[i].nodeID));
							routingtable_entry_t *head =routingtable->hash[slotnum];
							while(head != NULL)
							{
								if(head->destNodeID == dv[local].dvEntry[i].nodeID)
								{
									printf("update route: %d\n",dv[local].dvEntry[i].nodeID);
									printf("change nextnodeID to %d\n\n",dv[local].dvEntry[j].nodeID);
									head->nextNodeID = dv[local].dvEntry[j].nodeID;
									break;
								}
							}
							if(head ==NULL)
							{
								printf("update route && add: %d to route\n",dv[local].dvEntry[i].nodeID);
								printf("change nextnodeID to %d\n\n",dv[local].dvEntry[j].nodeID);
								head = malloc(sizeof(routingtable_entry_t));
								head->destNodeID = dv[local].dvEntry[i].nodeID;
								head->nextNodeID = dv[local].dvEntry[j].nodeID;
								head->next = routingtable->hash[slotnum];
								routingtable->hash[slotnum] =head;
							}
						}

					}
				break;
			case SIP:
				if(pkt.header.dest_nodeID == topology_getMyNodeID()){
					seg_t seg_data;
					memcpy(&seg_data, pkt.data, sizeof(seg_t));
					if(forwardsegToSTCP(stcp_conn, pkt.header.src_nodeID, &seg_data) == -1)
						printf("can't forward seg to STCP \n");
					else
						printf("forward seg to STCP \n");
				}
				else{
					pthread_mutex_lock(routingtable_mutex);
					int nextNodeID = routingtable_getnextnode(routingtable, pkt.header.dest_nodeID);
					pthread_mutex_unlock(routingtable_mutex);
					if(son_sendpkt(nextNodeID, &pkt, son_conn) == -1)
						printf("can't send pkt to son \n");
					else
						printf("receive PKT & transmit it to nextNodeID %d\n",nextNodeID);
				}
				break;
		}
	}


	pthread_exit(NULL);
}

//这个函数终止SIP进程, 当SIP进程收到信号SIGINT时会调用这个函数. 
//它关闭所有连接, 释放所有动态分配的内存.
void sip_stop() 
{
	pthread_mutex_destroy(dv_mutex);
	pthread_mutex_destroy(routingtable_mutex);
	free(dv_mutex);
	free(routingtable_mutex);
	close(son_conn);
	close(stcp_conn);
	son_conn = -1;
	stcp_conn = -1;
	return;
}

//这个函数打开端口SIP_PORT并等待来自本地STCP进程的TCP连接.
//在连接建立后, 这个函数从STCP进程处持续接收包含段及其目的节点ID的sendseg_arg_t. 
//接收的段被封装进数据报(一个段在一个数据报中), 然后使用son_sendpkt发送该报文到下一跳. 下一跳节点ID提取自路由表.
//当本地STCP进程断开连接时, 这个函数等待下一个STCP进程的连接.
void waitSTCP() 
{
	int listenfd;
	socklen_t clilen;
	struct sockaddr_in cliaddr, servaddr;

	if( (listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
		perror("error in creating the socket");
		exit(2);
	}

	int opt = 1;
	setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
	clilen = sizeof(cliaddr);
	memset(&servaddr,0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servaddr.sin_port = htons(SIP_PORT);
	bind(listenfd, (struct sockaddr *) &servaddr, sizeof(servaddr));
	listen(listenfd, LISTENQ);
	printf("sip running...waiting for STCP connection.\n");

	while(1){
		stcp_conn = accept(listenfd, (struct sockaddr *) &cliaddr, &clilen);
		while(1){
			int dest_nodeID;
			seg_t segPtr;
			if(getsegToSend(stcp_conn, &dest_nodeID, &segPtr)<=0) break;
			printf("receive from STCP ============================================\n");
			printf("dest_nodeID:%d--------\n", dest_nodeID);
			pthread_mutex_lock(routingtable_mutex);
			int nextNodeID = routingtable_getnextnode(routingtable, dest_nodeID);
			printf("get from ROUTE & nextNodeID %d\n",nextNodeID);
			pthread_mutex_unlock(routingtable_mutex);
			sip_pkt_t* pkt = malloc(sizeof(sip_pkt_t));
			pkt->header.src_nodeID = topology_getMyNodeID();
			pkt->header.dest_nodeID = dest_nodeID;
			pkt->header.length = sizeof(seg_t);
			pkt->header.type =SIP;
			memcpy(pkt->data, &segPtr, sizeof(seg_t));
			printf("sending pkt to SON============================================\n");
			son_sendpkt(nextNodeID, pkt, son_conn);
		}
		printf("---------------------------------------------------------------end\n");
	}
	close(listenfd);
}


int main(int argc, char *argv[]) 
{
	printf("SIP layer is starting, pls wait...\n");

	topology_analysis();
	//初始化全局变量
	nct = nbrcosttable_create();
	dv = dvtable_create();
	dv_mutex = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(dv_mutex,NULL);
	routingtable = routingtable_create();
	routingtable_mutex = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
	pthread_mutex_init(routingtable_mutex,NULL);
	son_conn = -1;
	stcp_conn = -1;

	nbrcosttable_print(nct);
	dvtable_print(dv);
	routingtable_print(routingtable);

	//注册用于终止进程的信号句柄
	signal(SIGINT, sip_stop);

	//连接到本地SON进程 
	son_conn = connectToSON();
	if(son_conn<0) {
		printf("can't connect to SON process\n");
		exit(1);		
	}

	//启动线程处理来自SON进程的进入报文 
	pthread_t pkt_handler_thread; 
	pthread_create(&pkt_handler_thread,NULL,pkthandler,(void*)0);

	//启动路由更新线程 
	pthread_t routeupdate_thread;
	pthread_create(&routeupdate_thread,NULL,routeupdate_daemon,(void*)0);	

	printf("SIP layer is started...\n");
	printf("waiting for routes to be established\n");
	sleep(SIP_WAITTIME);
	routingtable_print(routingtable);

	//等待来自STCP进程的连接
	printf("waiting for connection from STCP process\n");
	waitSTCP(); 

}


