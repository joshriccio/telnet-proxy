#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h> 
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>       
#include <netdb.h>
#include <unistd.h>
#include <errno.h>

#include <ifaddrs.h>

#define true 1

//prototypes
char* setPacket(int type, char* payload, int len, int seq);
int getPacketType(char* packet);
int getPacketSeq(char* packet);
int getPacketLen(char* packet);
char* getPacketMsg(char* packet);

struct sockaddr_in telnetAddr, spAddr, temp;
fd_set readfds;
char telnetbuf[1024], spbuf[1024];
char packetbuf[2048];
int tnSock, spSock, tnSockTemp;
int result, n, debug = 0;

char ipA[32];
char ipB[32];

/*
 * Joshua Riccio
 * cproxy.c
 * CSC 425 
 * Project Milestone 3
 */

int setIp(char *ip) {
    struct ifaddrs *ifap, *ifa;
    struct sockaddr_in *sa;
    char *addr;
    int result = 0;

    getifaddrs(&ifap);
    for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr->sa_family == AF_INET) {
            sa = (struct sockaddr_in *) ifa->ifa_addr;
            addr = inet_ntoa(sa->sin_addr);
            if (debug)
                printf("Interface: %s\tAddress: %s\n", ifa->ifa_name, addr);
            if (strcmp(ifa->ifa_name, "eth1") == 0) {
                strcpy(ip, addr);
                result = 1;
            }
        }
    }
    freeifaddrs(ifap);
    return result;
}

int main(int argc, char * argv[]) {
    //Check argument count for correct count
    if (argc < 4) {
        printf("usage: cproxy [cport] [sip] [sport]\n");
        exit(EXIT_FAILURE);
    }

    int cport = atoi(argv[1]);
    int sport = atoi(argv[3]);
    char *ip = argv[2];

    if (argc == 5) {
        debug = 1;
    } else {
        debug = 0;
    }

    //Initialize telnet addr sock struct
    telnetAddr.sin_family = AF_INET;
    telnetAddr.sin_port = htons(cport);
    telnetAddr.sin_addr.s_addr = INADDR_ANY;
    bzero(&telnetAddr.sin_zero, sizeof (telnetAddr));

    //Initialize server proxy sock struct
    spAddr.sin_family = AF_INET;
    spAddr.sin_port = htons(sport);
    spAddr.sin_addr.s_addr = inet_addr(ip);
    bzero(&spAddr.sin_zero, sizeof (spAddr));

    //Build sockets
    if (debug)
        printf("Debug: Creating telnet and server proxy sockets\n");
    if ((tnSock = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        fprintf(stderr, "Unable to make telnet socket\n");
        exit(-1);
    }
    //initialize ipA to current IP addr
    setIp(ipA);

    if ((spSock = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        fprintf(stderr, "Unable to make server proxy socket\n");
        exit(-1);
    }

    //Bind telnet socket
    if (debug)
        printf("Debug: Binding telnet socket to port %d\n", sport);
    if ((result = bind(tnSock, (struct sockaddr *) &telnetAddr, sizeof (struct sockaddr_in))) == -1) {
        fprintf(stderr, "Unable to bind telnet socket\n");
        exit(-1);
    }

    //Listening to bound socket for telnet connections
    if (debug)
        printf("Debug: Listening to bound socket for telnet connections\n");
    if ((result = listen(tnSock, 0)) == -1) {
        fprintf(stderr, "Unable to listen to bound socket\n");
        exit(-1);
    }

    socklen_t t_len = sizeof (temp);
    int tnRecv = 0;
    int spRecv = 0;
    while (true) {
        if (debug)
            printf("Debug: Waiting for input from telnet socket\n");
        if ((tnSockTemp = accept(tnSock, (struct sockaddr *) &temp, &t_len)) == -1) {
            fprintf(stderr, "Unable to accept connection\n");
            break;
        }

        //Attempting to connect to server proxy
        if (debug)
            printf("Debug: Connecting to server proxy\n");
        if (connect(spSock, (struct sockaddr *) &spAddr, sizeof (spAddr)) == -1) {
            fprintf(stderr, "Unable to connect to server proxy: %s\n", strerror(errno));
            exit(-1);
        }

        //Zero and set fds
        FD_ZERO(&readfds);
        FD_SET(tnSockTemp, &readfds);
        FD_SET(spSock, &readfds);
        n = spSock + 1;

        if (debug)
            printf("Debug: Executing select loop\n");
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        int hbcount = 0;
        while ((result = select(n, &readfds, NULL, NULL, &tv)) >= 0) {
            if (result == -1) {
                fprintf(stderr, "Unable to execute select\n");
                exit(-1);
            } else {
                if (result == 0) {
                    //Timeout occured
                    if (debug)
                        printf("Debug: Timeout, hbcount=%d\n", hbcount);
                    hbcount++;
                    tv.tv_sec = 1;
                    setPacket(1, "hb", 2, hbcount);
                    send(spSock, packetbuf, 14, 0);
                    if (hbcount == 3) {
                        hbcount = 0;
                        //Close the server proxy socket
                        if (debug)
                            printf("Debug: Closing cproxy/sproxy connection on %s\n", ip);
                        close(spSock);
                        while (setIp(ipB) == 0) {
                        }
                        if ((spSock = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
                            fprintf(stderr, "Unable to make server proxy socket\n");
                            exit(-1);
                        }
                        if (connect(spSock, (struct sockaddr *) &spAddr, sizeof (spAddr)) == -1) {
                            fprintf(stderr, "Unable to connect to server proxy: %s\n", strerror(errno));
                            exit(-1);
                        }
                        FD_CLR(spSock, &readfds);
                        FD_SET(spSock, &readfds);
                        n = spSock + 1;
                        if (debug)
                            printf("Debug: Connected! cproxy/sproxy connection on %s\n", ip);
                    }
                } else {
                    //Zero buffer space
                    if (debug)
                        printf("Debug: Zeroing out buffers\n");
                    memset(telnetbuf, 0, sizeof (telnetbuf));
                    memset(spbuf, 0, sizeof (spbuf));

                    if (debug)
                        printf("Debug: Checking if telnet has sent\n");
                    if (FD_ISSET(tnSockTemp, &readfds)) {
                        if (debug)
                            printf("Debug: Receving telnet message\n");
                        tnRecv = recv(tnSockTemp, telnetbuf, sizeof (telnetbuf), 0);
                        if (tnRecv < 1) {
                            if (debug)
                                printf("Debug: Telnet client discoonnected\n");
                            break;
                        }
                        if (debug)
                            printf("Debug: Recevied message from telnet: %s\n", telnetbuf);
                    }
                    if (debug)
                        printf("Debug: Checking for response from server proxy\n");
                    if (FD_ISSET(spSock, &readfds)) {
                        spRecv = recv(spSock, spbuf, sizeof (spbuf), 0);
                        if (debug)
                            printf("Debug: Received message from server proxy: %s\n", spbuf);
                    }

                    if (tnRecv > 0) {
                        if (debug)
                            printf("Debug: Sending msg to server proxy\n");
                        setPacket(2, telnetbuf, tnRecv, 1);
                        send(spSock, packetbuf, tnRecv + 12, 0);
                        tnRecv = 0;
                    } else if (tnRecv < 0) {
                        if (debug)
                            printf("Debug: Telnet client discoonnected\n");
                        break;
                    }
                    if (spRecv > 0) {
                        if (debug)
                            printf("Debug: Sending message to telnet client\n");
                        if (getPacketType(spbuf) == 2) {
                            send(tnSockTemp, getPacketMsg(spbuf), spRecv - 12, 0);
                        } else if (getPacketType(spbuf) == 1) {
                            hbcount = 0;
                        }
                    }
                }
                if (debug)
                    printf("Debug: Waiting for next message\n");

                //Zero and clear fd set and rebuild
                if (debug)
                    printf("Debug: Setting the file desciptors\n");
                FD_ZERO(&readfds);
                FD_SET(tnSockTemp, &readfds);
                FD_SET(spSock, &readfds);
                n = tnSockTemp + 1;
                if (n <= spSock + 1) {
                    n = spSock + 1;
                }
            }
        }
        if (debug) {
            printf("Debug: Closing connections\n");
            setIp(ipB);
            printf("ipA: %s\nipB: %s\n", ipA, ipB);
        }
        close(tnSockTemp);
        close(spSock);
        break;
    }
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






