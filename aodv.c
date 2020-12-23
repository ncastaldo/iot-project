/*---------------------------------------------------------------------------*/
/*----------------------------------#DEFINE----------------------------------*/
/*---------------------------------------------------------------------------*/


#define ROUTE_REQ_PACKET_LEN 35

#define ROUTE_REPLY_PACKET_LEN 45

#define DATA_PACKET_LEN 22+11 //header + payload
#define DATA_PAYLOAD_LEN 11 //payload


/*---------------------------------------------------------------------------*/
/*----------------------------------STRUCTS----------------------------------*/
/*---------------------------------------------------------------------------*/


//ROUTING TABLES

struct ROUTING_TABLE_ROW{
	int DEST;
	int NEXT;
        int ACTIVE;
	int EXP;
};

struct ROUTING_DISCOVERY_TABLE_ROW{
	int REQ_ID;
	int SRC;
        int DEST;
	int SND; 
        int HOPS;
};


//PACKET TYPES

struct ROUTE_REQ {
	int REQ_ID;
	int SRC;
	int DEST;
};

struct ROUTE_REPLY {
	int REQ_ID;
        int SRC;
	int DEST;
	int HOPS;
};

struct DATA {
	int DEST;
	char PAYLOAD[DATA_PAYLOAD_LEN];
};


//HELPER-STRUCT

struct ROUTE_REQ_INFO {
        int REQ_ID;
	int SRC;
	int DEST;
        int SND;
};

/*---------------------------------------------------------------------------*/

//HELPER METHODS

static void routeReqToPacket(struct ROUTE_REQ rreq, char* packet){
	sprintf(packet, "ROUTE_REQ;");
	sprintf(packet + strlen(packet), "REQ_ID:%2d;", rreq.REQ_ID);
	sprintf(packet + strlen(packet), "SRC:%2d;", rreq.SRC);
	sprintf(packet + strlen(packet), "DEST:%2d;", rreq.DEST);
}

static void routeReplyToPacket(struct ROUTE_REPLY rreply, char* packet){
	sprintf(packet, "ROUTE_REPLY;");
	sprintf(packet + strlen(packet), "REQ_ID:%2d;", rreply.REQ_ID);
        sprintf(packet + strlen(packet), "SRC:%2d;", rreply.SRC);
	sprintf(packet + strlen(packet), "DEST:%2d;", rreply.DEST);
	sprintf(packet + strlen(packet), "HOPS:%2d;", rreply.HOPS);
}

static void dataToPacket(struct DATA data, char* packet){
	sprintf(packet, "DATA;");
	sprintf(packet + strlen(packet), "DEST:%2d;", data.DEST);
	sprintf(packet + strlen(packet), "PAYLOAD:%s", data.PAYLOAD);
}

//ROUTE_REPLY;REQ_ID:xx;SRC:yy;DEST:zz;HOPS:vv;
static char packetToRouteReply(char* packet, struct ROUTE_REPLY * rreply){
        
	static char reqId[2];
        static char src[2];
	static char dest[2];
	static char hops[2];
        if( strncmp(packet,"ROUTE_REPLY;", 12) == 0){
                reqId[0] = packet[19];
                reqId[1] = packet[20];
                src[0] = packet[26];
                src[1] = packet[27];
                dest[0] = packet[34];
                dest[1] = packet[35];
                hops[0] = packet[42];
                hops[1] = packet[43];
                rreply->REQ_ID = atoi(reqId);
                rreply->SRC = atoi(src);
                rreply->DEST = atoi(dest);
                rreply->HOPS = atoi(hops);
                return 1;
        }
	return 0;
}

//DATA;DEST:xx;PAYLOAD:etcetcetc....;
static char packetToData(char* packet, struct DATA * data_msg){
        static char dest[2];
        if( strncmp(packet, "DATA;", 5) == 0){
                //strncpy(dest, packet+10, 2);
                dest[0] = packet[10];
                dest[1] = packet[11];
                data_msg->DEST = atoi(dest);
                strncpy(data_msg->PAYLOAD, packet+21, DATA_PAYLOAD_LEN);
                return 1;
        }return 0;
}

//ROUTE_REQ;REQ_ID:xx;SRC:yy;DEST:zz;
static char packetToRouteReq(char* packet, struct ROUTE_REQ * rreq){
	static char reqId[2];
	static char src[2];
	static char dest[2];
        if( strncmp(packet,"ROUTE_REQ;", 10) == 0){
                reqId[0] = packet[17];
                reqId[0] = packet[18];
                src[0] = packet[24];
                src[0] = packet[25];
                dest[0] = packet[32];
                dest[1] = packet[33];
                rreq->REQ_ID = atoi(reqId);
                rreq->SRC = atoi(src);
                rreq->DEST = atoi(dest);
                return 1;
        }
	return 0;
}


/*---------------------------------------------------------------------------*/
