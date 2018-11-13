#include <netinet/in.h>
#include <string.h>
#include <sys/time.h>

#define neighborList_LEN   255 * 256 / 2
#define SIZE        (sizeof(uint16_t) + sizeof(uint32_t))
#define MAX_len           1539

struct FTEntry {
    unsigned int defaultCost;
    uint32_t seqNum;
    int cost;
    int nextHop;
};
struct LSPlist {
    int dest;
    unsigned int cost;
    int destpre;
    int nextHop;
};

void* monitorNeighborAlive(void* unusedParam);
void* announceToNeighbors(void* unusedParam);
void hackyBroadcast(const char* buffer, int length);
void listenForNeighbors();
void wfile(char* buf,unsigned int len);
void* sendLSP(void* unusedParam);
void neighborLinkelistInit();
unsigned int gtCost(int i, int j);
void stCost(int i, int j, unsigned int nwCost);
int isConnected(int i, int j);
unsigned int stLSPBuf(char *buf, int dest);
void BC(int start, char *LSP, unsigned int LSP_length);
void bcTP(int block);
void getInfoFromLSPBuf(char *buf, uint16_t *node, uint16_t *neighbors, uint32_t *costs, int count, uint32_t *sequence_number, unsigned int buf_length);
void sendWholeTopology(int dest);
void flushSequenceNumber();
void dijkstra();

