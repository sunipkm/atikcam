// Server side C/C++ program to demonstrate Socket programming 
#include <unistd.h> 
#include <stdio.h> 
#include <sys/socket.h> 
#include <stdlib.h> 
#include <netinet/in.h> 
#include <string.h> 
#include <iostream>
using namespace std;
#define PORT 12345

/* Internal data structure */
typedef struct image {
	uint64_t tnow ; // timestamp in ms (from epoch, in local time)
	float exposure ; //exposure length in seconds
	unsigned short pixx ; //pixel x
	unsigned short pixy ; //pixel y
    unsigned int imgsize ; //pixx*pixy
    short ccdtemp ; // temp in C = temp/100
    short boardtemp ;
    short chassistemp ;
	unsigned short picdata[1449072] ;
	unsigned char unused[6] ; //first set of padding, default for GCC -> multiple of 32
	unsigned char unused2[1792] ; //padding to round off to 708*4096 bytes
} image ; //size 708*4096
/* End internal data structure */

/* Packet Serializer */
#ifndef PACK_SIZE
#define PACK_SIZE 8192
#endif
typedef union{
	image a ;
	unsigned char buf[sizeof(image)/PACK_SIZE][PACK_SIZE];
} packetize ;
/* Packet Serializer */

int main(int argc, char const *argv[]) 
{ 
    int server_fd, new_socket, valread; 
    struct sockaddr_in address; 
    int opt = 1; 
    int addrlen = sizeof(address);  
       
    // Creating socket file descriptor 
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) 
    { 
        perror("socket failed"); 
        exit(EXIT_FAILURE); 
    } 
       
    // Forcefully attaching socket to the port 8080 
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, 
                                                  &opt, sizeof(opt))) 
    { 
        perror("setsockopt"); 
        exit(EXIT_FAILURE); 
    } 
    address.sin_family = AF_INET; 
    address.sin_addr.s_addr = INADDR_ANY; 
    address.sin_port = htons( PORT ); 
       
    // Forcefully attaching socket to the port 8080 
    if (bind(server_fd, (struct sockaddr *)&address,  
                                 sizeof(address))<0) 
    { 
        perror("bind failed"); 
        exit(EXIT_FAILURE); 
    } 
    if (listen(server_fd, 3) < 0) 
    { 
        perror("listen"); 
        exit(EXIT_FAILURE); 
    } 
    packetize p ;
    while (true){
    for ( int i = 0 ; i < sizeof(image)/PACK_SIZE; i++ ){
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address,  
                       (socklen_t*)&addrlen))<0) 
        { 
            perror("accept"); 
            exit(EXIT_FAILURE); 
        }
        valread = read( new_socket , &p.buf[i], PACK_SIZE);
        if ( valread != PACK_SIZE )
        cout << i << " th read: " << valread << "/" << PACK_SIZE << endl ;
        close(new_socket);
    }
    cout << "Temperature: " << p.a.ccdtemp << " C" << endl ;
    cout << "Timestamp: " << p.a.tnow << " ms" << endl ;
    cout << "Image size: " << p.a.imgsize << endl ;}
    return 0; 
} 