#include "contiki.h"
#include "net/rime/rime.h"
#include "dev/button-sensor.h"
#include "dev/leds.h"
#include <stdio.h>
#include <time.h>
#include "random.h"
#include <stdlib.h>
   
#define MOTES_COUNT 10
#define RREP_CH 146 
#define RREQ_CH 129 
#define TABLE_SIZE 30 //Route requests and waiting data tables

//Processes
PROCESS(open_broadcast_unicast_connection, "Open Broadcast and Unicast Connections"); 
PROCESS(begin_discovery_on_button_click, "Start Route Discovery by Choosing a Random Destination"); 

AUTOSTART_PROCESSES(&open_broadcast_unicast_connection,
					&begin_discovery_on_button_click); 

static void sendRREQPacket(int reqid, int src,int dest, int hopcount, int srcSeq, int destSeq);
static void sendRREPPacket(int reqid, int src, int dest, int hopcount, int srcSeq, int destSeq);
static void writeRREQTable(int reqId, int src, int dest);
static char alreadySentRREQ(int reqId, int src, int dest);
static char isRrepWithShorterDistance(int reqId, int src, int dest, int hops);
static int getNextHop(int dest);

// Routing Table Varibles
struct routingTableEntry {
  int dest;
  int next;
  int dist;
  int seq; 
};

static struct routingTableEntry routingTable[MOTES_COUNT];

struct sentRREQEntry{
  int reqid;
  int src;
  int dest;
  int minHopsRrep;
};

//Table containing all pending/sent RREQs. Used to discard duplicates
static struct sentRREQEntry rreqPendingTable[TABLE_SIZE];

struct neighbour_node{
	int dest;
	char valid;
};

/* all neighbor nodes which received/send a rrep from/to us are stored in this table, as they might be depending on our routes.
we will monitor the availability of these through the hello messages */
static struct neighbour_node rrepNextHops[MOTES_COUNT];

static int current_req_id = 0;
static int own_sequence = 0;

//Broadcast and Unicast Connections
static struct broadcast_conn broadcast;
static struct broadcast_conn hello_broad;
static struct broadcast_conn rerr_broad;
static struct unicast_conn rrep_conn;
static struct unicast_conn data_conn;

//Function to handle RREQ Received Packet
static void receiveRREQ(struct broadcast_conn *c, const linkaddr_t *from) {
	static char packet[18];
    strncpy(packet, (char *)packetbuf_dataptr(), 18);

    char delim[] =",";
    
    char *reqid = strtok(packet, delim);
    char *src = strtok(NULL, delim);
    char *dest = strtok(NULL, delim);
    char *hop = strtok(NULL, delim);
    char *srcSeq = strtok(NULL, delim);
    char *destSeq = strtok(NULL, delim);
    
    int reqIdInt = atoi(reqid);
    int srcInt = atoi(src);
    int destInt = atoi(dest);
    int hopInt = atoi(hop);
    int srcSeqInt = atoi(srcSeq);
    int destSeqInt = atoi(destSeq);
    
    //TODO - Write to routing table
           
    if(linkaddr_node_addr.u8[0]==destInt){
    	printf("Destination Found\n");
    	
    	if(destSeqInt > own_sequence){
    		own_sequence = destSeqInt;
    	}
        printf("Sending RREP Packet Now\n");
    	sendRREPPacket(reqIdInt,srcInt,destInt,1,srcSeqInt,own_sequence);
    } else {
    	if(!alreadySentRREQ(reqIdInt,srcInt, destInt)){
    	
    		printf("RREQ received from %d :[ reqId: %d, srcId: %d,destId: %d,hopCount: %d, srcSeq: %d,destSeq: %d]\n", from->u8[0], reqIdInt, srcInt, destInt, hopInt, srcSeqInt, destSeqInt);
			if(routingTable[destInt-1].next && routingTable[destInt-1].next != 0 && routingTable[destInt-1].seq >= destSeqInt){ //check if we have a route to the destination which is at least as fresh (sequence number) as requested
        		printf("I can answer this RREQ, even though I'm not the destination.\n");
    			if(isRrepWithShorterDistance(reqIdInt,srcInt,destInt,routingTable[destInt-1].dist+1)){
    				sendRREPPacket(reqIdInt,srcInt,destInt,routingTable[destInt-1].dist+1,srcSeqInt,routingTable[destInt-1].seq);
    			}
        	} else{//If we do not know the path
        		sendRREQPacket(reqIdInt,srcInt,destInt,hopInt+1,srcSeqInt,destSeqInt);
        	}
    	}
    }
}

//Function to handle RREP Received Packet
static void receiveRREP(struct unicast_conn *c, const linkaddr_t *from) {
	//the paylaod is the same as for the RREQ
	static char packet[18];
    strncpy(packet, (char *)packetbuf_dataptr(), 18);

    char delim[] =",";
    
    char *reqid = strtok(packet, delim);
    char *src = strtok(NULL, delim);
    char *dest = strtok(NULL, delim);
    char *hop = strtok(NULL, delim);
    char *srcSeq = strtok(NULL, delim);
    char *destSeq = strtok(NULL, delim);
    
    int reqIdInt = atoi(reqid);
    int srcInt = atoi(src);
    int destInt = atoi(dest);
    int hopInt = atoi(hop);
    int srcSeqInt = atoi(srcSeq);
    int destSeqInt = atoi(destSeq);
    printf("RREP received from %d :[ reqId: %d, srcId: %d,destId: %d,hopCount: %d, srcSeq: %d,destSeq: %d]\n", from->u8[0], reqIdInt, srcInt, destInt, hopInt, srcSeqInt, destSeqInt);
           
    if(!rrepNextHops[from->u8[0]-1].dest || rrepNextHops[from->u8[0]-1].dest == 0){ //Add the sender of the rrep to the monitoring list -> we want to know when it's not available anymore
    	rrepNextHops[from->u8[0]-1].dest = from->u8[0];
    	rrepNextHops[from->u8[0]-1].valid = 1; //is valid at the start.. will be reset later
    }	
    
 //TODO - Write to routing table
           
    if(linkaddr_node_addr.u8[0]==srcInt){
    	//we reached our origin source
		//check that we haven't received a RREP previously. But also, if we receive a second RREP with a smaller hopcount we call this to show the new routing table
    	if(isRrepWithShorterDistance(reqIdInt,srcInt,destInt,hopInt)){
    		printf("I am the original sender and found the route!\n");
    		printf("The routing table will be updated!\n");
    	}
    	//else we received a rrep which isnt smaller or already processed
    } else {
    	//send the rrep to the next node. The source of the original message is our destination for the reply
		//check that we haven't received a RREP previously which was forwarded. But also, if we receive a second RREP with a smaller hopcount we will forward this message
    	if(isRrepWithShorterDistance(reqIdInt,srcInt,destInt,hopInt+1)){
    		sendRREPPacket(reqIdInt,srcInt,destInt,hopInt+1,srcSeqInt,destSeqInt);
    	}
    }
}

// Callback Funtions
static const struct broadcast_callbacks rreq_cb = {receiveRREQ};
static const struct unicast_callbacks rrep_cb = {receiveRREP};

PROCESS_THREAD(open_broadcast_unicast_connection, ev, data) {

	//when the process is killed, close all open connections
    PROCESS_EXITHANDLER(
    	unicast_close(&rrep_conn);
    	unicast_close(&data_conn);
    	broadcast_close(&broadcast);
    	broadcast_close(&hello_broad);
    	broadcast_close(&rerr_broad);
    );
    
	PROCESS_BEGIN();
	
    broadcast_open(&broadcast, RREQ_CH, &rreq_cb);
    unicast_open(&rrep_conn, RREP_CH, &rrep_cb);
    
	PROCESS_END();
}

PROCESS_THREAD(begin_discovery_on_button_click, ev, data) {
    PROCESS_BEGIN();
    
    SENSORS_ACTIVATE(button_sensor);
    int randomDestinationAddress;
     
    while(1){
		PROCESS_WAIT_EVENT_UNTIL(ev == sensors_event && data == &button_sensor);
		
        //Set RIME address of destination node randomly
        linkaddr_t addr;
		randomDestinationAddress = 0;
		while(randomDestinationAddress == 0 || linkaddr_cmp(&addr, &linkaddr_node_addr)){ //if we have not generated any address yet, or our own address -> do it again
			randomDestinationAddress = ( random_rand() % MOTES_COUNT ) + 1;
        	addr.u8[0] = randomDestinationAddress;
        	addr.u8[1] = 0;
		}

		printf("Route Discovery Begins for %d\n", randomDestinationAddress);
        
		if(routingTable[randomDestinationAddress-1].next && routingTable[randomDestinationAddress-1].next!=0){ //Check if we know the route already
			printf("Route already exists in the routing table \n");
        
		//TODO - Send Data here
		} else {
			printf("No route found! Storing the data and sending RREQ Packet \n");
            
            //TODO - Store data temporarily here
			
			own_sequence++; //before we send a new RREQ, we always have to increment our source sequence. All receving nodes can update their routing table with that information.
			
			//get the last known sequence of the destination. Important if we set this node to invalid before and now need to request a fresh route (with a higher sequence). Will be 0 initially, if we never had a valid route
			int lastDestSeq = routingTable[randomDestinationAddress-1].seq; 
			
			//we are starting with hop=1 (the distance will be 1, when the message is received)
            printf("Sending RREQ Packet\n");
			sendRREQPacket(current_req_id,linkaddr_node_addr.u8[0],randomDestinationAddress,1,own_sequence,lastDestSeq);
			
			//the request id is used to uniquely identify the rreq (together with the src). Used by the other nodes to discard duplicates. Increment after each RREQ
			current_req_id++;
		}
	}
	
    PROCESS_END();
}

// Function to send RREQ Packet
static void sendRREQPacket(int reqid, int src,int dest,int hopcount, int srcSeq, int destSeq){
    
    char packet[18] = "";
	sprintf(packet, "%.2d,%.2d,%.2d,%.2d,%.2d,%.2d", reqid, src, dest,hopcount,srcSeq, destSeq);
    
	printf("RREQ sent from %.2d [ reqId: %d, srcId: %d,destId: %d,hopCount: %d, srcSeq: %d,destSeq: %d]\n", linkaddr_node_addr.u8[0],reqid,src,dest,hopcount,srcSeq,destSeq);
	
	writeRREQTable(reqid, src,dest); //put this rreq in the table, so if we receive broadcasted packet from the neighbor nodes it can be discarded
	
    packetbuf_copyfrom(packet, 18);
    
    broadcast_send(&broadcast);
}

//Function to Send RREP Packet
static void sendRREPPacket(int reqid, int src,int dest,int hopcount, int srcSeq, int destSeq){
    
	char packet[18] = "";
	sprintf(packet, "%.2d,%.2d,%.2d,%.2d,%.2d,%.2d", reqid, src, dest,hopcount,srcSeq, destSeq); 
	
    packetbuf_copyfrom(packet, 18); //Copy data to the packet buffer

	int next_hop = getNextHop(src);
    linkaddr_t addr;
    addr.u8[0] = next_hop;
    addr.u8[1] = 0;
    
	//we want to monitor all neighbor nodes where we send/receive a rrep from/to. Since they might be depending on our route
    if(!rrepNextHops[next_hop-1].dest || rrepNextHops[next_hop-1].dest == 0){
    	//initialize the entry in our monitoring list
    	rrepNextHops[next_hop-1].dest = next_hop;
    	rrepNextHops[next_hop-1].valid = 1; //is valid at the start.. will be reset later
    }
	
    printf("RREP sent from %d to %d: [reqId: %d, srcId: %d,destId: %d,hopCount: %d, srcSeq: %d,destSeq: %d]\n", linkaddr_node_addr.u8[0],next_hop,reqid,src,dest,hopcount,srcSeq,destSeq);
    
    /* Send unicast packet */
    unicast_send(&rrep_conn, &addr);
}

static void writeRREQTable(int reqId, int src, int dest){
	//store the sent RREQ to discard future duplicates
	int i=0;
	for(i=0; i<TABLE_SIZE; i++){
		if(!rreqPendingTable[i].reqid){
			rreqPendingTable[i].reqid = reqId;
			rreqPendingTable[i].src = src;
			rreqPendingTable[i].dest = dest;
			rreqPendingTable[i].minHopsRrep = 0; //stored to save the lowest number of hops sent as RREP (to discard RREP with higher hops)
			return;
		}
	}
}

static char alreadySentRREQ(int reqId, int src, int dest){//check if we already sent this RREQ and can discard it
	int i = 0;
	for(i = 0; i < TABLE_SIZE; i++){
		if(rreqPendingTable[i].reqid == reqId &&
		   rreqPendingTable[i].src == src &&
		   rreqPendingTable[i].dest == dest){
			//printf("This RREQ was already sent\n");
			return 1;
		}
	}
	return 0;
}

static char isRrepWithShorterDistance(int reqId, int src, int dest, int hops){
	/*we don't want to forward multiple RREP, if they don't provide a better route therefore, only new rrep's or one with a smaller hopcount are forwarded
	we store the shortest route (through the number of hops) in the corresponding entry of the RREQ Table. */
	int i = 0;
	for(i = 0; i < TABLE_SIZE; i++){
		//find the RREQ for our RREP
		if(rreqPendingTable[i].reqid == reqId &&
		   rreqPendingTable[i].src == src &&
		   rreqPendingTable[i].dest == dest){
		   if(rreqPendingTable[i].minHopsRrep==0 || hops < rreqPendingTable[i].minHopsRrep){
		   		//no rrep sent yet or smaller distance
		   		printf("We never sent a RREP with a smaller distance! So send this...\n");
		   		rreqPendingTable[i].minHopsRrep = hops;
		   		return 1;
		   }
		   printf("We already sent a RREP with a smaller distance! Discard this..\n");
		   //we already sent a rrep with a smaller distance to the dest
		   return 0;
		}
	}
	//if we never sent this rreq (because we are the destination of the RREQ), we never sent a smaller route back! Write a new entry
	int j=0;
	for(j=0; j<TABLE_SIZE; j++){
		if(!rreqPendingTable[j].reqid){
			rreqPendingTable[j].reqid = reqId;
			rreqPendingTable[j].src = src;
			rreqPendingTable[j].dest = dest;
			rreqPendingTable[j].minHopsRrep = hops; //stored to save the lowest number of hops sent as RREP (to discard RREP with higher hops)
			return 1;
		}
	}
	return 1;
}

static int getNextHop(int dest){ //return the next hop to the destination
	if(routingTable[dest-1].next && routingTable[dest-1].next!=0){
		return routingTable[dest-1].next;
	}
	return 0;
}

