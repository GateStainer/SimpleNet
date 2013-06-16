//文件名: son/neighbortable.c
//
//描述: 这个文件实现用于邻居表的API
//
//创建日期: 2013年1月

#include "neighbortable.h"

//这个函数首先动态创建一个邻居表. 然后解析文件topology/topology.dat, 填充所有条目中的nodeID和nodeIP字段, 将conn字段初始化为-1.
//返回创建的邻居表.
nbr_entry_t* nt_create()
{
	int nbrNum = topology_getNbrNum();
	nbr_entry_t *nbrList = (nbr_entry_t *)malloc(nbrNum * sizeof (nbr_entry_t));
	int *nbrIDlist = topology_getNbrArray();
	in_addr_t *nbrIPlist = topology_getNbrIpArray();
	int i = 0;
	for(i = 0; i < nbrNum; i++){
		nbrList[i].nodeID = nbrIDlist[i];
		nbrList[i].nodeIP = nbrIPlist[i];
		nbrList[i].conn = -1;
	}
	return nbrList;
}

//这个函数删除一个邻居表. 它关闭所有连接, 释放所有动态分配的内存.
void nt_destroy(nbr_entry_t* nt)
{
  return;
}

//这个函数为邻居表中指定的邻居节点条目分配一个TCP连接. 如果分配成功, 返回1, 否则返回-1.
int nt_addconn(nbr_entry_t* nt, int nodeID, int conn)
{
	int nbrNum = topology_getNbrNum();
	int i = 0;
	for(i = 0;i < nbrNum; i++){
		if(nt[i].nodeID == nodeID){
			nt[i].conn = conn;
			return 1;
		}
	}
	return -1;
}

int nt_addconnByIP(nbr_entry_t* nt, in_addr_t nodeIP, int conn)
{
	int nbrNum = topology_getNbrNum();
	int i = 0;
	for(i = 0;i < nbrNum; i++){
		if(nt[i].nodeIP == nodeIP){
			nt[i].conn = conn;
			return 1;
		}
	}
	return -1;
}
