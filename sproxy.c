#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/select.h> 
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h> 
#include <unistd.h>

#define true 1

//prototypes
char* setPacket(int type, char* payload, int len, int seq);
int getPacketType(char* packet);
int getPacketSeq(char* packet);
int getPacketLen(char* packet);
char* getPacketMsg(char* packet);


char clientbuf[1024], tdbuf[1024];
char packetbuf[2048];
char overflowbuf[100][2048];
int of_index = 0;
int cpSock, tdSock, cpSockTemp;
int result, n, debug, seqNum = 0;
struct sockaddr_in cpAddr, tdAddr, tempAddr;
socklen_t tempAddr_len;
fd_set readfds;

/*
 * Joshua Riccio
 * sproxy.c
 * CSC 425 
 * Project Milestone 3
 */
int main(int argc, char * argv[]) {
    //Check argument count for correct count
    if (argc < 2) {
        printf("usage: sproxy [sport]\n");
        exit(EXIT_FAILURE);
    }
    int sport = atoi(argv[1]);

    if (argc == 3) {
        debug = 1;
    } else {
        debug = 0;
    }

    //Initialize client proxy socket addr
    cpAddr.sin_family = AF_INET;
    cpAddr.sin_port = htons(sport);
    cpAddr.sin_addr.s_addr = INADDR_ANY;
    bzero(&cpAddr.sin_zero, sizeof (cpAddr));

    //Initialize telnet daemon socket addr
    tdAddr.sin_family = AF_INET;
    tdAddr.sin_port = htons(23);
    tdAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    bzero(&tdAddr.sin_zero, sizeof (tdAddr));

    //Build client proxy socket
    if (debug)
        printf("Debug: Building client proxy, and daemon sockets\n");
    if ((cpSock = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        fprintf(stderr, "Unable to create client socket\n");
        exit(-1);
    }

    //Build telnet daemon socket
    if ((tdSock = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        fprintf(stderr, "Unable to create telnet daemon socket\n");
        exit(-1);
    }

    //Bind the sockets
    if (debug)
        printf("Debug: Binding cproxy socket to port %d\n", sport);
    if ((result = bind(cpSock, (struct sockaddr *) &cpAddr, sizeof (struct sockaddr_in))) == -1) {
        fprintf(stderr, "Unable to bind the cpSock\n");
        exit(-1);
    }

    //Listening to c proxy port for connection
    if (debug)
        printf("Debug: Listening to c proxy port for connection\n");
    if ((result = listen(cpSock, 0)) == -1) {
        fprintf(stderr, "Unable to listen on cpSock\n");
        exit(-1);
    }

    tempAddr_len = sizeof (tempAddr);
    int cpRecv = 0;
    int tdRecv = 0;
    //Wait for input from client proxy or telnet daemon
    if (debug)
        printf("Debug: Waiting for input from client proxy\n");
    if ((cpSockTemp = accept(cpSock, (struct sockaddr *) &tempAddr, &tempAddr_len)) == -1) {
        fprintf(stderr, "Unable to accept\n");
        return 0;
    }
    //Connecting to telnet daemon
    if (debug)
        printf("Debug: Connecting to telnet daemon\n");
    if ((result = connect(tdSock, (struct sockaddr *) &tdAddr, sizeof (tdAddr))) == -1) {
        fprintf(stderr, "Unable to connect to telnet daemon\n");
        exit(-1);
    }

    //Setting file descriptors
    if (debug)
        printf("Debug: Setting the file desciptors\n");
    FD_ZERO(&readfds);
    FD_SET(cpSockTemp, &readfds);
    FD_SET(tdSock, &readfds);

    //Set max socket;
    n = tdSock + 1;
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    int hbcount = 0;

    if (debug)
        printf("Debug: Executing select loop\n");
    while ((result = select(n, &readfds, NULL, NULL, &tv)) >= 0) {
        if (result == -1) {
            fprintf(stderr, "Select failed to execute\n");
            exit(-1);
        }
        if (result == 0) {
            //Timeout occured
            if (debug)
                printf("Debug: Timeout, hbcount=%d\n", hbcount);
            hbcount++;
            tv.tv_sec = 1;
            setPacket(1, "hb", 2, hbcount);
            send(cpSockTemp, packetbuf, 14, 0);
            if (hbcount == 3) {
                hbcount = 0;
                if (debug)
                    printf("Debug: Waiting to accpet cproxy connection\n");
                if ((cpSockTemp = accept(cpSock, (struct sockaddr *) &tempAddr, &tempAddr_len)) == -1) {
                    fprintf(stderr, "Unable to accept\n");
                    return 0;
                }
                FD_SET(cpSockTemp, &readfds);
                if (debug)
                    printf("Debug: Connected!\n");
                //Send backlog buffer
                if (debug)
                    printf("Debug: %d backlog msg to send\n", of_index);
                for (int i = 0; i < 100 || i < of_index; i++) {
                    if (debug)
                        printf("Debug: sending to client: %s\n", overflowbuf[i]);
                    send(cpSockTemp, overflowbuf[i], getPacketLen(overflowbuf[i]) + 12, 0);
                }
            }

        }
        memset(clientbuf, 0, sizeof (clientbuf));
        memset(tdbuf, 0, sizeof (tdbuf));

        //Check which fd is ready, then recieve on that socket
        if (debug)
            printf("Debug: Checking if client has sent\n");
        if (FD_ISSET(cpSockTemp, &readfds)) {
            cpRecv = recv(cpSockTemp, clientbuf, sizeof (clientbuf), 0);
            if (cpRecv < 1) {
                printf("Connection closed\n");
                break;

            }
            if (debug)
                printf("Debug: Recieved from client proxy\n");
        }
        if (debug)
            printf("Debug: Checking if daemon has sent\n");
        if (FD_ISSET(tdSock, &readfds)) {
            tdRecv = recv(tdSock, tdbuf, sizeof (tdbuf), 0);
            if (debug)
                printf("Debug: Received from daemon\n");
        }
        //Forward the response to the next (clientproxy or telnet daemon)
        if (tdRecv > 0) {
            if (debug)
                printf("Debug: Sending %d bytes to client proxy: %s\n", tdRecv, tdbuf);
            setPacket(2, tdbuf, tdRecv, seqNum);
            seqNum++;
            memcpy(overflowbuf[of_index], packetbuf, tdRecv + 12);
            of_index++;
            int result = send(cpSockTemp, packetbuf, tdRecv + 12, 0);
            if (debug)
                printf("Debug: Result from send to client: %d\n", result);
            if (result == -1)
                break;
            tdRecv = 0;
        } else if (tdRecv < 0) {
            printf("Connection closed\n");
            return 0;
        }

        if (cpRecv > 0) {
            if (debug)
                printf("Debug: Sending %d bytes to daemon socket: %s\n", cpRecv, clientbuf);
            if (getPacketType(clientbuf) == 2) {
                int result = send(tdSock, getPacketMsg(clientbuf), cpRecv - 12, 0);
                if (result == -1)
                    break;
                cpRecv = 0;
            } else if (getPacketType(clientbuf) == 1) {
                if (debug)
                    printf("Debug: Heartbeat msg received, hbcount reset to 0\n");
                hbcount = 0;
                of_index = 0;
                memset(overflowbuf, 0, 2048 * 100);
            }
        } else if (cpRecv < 0) {
            printf("Connection closed\n");
            break;
        }

        if (debug)
            printf("Debug: Wating for next message\n");

        //Zero and set file descriptors
        if (debug)
            printf("Debug: Setting the file desciptors\n");
        FD_ZERO(&readfds);
        FD_SET(cpSockTemp, &readfds);
        FD_SET(tdSock, &readfds);
        //Determine max socket for select
        n = cpSockTemp + 1;
        if (n <= tdSock + 1)
            n = tdSock + 1;
    }
    if (debug)
        printf("Debug: Closing the sockets\n");
    close(cpSock);
    close(tdSock);
    return 0;
}

char* setPacket(int type, char* payload, int len, int seq) {
    memset(packetbuf, 0, 2048);
    char *p = packetbuf;
    *((int*) p) = type;
    p = p + 4;
    *((int*) p) = seq;
    p = p + 4;
    *((int*) p) = len;
    p = p + 4;
    memcpy(p, payload, len);
    return packetbuf;
}

int getPacketType(char* packet) {
    return *((int*) packet);
}

int getPacketSeq(char* packet) {
    return *((int*) (packet + 4));
}

int getPacketLen(char* packet) {
    return *((int*) (packet + 8));
}

char* getPacketMsg(char* packet) {
    return packet + 12;
}
