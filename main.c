/*---------------------------------------------------------------------------*/
/*----------------------------------#INCLUDE---------------------------------*/
/*---------------------------------------------------------------------------*/

#include "contiki.h"
#include "net/rime.h"
#include "random.h"

#include "aodv.c" //custom c file

#include "dev/leds.h"
#include "dev/button-sensor.h"

#include <stdio.h>
#include <string.h>

/*---------------------------------------------------------------------------*/
/*----------------------------------#DEFINE----------------------------------*/
/*---------------------------------------------------------------------------*/

#define TOT_NODES 8 //max 99 --- MODIFIABLE
#define INFINITE_HOPS 99 //max 99 --- MODIFIABLE

#define IN_BETWEEN_SECONDS_DATA_SEND 30// --- MODIFIABLE
#define WAITING_SECONDS_AFTER_ROUTE_REQ 1// --- MODIFIABLE

#define EXPIRATION_SECONDS_ROUTING_TABLE_ROW 90 // --- MODIFIABLE(-1 == NO exp)

#define UNICAST_ROUTE_REPLY_CHANNEL 11// --- MODIFIABLE
#define UNICAST_DATA_CHANNEL 20// --- MODIFIABLE
#define BROADCAST_CHANNEL 26// --- MODIFIABLE


/*---------------------------------------------------------------------------*/
/*-------------------------PROCESSES DECLARATIONS----------------------------*/
/*---------------------------------------------------------------------------*/

PROCESS(rime_listener, "Open all connections");
PROCESS(route_req_handler, "Handle ROUTE_REQ messages");
PROCESS(data_sender, "Creates and sends new DATA");
PROCESS(rotuing_table_exp_controller, "Controls the expiration of the r. table");
PROCESS(debugger_handler, "Enables/disables the debugger if button is clicked");

AUTOSTART_PROCESSES(&rime_listener,
                    &route_req_handler, 
		    &data_sender,
                    &rotuing_table_exp_controller,
		    &debugger_handler);


/*---------------------------------------------------------------------------*/
/*--------------------------FUNCTION PROTOTYPES------------------------------*/
/*---------------------------------------------------------------------------*/


//RREPLY
static void sendRouteReply(struct ROUTE_REPLY, int);
static int getRouteReplySender(struct ROUTE_REPLY);

//DATA
static void sendData(struct DATA, int);
static void getRandomDataPayload(char []);

//ROUTE_REQ
static void broadcastRouteReq(struct ROUTE_REQ);


//BOTH TABLES
static char updateTablesIfBetterRouteReply(struct ROUTE_REPLY, int);

//ROUTING TABLE
static int getNextHop(int);
static void addRowToRoutingTable(int);
static void setActiveRoutingTable(int);
static void printRoutingTable();

//ROUTING DISCOVERY TABLE
static void addRowToDiscoveryTable(struct ROUTE_REQ_INFO);
static char isRouteReqInDiscoveryTable(struct ROUTE_REQ);
static void printRoutingDiscoveryTable();


//UNICAST CALLBACKS
static void route_reply_callback(struct unicast_conn *, const rimeaddr_t *);
static void data_callback(struct unicast_conn *, const rimeaddr_t *);

//BROADCAST CALLBACKS
static void route_req_callback(struct broadcast_conn *, const rimeaddr_t *);



/*---------------------------------------------------------------------------*/
/*-----------------------------GLOBAL VARIABLES------------------------------*/
/*---------------------------------------------------------------------------*/

//DEBUGGER - SEE debugger_handler PROCESS
static char dbg = 0;

//UNICAST CALLBACKS
static const struct unicast_callbacks unicast_call_rreply = 
{route_reply_callback};
static const struct unicast_callbacks unicast_call_data = 
{data_callback};

static struct unicast_conn unicast_connection_route_reply;
static struct unicast_conn unicast_connection_data;


//BROADCAST CALLBACK
static const struct broadcast_callbacks broadcast_call_rreq = 
{route_req_callback};
static struct broadcast_conn broadcast_connection_route_req;


//ROUTING TABLES
static struct ROUTING_TABLE_ROW routingTable[TOT_NODES];
static struct ROUTING_DISCOVERY_TABLE_ROW routingDiscoveryTable[TOT_NODES];



/*---------------------------------------------------------------------------*/
/*--------------------------PROCESSES DEFINITION-----------------------------*/
/*---------------------------------------------------------------------------*/

//This process activates the connections (broadcast and unicast)
PROCESS_THREAD(rime_listener, ev, data)
{
   
	PROCESS_EXITHANDLER({
                unicast_close(&unicast_connection_route_reply);
                unicast_close(&unicast_connection_data);
                broadcast_close(&broadcast_connection_route_req);
        }); 
        
	PROCESS_BEGIN();

	unicast_open(&unicast_connection_route_reply, 
		UNICAST_ROUTE_REPLY_CHANNEL, 
		&unicast_call_rreply);
	
	printf("Now listening to ROUTE_REPLY messages on channel: %d\n", 
		UNICAST_ROUTE_REPLY_CHANNEL);

        unicast_open(&unicast_connection_data, 
		UNICAST_DATA_CHANNEL, 
		&unicast_call_data);
	
	printf("Now listening to DATA messages on channel: %d\n",
                UNICAST_DATA_CHANNEL);

        broadcast_open(&broadcast_connection_route_req, 
                BROADCAST_CHANNEL, 
                &broadcast_call_rreq);
	
	printf("Now listening to ROUTE_REQ  messages on channel: %d\n", 
                BROADCAST_CHANNEL);

	PROCESS_END();
}

//This process helps to perform outgoing ROUTE_REQ
PROCESS_THREAD(route_req_handler, ev, data)
{
        
        static struct etimer et;

        static struct ROUTE_REQ_INFO rreq_info;
        static struct ROUTE_REQ rreq;

	PROCESS_BEGIN();
        
        while(1){
                
                leds_off(LEDS_YELLOW); 
                
		//the process waits for a post request (it can come either from
		//the data_sender or from the route_req callback
                PROCESS_WAIT_EVENT_UNTIL(ev != sensors_event);
                
                leds_on(LEDS_YELLOW);
                
                rreq_info.REQ_ID = (*(struct ROUTE_REQ_INFO*) data).REQ_ID;
                rreq_info.SRC = (*(struct ROUTE_REQ_INFO*) data).SRC;
                rreq_info.DEST = (*(struct ROUTE_REQ_INFO*) data).DEST;
                rreq_info.SND = (*(struct ROUTE_REQ_INFO*) data).SND;
                
                rreq.REQ_ID = rreq_info.REQ_ID;
                rreq.SRC = rreq_info.SRC;
                rreq.DEST = rreq_info.DEST;
                             
                //create entry in routing discovery table
                addRowToRoutingTable(rreq.DEST);
                
                //create entry (ACTIVE=0) in routing table 
                addRowToDiscoveryTable(rreq_info);
                
		//broadcasting the ROUTE_REQUEST
                broadcastRouteReq(rreq);
                
                etimer_set(&et, 
                        CLOCK_CONF_SECOND * WAITING_SECONDS_AFTER_ROUTE_REQ);
                
		
               	PROCESS_WAIT_EVENT_UNTIL(ev == PROCESS_EVENT_TIMER);
                
                //set entry in routing table: ACTIVE = 1
                setActiveRoutingTable(rreq.DEST);

		if(dbg) printRoutingTable();
                
                
        }

	PROCESS_END();
}


//This process is responsible of the periodic sending of random data
PROCESS_THREAD(data_sender, ev, data){
	
	static struct etimer et;
        static struct etimer et_2;
        

	static int req_id = 1; //starting req_id
        static int initial_delay;

	static int dest;
	static int next;

        
        static struct ROUTE_REQ_INFO rreq_info;
        static struct DATA data_msg;
        
        
        PROCESS_BEGIN();
        
	//this variable helps to reduce (randomly) the conflicts at the start
        initial_delay = random_rand() % IN_BETWEEN_SECONDS_DATA_SEND;

	while(1) {
	
                leds_off(LEDS_GREEN);

		if(initial_delay >= 0){
			etimer_set(&et, (CLOCK_CONF_SECOND) * initial_delay );
			initial_delay = -1;
		}else{
			etimer_set(&et, 
                        	(CLOCK_CONF_SECOND) * 
				 IN_BETWEEN_SECONDS_DATA_SEND);
		}
                
                PROCESS_WAIT_EVENT_UNTIL(ev != sensors_event);

                leds_on(LEDS_GREEN);
                
		//IF the event is the data_sender TIMER, i.e. if DATA has to be
		//generated by THIS node
                if(ev == PROCESS_EVENT_TIMER){ 

                        if(dbg) printf("Process that sends DATA is awake!\n");

                        //get a random destination node, but not this one
                        dest = 1 + random_rand() % TOT_NODES;
                        if(dest==rimeaddr_node_addr.u8[0]){ //not same node
                                dest = (dest!=TOT_NODES)? dest+1 : 1;//last node
                        }

                        
                        
                        getRandomDataPayload(data_msg.PAYLOAD);
                        
                        printf("+ + + + + Sending NEW DATA message to %d: {%s}\n",
                                dest, data_msg.PAYLOAD);
                        
		//IF the event is generated by the data message CALLBACK, 
		//i.e. if DATA is coming from ANOTHER node BUT is not directed
		//to this one (has to be re-sent)
                }else{
                        
                        dest = (*(struct DATA*) data).DEST;
                        strcpy(data_msg.PAYLOAD,(*(struct DATA*) data).PAYLOAD);

			printf(" -> -> -> RE-sending a DATA message to %d: {%s}\n",
				dest, data_msg.PAYLOAD);

                }

		//gets the NEXT hop to destination "dest"
                next = getNextHop(dest);
                
                //if no route exists to "dest"    
                if(next==0){

			//configuring parameter for the ROUTE_REQ...
                        req_id = (req_id<100) ? req_id : 1;
                        rreq_info.REQ_ID= req_id;
                        rreq_info.SRC = rimeaddr_node_addr.u8[0]; //this node
                        rreq_info.DEST = dest;
                        rreq_info.SND = rimeaddr_node_addr.u8[0]; //this node
                        
			//calls the route_req_handler PROCESS in order to
			//broadcast the generated ROUTE_REQ
                        process_post(&route_req_handler, 
                                PROCESS_EVENT_CONTINUE,
                                &rreq_info);
                        
                        req_id++;
                        

			//waits the same interval of time that the route_req 
			//handler
                        etimer_set(&et_2, CLOCK_CONF_SECOND*
					WAITING_SECONDS_AFTER_ROUTE_REQ);
                        PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et_2));
                        
			//tries to get the next hop, hoping that the routing
			//table now contains it
                        next = getNextHop(dest);
                        
                }
                
                
                //if there is next hop
                if(next!=0){
                
                        if(dbg) printf("Now sending! DEST:%d, NEXT:%d \n",
                                	dest, next);

                        data_msg.DEST = dest;

                        //SEND DATA NOW
                        sendData(data_msg, next);
                
		//if there is no route to the wanted destination
                }else{
                        
                        printf("- - - - - NO ROUTE to %d\n",dest);
                        
                }
                
                
                
                
 	}

	PROCESS_END();

}


//This process periodically checks the routing table to delete expired rows
PROCESS_THREAD(rotuing_table_exp_controller, ev, data){
        
        static struct etimer et;
        static int i;
        
        PROCESS_BEGIN();
        
        leds_off(LEDS_RED);
        
        while(1){
                
                etimer_set(&et, CLOCK_CONF_SECOND);
                
                PROCESS_WAIT_EVENT_UNTIL(ev != sensors_event);
                
                leds_off(LEDS_RED);
                
                for(i=0; i<TOT_NODES; i++){
                        if(routingTable[i].EXP > 0 && 
                                routingTable[i].ACTIVE ==1){
                                routingTable[i].EXP --;
                                if(routingTable[i].EXP == 0){
                                        routingTable[i].ACTIVE=0;
                                        routingTable[i].NEXT=0;  
                                        if(dbg) printf("NEXT-hop to %d expired!\n",
                                                i+1);
                                        if(dbg) printRoutingTable();
                                        leds_on(LEDS_RED);
                                }
                        }
                        
                        
                }
                
                
        }
        
        
        
        
        PROCESS_END();
        
}

//This process enables/disables the debugger, catching the event generated
//by the clicking of the BUTTON
PROCESS_THREAD(debugger_handler, ev, data){
	
	PROCESS_BEGIN();

	SENSORS_ACTIVATE(button_sensor);
  	
  	while(1) {
    		PROCESS_WAIT_EVENT_UNTIL(ev == sensors_event &&
			     data == &button_sensor);
    		if(dbg==0)
			dbg=1;
		else
			dbg=0;
  	}

	PROCESS_END();

}


/*---------------------------------------------------------------------------*/
/*-----------------------------FUNCTIONS DEFINITION--------------------------*/
/*---------------------------------------------------------------------------*/


/*-----------------------------connection callbacks--------------------------*/

//This function gets called whenever a packet is received in the unicast
//connection on the channel reserved to "ROUTE_REPLY" messages
static void route_reply_callback(struct unicast_conn *c, 
                                const rimeaddr_t *from){
	
        static char rreply_packet[ROUTE_REPLY_PACKET_LEN];
        static struct ROUTE_REPLY rreply;
        
        static int sender;
        
        strncpy(rreply_packet, (char *)packetbuf_dataptr(), 
                ROUTE_REPLY_PACKET_LEN);
        
	//if it is a ROUTE_REPLY packet
        if(packetToRouteReply(rreply_packet, &rreply)!=0){
                
                if(dbg) printf("ROUTE_REPLY received from %d, content:{%s}\n",
                                from->u8[0], 
                                rreply_packet);

        
		//updates the tables if ROUTE_REPLY is significant,
		//it returns 0 if no update is done
                if(updateTablesIfBetterRouteReply(rreply, from->u8[0])){
                        
			//retrieve the sender of the rreply
                        sender = getRouteReplySender(rreply);
                        
			//if the sender is this node
                        if(sender != rimeaddr_node_addr.u8[0]){
                                
				//it adds 1 hop and resends the rreply
                                rreply.HOPS = rreply.HOPS + 1;
                                sendRouteReply(rreply, sender);
                        }
                        
                }else{
                        
                        if(dbg)  printf("Dropping ROUTE_REPLY!\n");
                        
                }
                
                
        }else{
		printf("ERROR in the unicast ROUTE_REPLY CALLBACK!\n");
	}



}



//This function gets called whenever a packet is received in the unicast
//connection on the channel reserved to "DATA" messages
static void data_callback(struct unicast_conn *c, const rimeaddr_t *from){
	
        static struct DATA data_msg;
        
        static char data_packet[DATA_PACKET_LEN];
        
        strncpy(data_packet, (char *)packetbuf_dataptr(), DATA_PACKET_LEN);
        
	//if the messae received is actually a DATA message
        if(packetToData(data_packet, &data_msg) != 0){
		
		//if the destination of the message is this node
                if(data_msg.DEST == rimeaddr_node_addr.u8[0]){
                        printf("! ! ! ! ! DATA message RECEIVED: {%s}\n",
				data_msg.PAYLOAD );
                        
		//otherwise
                }else{
                        if(dbg) printf("This DATA is not for this node!\n");

			//wakes up the event that handles the sending of DATA
                        process_post(&data_sender, 
                                PROCESS_EVENT_CONTINUE,
                                &data_msg);
                }
        }else{
                printf("ERROR in the DATA CALLBACK!\n");
        }

}


//This function gets called whenever a packet is received in the broadcast
//connection on the channel reserved to "ROUTE_REQ" messages
static void route_req_callback(struct broadcast_conn *c, 
                                const rimeaddr_t *from){

        static struct ROUTE_REQ_INFO rreq_info;
       	static struct ROUTE_REQ rreq;
        
        static char rreq_packet[ROUTE_REQ_PACKET_LEN];
        
        static struct ROUTE_REPLY rreply;
        
        strncpy(rreq_packet, (char *)packetbuf_dataptr(), ROUTE_REQ_PACKET_LEN);

        //if the packet is a ROUTE_REQ
	if(packetToRouteReq(rreq_packet , &rreq) != 0){
                
                if(dbg) printf("ROUTE_REQ received from %d, content:{%s}\n",
                        from->u8[0], 
                        rreq_packet);
                
		//if THIS is the destination
		if(rreq.DEST == rimeaddr_node_addr.u8[0]){
                        
                        
                        rreply.REQ_ID = rreq.REQ_ID;
                        rreply.SRC = rreq.SRC;
                        rreply.DEST = rreq.DEST;
                        rreply.HOPS = 0;
                        
			//sends a new ROUTE_REPLY to the ROUTE_REQ sender
                        sendRouteReply(rreply, from->u8[0]);
                        
		//if this is NOT the destination AND the ROUTE_REQ is new
		}else if(isRouteReqInDiscoveryTable(rreq)==0){
                        
                        rreq_info.REQ_ID = rreq.REQ_ID;
                        rreq_info.SRC = rreq.SRC;
                        rreq_info.DEST = rreq.DEST;
                        rreq_info.SND = from->u8[0];
                        
			//wakes up the process to perform a ROUTE_REQ
                        process_post(&route_req_handler, 
                                PROCESS_EVENT_CONTINUE,
                                &rreq_info);
                        
                }else{
			
			//if the ROUTE_REQ already passed through this node
                        if(dbg) printf("Dropping ROUTE_REQ\n");
                        
                }
	}else{
		printf("ERROR in the BROADCAST CALLBACK!\n");
	}
}

/*------------------------connection helper functions -----------------------*/

//Actually sends the ROUTE_REPLY message
static void sendRouteReply(struct ROUTE_REPLY rreply, int toNode){
        
        static char packet[ROUTE_REPLY_PACKET_LEN];
        
        static rimeaddr_t to_rimeaddr;
        to_rimeaddr.u8[0]=toNode;
	to_rimeaddr.u8[1]=0;
        
        
        routeReplyToPacket(rreply, packet);
        packetbuf_clear();
        packetbuf_copyfrom(packet, ROUTE_REPLY_PACKET_LEN); 
        unicast_send(&unicast_connection_route_reply, &to_rimeaddr);
        
        
        if(dbg) printf("Unicasting ROUTE_REPLY {%s} to node {%d}\n", 
                 packet, to_rimeaddr.u8[0]);
        
}

//Extracts from the ROUTE_REPLY message the sender of it
static int getRouteReplySender(struct ROUTE_REPLY rreply){
        
        static int i;
        i = rreply.SRC - 1;
        
        return routingDiscoveryTable[i].SND;
        
}

//Actually sends the DATA message
static void sendData(struct DATA data_msg, int next){

	static char packet[DATA_PACKET_LEN];

	static rimeaddr_t next_rimeaddr;
	next_rimeaddr.u8[0]=next;
	next_rimeaddr.u8[1]=0;


	dataToPacket(data_msg, packet);
        
        packetbuf_clear();
	packetbuf_copyfrom(packet, DATA_PACKET_LEN);

        unicast_send(&unicast_connection_data, &next_rimeaddr);

}

//Generates a random DATA message (a message with a random 2 digits number)
static void getRandomDataPayload(char payload[DATA_PAYLOAD_LEN]){
        static int i;
        static int middle;
        static int rand;
        middle = DATA_PAYLOAD_LEN/2 - 1;
        rand = random_rand() %100;
        for(i=0;i<DATA_PAYLOAD_LEN-1;i++){
                if(i==(middle-1) || i==(middle+2)){
                        payload[i] = ' ';
                }else if(i==middle){
                        payload[i] = rand/10 + '0';
		}else if(i==(middle+1)){
			payload[i] = rand%10 + '0';
                }else{
                        payload[i] = '*';
                }
        }
        payload[i] = '\0';
}



//Actually sends the ROUTE_REQ message
static void broadcastRouteReq(struct ROUTE_REQ rreq){
        	
        static char packet[ROUTE_REQ_PACKET_LEN];
        
        routeReqToPacket(rreq, packet);
        packetbuf_clear();
        packetbuf_copyfrom(packet, ROUTE_REQ_PACKET_LEN); 
        broadcast_send(&broadcast_connection_route_req);

        if(dbg) printf("Broadcasting ROUTE_REQ: {%s}\n", packet);
        
}


/*--------------------routing and discovery table functions------------------*/

//return 1 if an update has been made, 0 otherwise
static char updateTablesIfBetterRouteReply(struct ROUTE_REPLY rreply, int from){
        
        static int i;
        static int j;
        
        i = rreply.DEST - 1;
        j = rreply.SRC - 1;
        
	//if the row not active (still modifiable/updatable)
        if(routingTable[i].ACTIVE == 0){
                
                //if the ROUTE_REPLY received shows a better path 
                if(rreply.HOPS < routingDiscoveryTable[j].HOPS){
                        
                        
			//UPDATES the routing discovery table!
                        routingDiscoveryTable[j].HOPS = rreply.HOPS;

			//UPDATES the entry in the routing table
                        routingTable[i].NEXT = from;
			routingTable[i].EXP = EXPIRATION_SECONDS_ROUTING_TABLE_ROW;
			
                        
                        if(dbg) printf("Better ROUTE found for %d: %d HOPS!\n",
                                rreply.DEST, rreply.HOPS);
                        
                        return 1;
                
                }
                
                
        }

        return 0;
}

/*---------------------------routing table functions-------------------------*/

//Simply gets the next hop for the given destination
static int getNextHop(int dest){
	
        return routingTable[dest-1].NEXT;
	
}

//Simply adds a row to the Routing Table for the given destination,
//it also puts it into "ACTIVE=0"
static void addRowToRoutingTable(int dest){
        
        routingTable[dest-1].DEST = dest;
        routingTable[dest-1].ACTIVE = 0;
        
}

static void setActiveRoutingTable(int dest){
        
        routingTable[dest-1].ACTIVE = 1;
        
}

//Helps to print the Routing Table
static void printRoutingTable(){
        static int i;
	static char flag;
	flag = 0;
	printf("*.*.*.*.*\n");
	printf("Routing Table:\n");
        for(i=0; i<TOT_NODES;i++){
                if(routingTable[i].DEST!= 0 && routingTable[i].NEXT!= 0){
                        printf("{DEST:%2d;NEXT:%2d;ACTIVE:%d;EXP:%d}\n",
                        routingTable[i].DEST, routingTable[i].NEXT,
                        routingTable[i].ACTIVE, routingTable[i].EXP );
			flag ++;
		}
        }
	if(flag==0){
		printf("/- empty -/\n");
	}
	printf("*.*.*.*.*\n");
}

/*----------------------routing discovery table functions--------------------*/

//Simply adds a NEW row into the Discovery Table
static void addRowToDiscoveryTable(struct ROUTE_REQ_INFO rreq_info){
        
        static int i;
        i = rreq_info.SRC - 1;
        
        
        routingDiscoveryTable[i].REQ_ID = rreq_info.REQ_ID;
        routingDiscoveryTable[i].SRC = rreq_info.SRC;
        routingDiscoveryTable[i].DEST = rreq_info.DEST;
        
        routingDiscoveryTable[i].SND = rreq_info.SND;
        routingDiscoveryTable[i].HOPS = INFINITE_HOPS;
        
        
      	if(dbg) printf("Row added to Discovery Routing Table: "
              "{REQ_ID:%2d;SRC:%2d;DEST:%2d;SND:%2d;HOPS:%2d}\n", 
              rreq_info.REQ_ID, rreq_info.SRC, rreq_info.DEST, 
              rreq_info.SND, INFINITE_HOPS);

	if(dbg) printRoutingDiscoveryTable();
        
        
}

//Tells the node if the received ROUTE_REQ was already in the Discovery Table
static char isRouteReqInDiscoveryTable(struct ROUTE_REQ rreq){
        
        static int i;
        i= rreq.SRC - 1;
        if(routingDiscoveryTable[i].REQ_ID==rreq.REQ_ID
                && routingDiscoveryTable[i].DEST==rreq.DEST){
                        return 1;
        }
        
        return 0;
        
}

//Helps to print the DiscoveryTable
static void printRoutingDiscoveryTable(){
        static int i;
	printf("*?*?*?*?*\n");
        printf("Routing Discovery Table:\n");
        for(i=0; i<TOT_NODES;i++){
                if(routingDiscoveryTable[i].SRC!= 0)
                        printf("{REQ_ID:%2d;SRC:%2d;DEST:%2d;SND:%2d;HOPS:%2d}"
                                "\n",
                                routingDiscoveryTable[i].REQ_ID,
                                routingDiscoveryTable[i].SRC,
                                routingDiscoveryTable[i].DEST,
                                routingDiscoveryTable[i].SND,
                                routingDiscoveryTable[i].HOPS);
        }
	printf("*?*?*?*?*\n");
}



