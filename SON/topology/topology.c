//文件名: topology/topology.c
//
//描述: 这个文件实现一些用于解析拓扑文件的辅助函数 
//
//创建日期: 2013年1月

#include "topology.h"

//这个函数返回指定主机的节点ID.
//节点ID是节点IP地址最后8位表示的整数.
//例如, 一个节点的IP地址为202.119.32.12, 它的节点ID就是12.
//如果不能获取节点ID, 返回-1.

static int *nbrIDArray;
static in_addr_t *nbrIPArray;
static int *nodeArray;
static int nbrNum = 0;
static int nodeNum = 0;
static int lessNbr = 0;

int searchInArray(int a[], int key, int num)
{
	int i = 0;
	for(i = 0; i < num; i++)
		if(a[i] == key)
			return i;
	return -1;
}

int topology_getNodeIDfromname(char* hostname) 
{
	struct hostent *h = gethostbyname(hostname);
	if(h == NULL)
		return -1;
	unsigned char node_id = ntohl(((struct in_addr *)h->h_addr_list[0])->s_addr);
	return node_id;
}

in_addr_t topology_getNodeIPfromname(char *hostname)
{
	struct hostent *h = gethostbyname(hostname);
	if(h == NULL)
		return -1;
	//network order
	in_addr_t node_ip =((struct in_addr *)h->h_addr_list[0])->s_addr;
	return node_ip;
}

//这个函数返回指定的IP地址的节点ID.
//如果不能获取节点ID, 返回-1.
int topology_getNodeIDfromip(struct in_addr* addr)
{
	unsigned char node_id = ntohl(addr->s_addr);
	return node_id;
}

//这个函数返回本机的节点ID
//如果不能获取本机的节点ID, 返回-1.
int topology_getMyNodeID()
{
	char localhostname[30];
	gethostname(localhostname, 30);
	return topology_getNodeIDfromname(localhostname);
}

//返回邻居数.
int topology_getNbrNum()
{
	return nbrNum;
}

int topology_getLessNum()
{
	return lessNbr;
}

int topology_getGreatNum()
{
	return nbrNum - lessNbr;
}

//返回重叠网络中的总节点数.
int topology_getNodeNum()
{ 
	return nodeNum;
}

//返回一个动态分配的数组, 它包含重叠网络中所有节点的ID. 
int* topology_getNodeArray()
{
	return nodeArray;
}

//返回一个动态分配的数组, 它包含所有邻居的节点ID.  
int* topology_getNbrArray()
{
	return nbrIDArray;
}

in_addr_t *topology_getNbrIpArray()
{
	return nbrIPArray;
}
//返回指定两个节点之间的直接链路代价. 
//如果指定两个节点之间没有直接链路, 返回INFINITE_COST.
unsigned int topology_getCost(int fromNodeID, int toNodeID)
{
	char host1[20], host2[20];
	int cost, host1ID, host2ID;
	FILE *pFile = fopen("../topology/topology.dat", "r");
	if(pFile == NULL)
		perror("Error opening topology");
	while(fscanf(pFile,"%s %s %d", host1, host2, &cost) > 0){
		host1ID = topology_getNodeIDfromname(host1);
		host2ID = topology_getNodeIDfromname(host2);
		if(host1ID == fromNodeID && host2ID == toNodeID)
			return cost;
	}

	fclose(pFile);
	return INFINITE_COST;
}

void topology_analysis()
{
	char host1[20], host2[20];
	int cost, host1ID, host2ID;
	in_addr_t host1IP, host2IP;
	int node[20], nbr[20];
	in_addr_t nip[20];
	int myNodeID = topology_getMyNodeID();
	FILE *pFile = fopen("../topology/topology.dat", "r");
	if(pFile == NULL)
		perror("Error opening topology");
	while(fscanf(pFile,"%s %s %d", host1, host2, &cost) > 0){
		host1ID = topology_getNodeIDfromname(host1);
		host1IP = topology_getNodeIPfromname(host1);
		host2ID = topology_getNodeIDfromname(host2);
		host2IP = topology_getNodeIPfromname(host2);

		if(host1ID == myNodeID){
			nbr[nbrNum] = host2ID;
			nip[nbrNum] = host2IP;
			nbrNum++;
			if(host2ID < myNodeID)
				lessNbr++;
		}
		else if(host2ID == myNodeID){
			nbr[nbrNum] = host1ID;
			nip[nbrNum] = host1IP;
			nbrNum++;
			if(host1ID < myNodeID)
				lessNbr++;
		}
		if(searchInArray(node, host1ID, nodeNum) == -1){
			node[nodeNum] = host1ID;
			nodeNum++;
		}
		if(searchInArray(node, host2ID, nodeNum) == -1){
			node[nodeNum] = host2ID;
			nodeNum++;
		}

	}

	int i = 0;
	nbrIDArray = (int *)malloc(nbrNum * sizeof (int));
	for(i = 0; i < nbrNum; i++)
		nbrIDArray[i] = nbr[i];

	nbrIPArray = (in_addr_t *)malloc(nbrNum * sizeof (in_addr_t));
	for(i = 0; i < nbrNum; i++)
		nbrIPArray[i] = nip[i];


	nodeArray = (int *)malloc(nodeNum * sizeof (int));
	for(i = 0; i < nodeNum; i++)
		nodeArray[i] = node[i];

	fclose(pFile);
}

#ifdef DEBUG
int debugTopology()
{
	topology_analysis();
	printf("nbr :%d node %d \n", topology_getNbrNum(), topology_getNodeNum());
	int i = 0;
	for(i = 0; i < nbrNum; i++)
		printf("%d ", nbrIDArray[i]);
	printf("\n");
	for(i = 0; i < nodeNum; i++)
		printf("%d ", nodeArray[i]);
	printf("\n");
}
#endif
