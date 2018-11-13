#include "monitor_neighbors.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>

#include <inttypes.h>

#define LENGTH   256
#define MAXTIME   99999

extern int globalMyID;
//last time you heard from each node. TODO: you will want to monitor this
//in order to realize when a neighbor has gotten cut off from you.
extern struct timeval globalLastHeartbeat[256];

//our all-purpose UDP socket, to be bound to 10.1.1.globalMyID, port 7777
extern int globalSocketUDP;
//pre-filled for sending to 10.1.1.0 - 255, port 7777
extern struct sockaddr_in globalNodeAddrs[256];

extern char *fileName;
extern struct FTEntry forwardTable[LENGTH];
extern char globalLSP[MAX_len];
extern unsigned int length;

unsigned int *neighborList;
extern int globalMyID;
extern int globalSocketUDP;
extern struct sockaddr_in globalNodeAddrs[256];
extern struct FTEntry forwardTable[256];
char globalLSP[MAX_len];
unsigned int length;

unsigned int getInfoFromSendBuf(unsigned char *buf, uint16_t *destID, char *msg, unsigned int buf_length)
{
    unsigned int msgL = buf_length - 4 - sizeof(uint16_t);
    buf += 4;
    memcpy(destID, buf, sizeof(uint16_t));
    *destID = ntohs(*destID);
    memcpy(msg, buf + sizeof(uint16_t), msgL);
    return msgL;
}

void getInfoFromCostBuf(unsigned char *buf, uint16_t *neighborID, uint32_t *nwCost)
{
    buf += 4;
    memcpy(neighborID, buf, sizeof(uint16_t));
    memcpy(nwCost, buf + sizeof(uint16_t), sizeof(uint32_t));
    *neighborID = ntohs(*neighborID);
    *nwCost = ntohl(*nwCost);
}

void wfile(char *buf, unsigned int buf_length)
{
    FILE *fp = fopen(fileName, "a");
    fwrite(buf, sizeof(char), buf_length, fp);
    fclose(fp);
}

void writeForwardLog(uint16_t dest, uint16_t nexthop, char *msg, unsigned int msgL)
{
    char buf[1000];
    unsigned int count;
    
    memset(buf, '\0', 1000);
    count = (unsigned int)sprintf(buf, "forward packet dest %"PRIu16" nexthop %"PRIu16" message ", dest, nexthop);
    strncpy(buf + count, msg, msgL);
    buf[count + msgL] = '\n';
    
    wfile(buf, count + msgL + 1);
}

int getIndex(int i, int j)
{
    return ((i - 1) * i) / 2 + j;
}

void neighborLinkelistInit()
{
    int i;
    neighborList = (unsigned int *)malloc(neighborList_LEN * sizeof(int));
    for (i = 0; i < neighborList_LEN; i ++) {
        neighborList[i] = 0;
    }
}

unsigned int gtCost(int i, int j)
{
    if (i < j) {
        int tmp = i;
        i = j;
        j = tmp;
    }
    return neighborList[getIndex(i, j)];
}

int isConnected(int i, int j)
{
    if (gtCost(i, j) == 0) {
        return 0;
    } else {
        return 1;
    }
}

void stCost(int i, int j, unsigned int nwCost)
{
    if (i < j) {
        int tmp = i;
        i = j;
        j = tmp;
    }
    neighborList[getIndex(i, j)] = nwCost;
}

unsigned int stLSPBuf(char *buf, int dest)
{
    uint16_t node = htons((uint16_t)dest);
    uint32_t seqNum = htonl(forwardTable[dest].seqNum);
    int i, count = 0;
    
    sprintf(buf, "LSP");
    memcpy(buf + 3, &node, sizeof(uint16_t));
    for (i = 0; i < 256; i ++) {
        if (i == dest) {
            continue;
        }
        if (isConnected(dest, i)) {
            uint16_t neighbor = htons((uint16_t)i);
            uint32_t cost = htonl((uint32_t)gtCost(dest, i));
            memcpy(buf + 3 + sizeof(uint16_t) + SIZE * count, &neighbor, sizeof(uint16_t));
            memcpy(buf + 3 + sizeof(uint16_t) * 2 + SIZE * count, &cost, sizeof(uint32_t));
            count ++;
        }
    }
    if (count == 0) {
        return 0;
    }
    memcpy(buf + 3 + sizeof(uint16_t) + SIZE * count, &seqNum, sizeof(uint32_t));
    return 3 + SIZE * (count + 1);
}

void BC(int start, char *LSP, unsigned int LSP_length)
{
    int i;
    for (i = 0; i < 256; i ++) {
        if (i != start && i != globalMyID) {
            if (isConnected(i, globalMyID)) {
                sendto(globalSocketUDP, LSP, LSP_length, 0, (struct sockaddr*)&globalNodeAddrs[i], sizeof(globalNodeAddrs[i]));
            }
        }
    }
}

void bcTP(int block)
{
    BC(block, globalLSP, length);
}

void getInfoFromLSPBuf(char *buf, uint16_t *node, uint16_t *neighbors, uint32_t *costs, int count, uint32_t *seqNum, unsigned int buf_length)
{
    int i;
    
    memcpy(node, buf + 3, sizeof(uint16_t));
    *node = ntohs(*node);
    memcpy(seqNum, buf + 3 + sizeof(uint16_t) + SIZE * count, sizeof(uint32_t));
    *seqNum = ntohl(*seqNum);
    
    for (i = 0; i < count; i ++) {
        memcpy(&neighbors[i], buf + 3 + sizeof(uint16_t) + SIZE * i, sizeof(uint16_t));
        memcpy(&costs[i], buf + 3 + sizeof(uint16_t) * 2 + SIZE * i, sizeof(uint32_t));
        neighbors[i] = ntohs(neighbors[i]);
        costs[i] = ntohl(costs[i]);
    }
}

unsigned int *DFS(int *count)
{
    unsigned int queue[256];
    int i, j, seqCount = 1;
    queue[0] = globalMyID;
    unsigned int dfs_result[256];
    *count = 0;
    unsigned int dest;
    
    while (seqCount > 0) {
        seqCount --;
        dest = queue[seqCount];
        int isAdded = 0;
        for (i = 0; i < *count; i ++) {
            if (dest == (int)dfs_result[i]) {
                isAdded = 1;
                break;
            }
        }
        if (isAdded) {
            continue;
        }
        dfs_result[*count] = dest;
        (*count) ++;
        for (i = 0; i < 256; i ++) {
            if ((int)dest == i) {
                continue;
            }
            if (isConnected((int)dest, i)) {
                int isAdded = 0;
                for (j = 0; j < *count; j ++) {
                    if (i == (int)dfs_result[j]) {
                        isAdded = 1;
                        break;
                    }
                }
                if (!isAdded) {
                    queue[seqCount] = i;
                    seqCount ++;
                }
            }
        }
    }
    
    unsigned int *result = (unsigned int *)malloc(*count * sizeof(unsigned int));
    for (i = 0; i < *count; i ++) {
        result[i] = dfs_result[i];
    }
    
    return result;
}

void sendWholeTopology(int dest)
{
    char *LSP = (char *)malloc(MAX_len);
    unsigned int *dfs;
    int i, count;
    dfs = DFS(&count);
    for (i = 0; i < count; i ++) {
        unsigned int LSP_length = stLSPBuf(LSP, (int)dfs[i]);
        sendto(globalSocketUDP, LSP, LSP_length, 0, (struct sockaddr*)&globalNodeAddrs[dest], sizeof(globalNodeAddrs[dest]));
    }
    free(LSP);
    LSP = NULL;
}

void flushSequenceNumber()
{
    unsigned int *dfs;
    int i, j, count;
    dfs = DFS(&count);
    for (i = 0; i < 256; i ++) {
        if (i == globalMyID) {
            continue;
        }
        for (j = 1; j < count; j ++) {
            if (i == dfs[j]) {
                break;
            }
        }
        forwardTable[i].seqNum = 0;
    }
}

struct LSPlist gtTent(struct LSPlist *tentlist, int tentnum, int dest)
{
    struct LSPlist result = { .dest = -1, .cost = 0, .nextHop = -1 };
    int i;
    for (i = 0; i < tentnum; i ++) {
        if (tentlist[i].dest == dest) {
            return tentlist[i];
        }
    }
    return result;
}

void setTentativeElement(struct LSPlist *tentlist, int tentnum, struct LSPlist sample)
{
    int i;
    for (i = 0; i < tentnum; i ++) {
        if (tentlist[i].dest == sample.dest) {
            tentlist[i] = sample;
            break;
        }
    }
}

struct LSPlist *uptentlist(struct LSPlist *tentlist, int *tentnum, int source_node)
{
    int source_node_cost = forwardTable[source_node].cost;
    int i;
    for (i = 0; i < 256; i ++) {
        if (i == source_node) {
            continue;
        }
        if (isConnected(source_node, i) && forwardTable[i].cost == -1) {
            int linkCost = gtCost(source_node, i);
            struct LSPlist tmp = gtTent(tentlist, *tentnum, i);
            if (tmp.dest != -1) {
                // dest i exist in tentlist
                // update i
                if (tmp.cost > source_node_cost + linkCost) {
                    // update node i in tentlist
                    tmp.cost = source_node_cost + linkCost;
                    tmp.destpre = source_node;
                    tmp.nextHop = forwardTable[source_node].nextHop;
                    setTentativeElement(tentlist, *tentnum, tmp);
                } else if (tmp.cost == source_node_cost + linkCost) {
                    // tie break
                    if (tmp.destpre > source_node) {
                        tmp.cost = source_node_cost + linkCost;
                        tmp.destpre = source_node;
                        tmp.nextHop = forwardTable[source_node].nextHop;
                        setTentativeElement(tentlist, *tentnum, tmp);
                    }
                }
            } else {
                // add i into tentlist
                if (*tentnum == 0) {
                    tentlist = (struct LSPlist*)malloc(sizeof(struct LSPlist));
                } else {
                    tentlist = (struct LSPlist*)realloc(tentlist, (*tentnum + 1) * sizeof(struct LSPlist));
                }
                tentlist[*tentnum].dest = i;
                tentlist[*tentnum].cost = source_node_cost + linkCost;
                tentlist[*tentnum].destpre = source_node;
                if (forwardTable[source_node].nextHop == -1) {
                    tentlist[*tentnum].nextHop = i;
                } else {
                    tentlist[*tentnum].nextHop = forwardTable[source_node].nextHop;
                }
                *tentnum = *tentnum + 1;
            }
        }
    }
    return tentlist;
}

struct LSPlist getSmallestCostElement(struct LSPlist *tentlist, int tentnum)
{
    struct LSPlist result = tentlist[0];
    int i;
    for (i = 0; i < tentnum; i ++) {
        if (result.cost > tentlist[i].cost) {
            result = tentlist[i];
        }
    }
    return result;
}

struct LSPlist *updateForwardingTable(struct LSPlist *tentlist, int *tentnum, int *source_node)
{
    if (*tentnum == 0) {
        *source_node = -1;
        return tentlist;
    }
    struct LSPlist smallestCostElement = getSmallestCostElement(tentlist, *tentnum);
    *tentnum = *tentnum - 1;
    *source_node = smallestCostElement.dest;
    
    // set forwardTable
    forwardTable[smallestCostElement.dest].cost = smallestCostElement.cost;
    forwardTable[smallestCostElement.dest].nextHop = smallestCostElement.nextHop;
    
    if (*tentnum == 0) {
        return NULL;
    }
    struct LSPlist *result = (struct LSPlist*)malloc(*tentnum * sizeof(struct LSPlist));
    int i, count = 0;
    for (i = 0; i < *tentnum + 1; i ++) {
        if (smallestCostElement.dest != tentlist[i].dest) {
            memcpy(&result[count], &(tentlist[i]), sizeof(struct LSPlist));
            count ++;
        }
    }
    free(tentlist);
    tentlist = NULL;
    return result;
}

void dijkstra()
{
    struct LSPlist *tentlist;
    int i, tentnum = 0, srcNode = globalMyID;
    
    for (i = 0; i < 256; i ++) {
        forwardTable[i].nextHop = -1;
        forwardTable[i].cost = -1;
    }
    forwardTable[globalMyID].cost = 0;
    
    while (srcNode != -1) {
        tentlist = uptentlist(tentlist, &tentnum, srcNode);
        tentlist = updateForwardingTable(tentlist, &tentnum, &srcNode);
    }
    return;
}

void wSL(uint16_t dest, uint16_t nexthop, char *msg, unsigned int msgL)
{
    char buf[1000];
    unsigned int count;
    
    memset(buf, '\0', 1000);
    count = (unsigned int)sprintf(buf, "sending packet dest %"PRIu16" nexthop %"PRIu16" message ", dest, nexthop);
    strncpy(buf + count, msg, msgL);
    buf[count + msgL] = '\n';
    
    wfile(buf, count + msgL + 1);
}

void wRv(char *msg, unsigned int msgL)
{
    char buf[1000];
    unsigned int count;
    
    memset(buf, '\0', 1000);
    count = (unsigned int)sprintf(buf, "receive packet message ");
    strncpy(buf + count, msg, msgL);
    buf[count + msgL] = '\n';
    
    wfile(buf, count + msgL + 1);
}

void writeUnreachableLog(uint16_t dest)
{
    char buf[1000];
    unsigned int count;
    
    memset(buf, '\0', 1000);
    count = (unsigned int)sprintf(buf, "unreachable dest %"PRIu16"\n", dest);
    
    wfile(buf, count);
}

void forwardSend(int nexthop, char *buf, unsigned int buf_length)
{
    sendto(globalSocketUDP, buf, buf_length, 0, (struct sockaddr*)&globalNodeAddrs[nexthop], sizeof(globalNodeAddrs[nexthop]));
}

unsigned int TimD(struct timeval after, struct timeval before)
{
    return (after.tv_sec - before.tv_sec) * 1000000 + after.tv_usec - before.tv_usec;
}

void* monitorNeighborAlive(void* unusedParam)
{
    int i;
    struct timeval current;
    struct timespec sleepFor;
    sleepFor.tv_sec = 0;
    sleepFor.tv_nsec = 500 * 1000 * 1000; //500 ms
    
    while (1) {
        int has_disconnect = 0;
        for (i = 0; i < 256; i ++) {
            if (globalMyID == i) {
                continue;
            }
            if (isConnected(globalMyID, i)) {
                gettimeofday(&current, 0);
                if (TimD(current, globalLastHeartbeat[i]) > MAXTIME) {
                    stCost(globalMyID, i, 0);
                    has_disconnect = 1;
                }
            }
        }
        //TODO: broadcast to others that the node loses a connection
        if (has_disconnect) {
            /*BDL(globalMy_ID);*/
        }
        nanosleep(&sleepFor, 0);
    }
}

//Yes, this is terrible. It's also terrible that, in Linux, a socket
//can't receive broadcast packets unless it's bound to INADDR_ANY,
//which we can't do in this assignment.
// hackyBroadcast will broadcast a message to all other nodes
void hackyBroadcast(const char* buf, int length)
{
    int i;
    for(i=0;i<256;i++)
        if(i != globalMyID) //(although with a real broadcast you would also get the packet yourself)
            sendto(globalSocketUDP, buf, length, 0,
                   (struct sockaddr*)&globalNodeAddrs[i], sizeof(globalNodeAddrs[i]));
}

void* announceToNeighbors(void* unusedParam)
{
    struct timespec sleepFor;
    sleepFor.tv_sec = 0;
    sleepFor.tv_nsec = 300 * 1000 * 1000; //300 ms
    while(1)
    {
        forwardTable[globalMyID].seqNum ++;
        hackyBroadcast("HEREIAM", 7);
        nanosleep(&sleepFor, 0);
    }
}

void listenForNeighbors()
{
    char fromAddr[100];
    struct sockaddr_in theirAddr;
    socklen_t theirAddrLen;
    unsigned char recvBuf[1000];
    
    int bytesRecvd;
    while(1)
    {
        theirAddrLen = sizeof(theirAddr);
        recvBuf[0] = '\0';
        if ((bytesRecvd = recvfrom(globalSocketUDP, recvBuf, 1000 , 0,
                                   (struct sockaddr*)&theirAddr, &theirAddrLen)) == -1)
        {
            perror("connectivity listener: recvfrom failed");
            exit(1);
        }
        
        inet_ntop(AF_INET, &theirAddr.sin_addr, fromAddr, 100);
        
        short int heardFrom = -1;
        if(strstr(fromAddr, "10.1.1."))
        {
            // heardFrom is the id of the node from where recvFrom() gets the msg
            heardFrom = atoi(
                             strchr(strchr(strchr(fromAddr,'.')+1,'.')+1,'.')+1);
            
            //TODO: this node can consider heardFrom to be directly connected to it; do any such logic now.
            if (!isConnected(globalMyID, heardFrom)) {
                stCost(globalMyID, heardFrom, forwardTable[heardFrom].defaultCost);
                //TODO: broadcast to other nodes that the node comes up a connection
            }
            
            //record that we heard from heardFrom just now.
            gettimeofday(&globalLastHeartbeat[heardFrom], 0);
        }
        
        //Is it a packet from the manager? (see mp2 specification for more details)
        //send format: 'send'<4 ASCII bytes>, destID<net order 2 byte signed>, <some ASCII message>
        if(!strncmp(recvBuf, "send", 4))
        {
            dijkstra();
            uint16_t destID;
            char msg[LENGTH];
            memset(msg, '\0', LENGTH);
            unsigned int msgL = getInfoFromSendBuf(recvBuf, &destID, msg, bytesRecvd);
            
            //TODO send the requested msg to the requested destination node
            // ...
            
            if (globalMyID == (int)destID) {
                // receive msg
                wRv(msg, msgL);
            } else {
                int nexthop = forwardTable[destID].nextHop;
                
                if (nexthop == -1) {
                    // unreachable
                    writeUnreachableLog(destID);
                } else {
                    forwardSend(forwardTable[destID].nextHop, recvBuf, bytesRecvd);
                    if (strncmp(fromAddr, "10.0.0.10", 9) == 0) {
                        wSL(destID, (uint16_t)nexthop, msg, msgL);
                    } else {
                        writeForwardLog(destID, (uint16_t)nexthop, msg, msgL);
                    }
                }
            }
        }
        //'cost'<4 ASCII bytes>, destID<net order 2 byte signed> nwCost<net order 4 byte signed>
        else if(!strncmp(recvBuf, "cost", 4))
        {
            uint16_t neighbor;
            uint32_t nwCost;
            
            getInfoFromCostBuf(recvBuf, &neighbor, &nwCost);
            
            //TODO record the cost change (remember, the link might currently be down! in that case,
            //this is the new cost you should treat it as having once it comes back up.)
            forwardTable[neighbor].defaultCost = nwCost;
            if (isConnected(globalMyID, neighbor)) {
                stCost(globalMyID, neighbor, nwCost);
            }
        }
        
        //TODO now check for the various types of packets you use in your own protocol
        else if(!strncmp(recvBuf, "LSP", 3))
        {
            uint16_t node;
            uint32_t seqNum;
            int i, count = (bytesRecvd - 3 - sizeof(uint16_t) * 2) / SIZE;
            uint16_t *neighbors = malloc(count * sizeof(uint16_t));
            uint32_t *costs = malloc(count * sizeof(uint32_t));
            getInfoFromLSPBuf(recvBuf, &node, neighbors, costs, count, &seqNum, bytesRecvd);
            
            // forward LSP
            if (seqNum > forwardTable[node].seqNum) {
                int j;
                // check if the connections still exist
                for (i = 0; i < 256; i ++) {
                    if (i == node) {
                        continue;
                    }
                    if (isConnected(node, i)) {
                        int isConnect = 0;
                        for (j = 0; j < count; j ++) {
                            if (neighbors[j] == (uint16_t)i) {
                                isConnect = 1;
                            }
                        }
                        if (!isConnect) {
                            /*neighborList_need_flush = 1;*/
                            stCost(node, i, 0);
                        }
                    }
                }
                for (i = 0; i < count; i ++) {
                    stCost(neighbors[i], node, costs[i]);
                }
                forwardTable[node].seqNum = seqNum;
                BC(heardFrom, recvBuf, bytesRecvd);
            }
        }
    }
    //(should never reach here)
    close(globalSocketUDP);
}

void* sendLSP(void* unusedParam)
{
    struct timeval t;
    gettimeofday(&t, NULL);
    srand((unsigned int)(t.tv_sec * 1000000 + t.tv_usec));
    struct timespec sSleep;
    sSleep.tv_sec = 0;
    sSleep.tv_nsec = (rand() % 1000) * 1000 * 1000;
    nanosleep(&sSleep, 0);
    
    struct timespec sleepFor;
    sleepFor.tv_sec = 1;
    sleepFor.tv_nsec = 0;
    while(1)
    {
        length = stLSPBuf(globalLSP, globalMyID);
        bcTP(globalMyID);
        nanosleep(&sleepFor, 0);
    }
}
