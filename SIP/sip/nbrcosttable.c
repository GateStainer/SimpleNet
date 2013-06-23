
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include "nbrcosttable.h"
#include "../common/constants.h"
#include "../topology/topology.h"

//这个函数动态创建邻居代价表并使用邻居节点ID和直接链路代价初始化该表.
//邻居的节点ID和直接链路代价提取自文件topology.dat. 
nbr_cost_entry_t* nbrcosttable_create()
{
	int nbrnum = topology_getNbrNum();
	nbr_cost_entry_t *nbrcost_table = (nbr_cost_entry_t *) malloc(nbrnum * sizeof(nbr_cost_entry_t));
	int *nbrArray = topology_getNbrArray();
	int i = 0, mynodeID = topology_getMyNodeID();
	for(i = 0; i < nbrnum; i++){
		nbrcost_table[i].nodeID = nbrArray[i];
		nbrcost_table[i].cost = topology_getCost(mynodeID, nbrArray[i]);
	}
	return nbrcost_table;
}

//这个函数删除邻居代价表.
//它释放所有用于邻居代价表的动态分配内存.
void nbrcosttable_destroy(nbr_cost_entry_t* nct)
{
	free(nct);
	return;
}

//这个函数用于获取邻居的直接链路代价.
//如果邻居节点在表中发现,就返回直接链路代价.否则返回INFINITE_COST.
unsigned int nbrcosttable_getcost(nbr_cost_entry_t* nct, int nodeID)
{
	int nbrnum = topology_getNbrNum();
	int i = 0;
	for(i = 0; i < nbrnum; i++){
		if(nct[i].nodeID == nodeID)
			return nct[i].cost;
	}
	return INFINITE_COST;
}

//这个函数打印邻居代价表的内容.
void nbrcosttable_print(nbr_cost_entry_t* nct)
{
	printf("-----------------nbrcost table ----------------------\n");
	int nbrnum = topology_getNbrNum();
	int mynodeID = topology_getMyNodeID();
	int i = 0;
	for(i = 0; i < nbrnum; i++){
		printf("%d=================>%d: %d\n", mynodeID, nct[i].nodeID, nct[i].cost);
	}
	printf("------------------end of nbrcost table----------------\n");
	return;
}
