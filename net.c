#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <err.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include "net.h"
#include "jbod.h"

/* the client socket descriptor for the connection to the server */
int cli_sd = -1;

/* attempts to read n (len) bytes from fd; returns true on success and false on failure. 
It may need to call the system call "read" multiple times to reach the given size len. 
*/
static bool nread(int fd, int len, uint8_t *buf) {
  int length = len; 
  int k = 0; // position
  int size = 0;

  while(length>0){ // iteration until reading all 
    size = read(fd, &buf[k], length);
    // if(length == 0){
    //   break;
    // }
    if(size == -1){
      return false;
    }
    length = length - size;
    k = k + size;
  }

  // if(read(fd, &buf[0], len) == -1){ //without loop call read()
  //   return false;
  // }

  return true;
}

/* attempts to write n bytes to fd; returns true on success and false on failure 
It may need to call the system call "write" multiple times to reach the size len.
*/
static bool nwrite(int fd, int len, uint8_t *buf) {
  ssize_t length = len;
  int k = 0; //position
  int size = 0;

  while(length>0){ // iteration until reading all 
    size = write(fd, &buf[k], length);
    // if(length == 0){
    //   break;
    // }
    if(size == -1){
      return false;
    }
    length = length - size;
    k = k + size;
  }
  // if(write(fd, buf, len) != len){ // without loop call write()
  //   return false;
  // }

  return true;
}

/* Through this function call the client attempts to receive a packet from sd 
(i.e., receiving a response from the server.). It happens after the client previously 
forwarded a jbod operation call via a request message to the server.  
It returns true on success and false on failure. 
The values of the parameters (including op, ret, block) will be returned to the caller of this function: 

op - the address to store the jbod "opcode"  
ret - the address to store the return value of the server side calling the corresponding jbod_operation function.
block - holds the received block content if existing (e.g., when the op command is JBOD_READ_BLOCK)

In your implementation, you can read the packet header first (i.e., read HEADER_LEN bytes first), 
and then use the length field in the header to determine whether it is needed to read 
a block of data from the server. You may use the above nread function here.  
*/
static bool recv_packet(int sd, uint32_t *op, uint16_t *ret, uint8_t *block) {
  uint8_t buf[HEADER_LEN]; //initiall set to length of header
  uint8_t *p = buf; //pointer of buf
  uint16_t len; // packet length

  if(nread(sd, HEADER_LEN, p) == false){
    return false;
  }

  //decode header
  memcpy(&len, &p[0], 2);
  len = ntohs(len);
  memcpy(op, &p[2], 4);
  *op = ntohl(*op);
  memcpy(ret, &p[6], 2);
  *ret = ntohs(*ret);

  if(*ret==-1){
    return false;
  }

  if(len > HEADER_LEN){ //if the packet has block to receive, then read using nread
    if(nread(sd, JBOD_BLOCK_SIZE, &block[0]) == false){
      return false;
    }
  }

  return true;
}



/* The client attempts to send a jbod request packet to sd (i.e., the server socket here); 
returns true on success and false on failure. 

op - the opcode. 
block- when the command is JBOD_WRITE_BLOCK, the block will contain data to write to the server jbod system;
otherwise it is NULL.

The above information (when applicable) has to be wrapped into a jbod request packet (format specified in readme).
You may call the above nwrite function to do the actual sending.  
*/
static bool send_packet(int sd, uint32_t op, uint8_t *block) {
  uint16_t length = HEADER_LEN;
  uint16_t ret = 0;

  uint32_t oop = op >> 26;
  if (oop == JBOD_WRITE_BLOCK){ // case when op is jbod_write_block
    uint8_t buf[HEADER_LEN + JBOD_BLOCK_SIZE];
    uint8_t *p = buf;
    length = length + JBOD_BLOCK_SIZE;
    memcpy(&p[8], &block[0], JBOD_BLOCK_SIZE);

    uint16_t len = length;
    length = htons(length);
    memcpy(&p[0], &length, 2);
    op = htonl(op);
    memcpy(&p[2], &op, 4);
    ret = htons(ret); //htons since uint16_t
    memcpy(&p[6], &ret, 2);
    if(nwrite(sd, len, p) == false){
      return false;
    }

    return true;
  }
  // case where there are no blocks to write (op not jbod_write_block)
  uint8_t buf[length];
  uint8_t *p = buf;

  //encode
  uint16_t len = length;
  length = htons(length);
  memcpy(&p[0], &length, 2);
  op = htonl(op);
  memcpy(&p[2], &op, 4);
  ret = htons(ret); //htons since uint16_t
  memcpy(&p[6], &ret, 2);

  // if (len > HEADER_LEN){
  //   memcpy(&buf[8], &block, JBOD_BLOCK_SIZE);
  // }

  if(nwrite(sd, len, p) == false){
    return false;
  }

  // nwrite(sd, length, buf);

  return true;
}



/* attempts to connect to server and set the global cli_sd variable to the
 * socket; returns true if successful and false if not. 
 * this function will be invoked by tester to connect to the server at given ip and port.
 * you will not call it in mdadm.c
*/
bool jbod_connect(const char *ip, uint16_t port) {
  struct sockaddr_in caddr;
  inet_aton(JBOD_SERVER, &caddr.sin_addr);

  //create socket
  cli_sd = socket(AF_INET, SOCK_STREAM, 0);

  // Setup the address information
  caddr.sin_family = AF_INET;
  caddr.sin_port = htons(JBOD_PORT);

  int connection = connect(cli_sd, (const struct sockaddr *)&caddr, sizeof(caddr));

  if (connection == -1){
    return false;
  }

  return true;
}




/* disconnects from the server and resets cli_sd */
void jbod_disconnect(void) {
  close(cli_sd);
  cli_sd = -1;
}



/* sends the JBOD operation to the server (use the send_packet function) and receives 
(use the recv_packet function) and processes the response. 

The meaning of each parameter is the same as in the original jbod_operation function. 
return: 0 means success, -1 means failure.
*/
int jbod_client_operation(uint32_t op, uint8_t *block) {
  uint16_t ret;
  uint32_t rec_op;

  if(send_packet(cli_sd, op, block) == false){ //send packet
    return -1;
  }
  if(recv_packet(cli_sd, &rec_op, &ret, block) == false){ //receive packet
    return -1;
  }

  return 0;
}
