
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../common/constants.h"
#include "../topology/topology.h"
#include "dvtable.h"

//这个函数动态创建距离矢量表.
//距离矢量表包含n+1个条目, 其中n是这个节点的邻居数,剩下1个是这个节点本身.
//距离矢量表中的每个条目是一个dv_t结构,它包含一个源节点ID和一个有N个dv_entry_t结构的数组, 其中N是重叠网络中节点总数.
//每个dv_entry_t包含一个目的节点地址和从该源节点到该目的节点的链路代价.
//距离矢量表也在这个函数中初始化.从这个节点到其邻居的链路代价使用提取自topology.dat文件中的直接链路代价初始化.
//其他链路代价被初始化为INFINITE_COST.
//该函数返回动态创建的距离矢量表.
dv_t* dvtable_create()
{
	int nbr_num = topology_getNbrNum();
	int node_num = topology_getNodeNum();
	int mynodeID = topology_getMyNodeID();
	int *nbrArray = topology_getNbrArray();
	int *nodeArray = topology_getNodeArray();
	dv_t *dv_table = (dv_t *)malloc((nbr_num + 1) * sizeof(dv_t));

	int i = 0, j = 0;
	for(i = 0; i < nbr_num; i++){
		dv_table[i].nodeID = nbrArray[i];			//source ID is my nbr
		dv_entry_t *dvEntry = (dv_entry_t *)malloc(node_num * sizeof(dv_entry_t));
		for(j = 0; j < node_num; j++){
			dvEntry[j].nodeID = nodeArray[j];
			dvEntry[j].cost = INFINITE_COST;
		}
		dv_table[i].dvEntry = dvEntry;
	}

	dv_entry_t *dvEntry = (dv_entry_t *)malloc(node_num * sizeof(dv_entry_t));
	for(j = 0; j < node_num; j++){
		dvEntry[j].nodeID = nodeArray[j];
		if(mynodeID == nodeArray[j])
			dvEntry[j].cost = 0;
		else
			dvEntry[j].cost = topology_getCost(mynodeID, nodeArray[j]);
	}
	dv_table[nbr_num].nodeID = mynodeID;			//source ID is myself
	dv_table[nbr_num].dvEntry = dvEntry;

	return dv_table;
}

//这个函数删除距离矢量表.
//它释放所有为距离矢量表动态分配的内存.
void dvtable_destroy(dv_t* dvtable)
{
	int nbr_num = topology_getNbrNum();
	int i = 0;

	for(i = 0; i < nbr_num; i++){
		free(dvtable[i].dvEntry);
	}
	free(dvtable[nbr_num].dvEntry);
	free(dvtable);
}

//这个函数设置距离矢量表中2个节点之间的链路代价.
//如果这2个节点在表中发现了,并且链路代价也被成功设置了,就返回1,否则返回-1.
int dvtable_setcost(dv_t* dvtable,int fromNodeID,int toNodeID, unsigned int cost)
{
	int nbr_num = topology_getNbrNum();
	int node_num = topology_getNodeNum();

	int i = 0, j = 0;
	// i <= nbr_num
	for(i = 0; i <= nbr_num; i++){
		if(fromNodeID == dvtable[i].nodeID){
			for(j = 0; j < node_num; j++){
				if(dvtable[i].dvEntry[j].nodeID == toNodeID){
					dvtable[i].dvEntry[j].cost = cost;
					return 1;
				}
			}
		}
	}
	return -1;
}

//这个函数返回距离矢量表中2个节点之间的链路代价.
//如果这2个节点在表中发现了,就返回链路代价,否则返回INFINITE_COST.
unsigned int dvtable_getcost(dv_t* dvtable, int fromNodeID, int toNodeID)
{
 	int nbr_num = topology_getNbrNum();
	int node_num = topology_getNodeNum();

	int i = 0, j = 0;
	for(i = 0; i <= nbr_num; i++){
		if(fromNodeID == dvtable[i].nodeID){
			for(j = 0; j < node_num; j++){
				if(dvtable[i].dvEntry[j].nodeID == toNodeID)
					return dvtable[i].dvEntry[j].cost;
			}
		}
	}
	return INFINITE_COST;
}

//这个函数打印距离矢量表的内容.
void dvtable_print(dv_t* dvtable)
{
	int nbr_num = topology_getNbrNum();
	int node_num = topology_getNodeNum();

//	printf("\tcsnetlab_1\tcsnetlab_2\tcsnetlab_3\tcsnetlab4\n");
	int i = 0, j = 0;
	for(j = 0; j < node_num; j++)
		printf("\t%d", dvtable[0].dvEntry[j].nodeID);
	printf("\n");
	for(i = 0; i <= nbr_num; i++){
		printf("%d", dvtable[i].nodeID);
		for(j = 0; j < node_num; j++)
			printf("\t%d", dvtable[i].dvEntry[j].cost);
		printf("\n");
	}
	return;
}
