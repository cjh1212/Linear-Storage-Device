//CMPSC 311 SP22
//LAB 5

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

#include "mdadm.h"
#include "jbod.h"
#include "cache.h"
#include "net.h"

int mounted = 0;

int mdadm_mount(void) {
  if(mounted == 0){
    uint32_t op = JBOD_MOUNT;
    op = op << 26;
    jbod_client_operation(op, NULL);
    mounted = 1;
    return 1;
  }
  else{
    return -1;
  }
}

int mdadm_unmount(void) {
  if(mounted == 1){
    uint32_t op = JBOD_UNMOUNT;
    op = op << 26;
    jbod_client_operation(op, NULL);
    mounted = 0;
    return 1;
  }
  else{
    return -1;
  }
}

int mdadm_read(uint32_t addr, uint32_t len, uint8_t *buf) {
  if(mounted != 1 || len > 1024 || addr+len > JBOD_DISK_SIZE*16 || (len == 0) & (buf != NULL) || (buf == NULL) & (len > 0)){
      return -1;
  }
  
  
  uint32_t diskID = addr/JBOD_DISK_SIZE; 
  uint32_t blockID = (addr - (diskID * JBOD_DISK_SIZE)) / JBOD_BLOCK_SIZE;
  uint32_t final = addr + len; // final address
  uint32_t finalDiskID = final/JBOD_DISK_SIZE; // disk number of final address
  uint32_t finalBlockID = (final - (finalDiskID * JBOD_DISK_SIZE)) / JBOD_BLOCK_SIZE; // block id of final address
  int offset = final - ((finalDiskID * JBOD_DISK_SIZE) + (finalBlockID * JBOD_BLOCK_SIZE));
  uint32_t op;
  
  //check if in the cache

  


  if (len > JBOD_BLOCK_SIZE){
    offset = offset + ((JBOD_BLOCK_SIZE * ((final - addr)/JBOD_BLOCK_SIZE))); // case when offset goes off for more than a block
  }

  if (diskID != finalDiskID){ // when reading across disks (assume crossing only two disks)
    
    int k = 0;
    int current_addr = addr;
    int current_addr1 = addr - ((diskID*JBOD_DISK_SIZE) + (blockID*JBOD_BLOCK_SIZE));
    uint8_t a = 0;
   
    int addrToNextDisk = finalDiskID * JBOD_DISK_SIZE;
    uint32_t diskIDD = diskID << 22; // shift bits to fit in operation format (DiskID: 22-25)

    offset = len - (JBOD_BLOCK_SIZE - current_addr1);
    int length = JBOD_BLOCK_SIZE - current_addr1;



    //for cache
    int diskID_cache = diskID;
    int blockID_cache = blockID;

    
    while (current_addr < addrToNextDisk){ // reading before the next disk
      uint8_t tb[256]; // assigning 256 bytes of block
      uint8_t *p = tb;
      
      uint8_t buff[256];
      uint8_t *pp = buff;
      
      //check if cache is available and use it if it is the case
      if(cache_lookup(diskID_cache, blockID_cache, pp) == 1){
        memcpy(&buf[k], &pp[current_addr1], length);
      }else{
        op = JBOD_SEEK_TO_DISK;
        op = op << 26;
        uint32_t seekDisk = op|diskIDD|(blockID);
        jbod_client_operation(seekDisk, NULL);

        op = JBOD_SEEK_TO_BLOCK;
        op = op << 26;
        uint32_t seekBlock = op|diskIDD|(blockID);
        jbod_client_operation(seekBlock, NULL);

        op = JBOD_READ_BLOCK;
        op = op << 26;
        jbod_client_operation(op, p);

        memcpy(&buf[k], &p[current_addr1], length);
        cache_insert(diskID_cache, blockID_cache, p);
      }


      current_addr = current_addr + length;
      k = k + length;
      current_addr1 = 0;
      a = a + 1;

      if(offset < JBOD_BLOCK_SIZE){
        length = offset;
      }
      else{
        offset = offset - JBOD_BLOCK_SIZE;
        length = JBOD_BLOCK_SIZE;
      }
    }
    diskIDD = diskID + 1;
    diskID = diskID + 1;
    int b = 0;
    blockID = 0;

    //cache
    diskID_cache = diskID;
    blockID_cache = blockID;
    uint8_t buff[256];
    uint8_t *pp = buff;

    while (current_addr < final){ // reading the next disk
      diskIDD = diskIDD << 22; // shift bits to fit in operation format (DiskID: 22-25)

      //check if cache is available and use it if it is the case
      if(cache_lookup(diskID_cache, blockID_cache, pp) == 1){
        memcpy(&buf[k], &pp[current_addr1], length);
      }else{
        op = JBOD_SEEK_TO_DISK;
        op = op << 26;
        uint32_t seekDisk = op|diskIDD|(blockID+b);
        jbod_client_operation(seekDisk, NULL);

        op = JBOD_SEEK_TO_BLOCK;
        op = op << 26;
        uint32_t seekBlock = op|diskIDD|(blockID+b);
        jbod_client_operation(seekBlock, NULL);

        op = JBOD_READ_BLOCK;
        op = op << 26;

        uint8_t tb[256]; // assigning 256 bytes of block
        uint8_t *p = tb;

        jbod_client_operation(op, p);

        memcpy(&buf[k], &p[current_addr1], length);
        cache_insert(diskID_cache, blockID_cache, p);
      }



      current_addr = current_addr + length;
      k = k + length;
      current_addr1 = 0;
      b = b + 1;

      if(offset < JBOD_BLOCK_SIZE){
        length = offset;
      }
      else{
        offset = offset - JBOD_BLOCK_SIZE;
        length = JBOD_BLOCK_SIZE;

      }
    }

  }
  else{// not across the disks
    int diskIDD = diskID;

    //for caching
    int diskID_cache = diskID;
    int blockID_cache = blockID;
    uint8_t buff[256];
    uint8_t *pp = buff;

    diskID = diskID << 22; // shift bits to fit in operation format (DiskID: 22-25)


    uint32_t current_addr1 = addr - ((diskIDD*JBOD_DISK_SIZE)+(blockID*JBOD_BLOCK_SIZE));
    offset = len - (JBOD_BLOCK_SIZE - current_addr1);

    if (blockID == finalBlockID){ // case within the block

      //check if caching available
      if(cache_lookup(diskID_cache, blockID_cache, pp) == 1){
        memcpy(&buf[0], &pp[current_addr1], len);
      }else{
        op = JBOD_SEEK_TO_DISK;
        op = op << 26;

        uint32_t seekDisk = op|diskID|blockID; // operation format for seeking disk
        jbod_client_operation(seekDisk, NULL);

        op = JBOD_SEEK_TO_BLOCK;
        op = op << 26; // shift bits to fit in operation format (command: 26-31)
        
        uint32_t seekBlock = op|diskID|blockID; // operation format for seeking block
        jbod_client_operation(seekBlock, NULL);

        op = JBOD_READ_BLOCK;
        op = op << 26; // enough since other fields are ignored

        uint8_t tb[256];
        uint8_t *p = tb;

        jbod_client_operation(op, p);

        memcpy(&buf[0], &p[current_addr1], len);
        cache_insert(diskID_cache, blockID_cache, p);
      }

    }
    else{ // case across the blocks
      uint32_t a = 0;

      uint32_t current_addr = addr;
      uint32_t current_addr1 = addr - ((diskIDD*JBOD_DISK_SIZE)+(blockID*JBOD_BLOCK_SIZE));
      offset = len - (JBOD_BLOCK_SIZE - current_addr1);
      uint32_t k = 0;
      uint32_t length = JBOD_BLOCK_SIZE - current_addr1;

      while(current_addr < final){

        if(cache_lookup(diskID_cache, blockID_cache, pp) == 1){
          memcpy(&buf[k], &pp[current_addr1], length);
        }else{

          op = JBOD_SEEK_TO_DISK;
          op = op << 26;

          uint32_t seekDisk = op|diskID|blockID; // operation format for seeking disk
          jbod_client_operation(seekDisk, NULL);

          op = JBOD_SEEK_TO_BLOCK;
          op = op << 26; // shift bits to fit in operation format (command: 26-31)
          
          uint32_t seekBlock = op|diskID|blockID; // operation format for seeking block
          jbod_client_operation(seekBlock, NULL);

          op = JBOD_READ_BLOCK;
          op = op << 26; // enough since other fields are ignored

          uint8_t tb[256]; // assigning 256 bytes of block
          uint8_t *p = tb;

          op = JBOD_READ_BLOCK;
          op = op << 26; // enough since other fields are ignored
          jbod_client_operation(op, p);

          memcpy(&buf[k], &p[current_addr1], length);
          cache_insert(diskID_cache, blockID_cache, p);
        }



        current_addr = current_addr + length;
        k = k + length;
        current_addr1 = 0;
        a = a + 1;
        blockID_cache = blockID_cache + 1;
        blockID = blockID + 1;

        if(offset < JBOD_BLOCK_SIZE){
          length = offset;
        }
        else{
          offset = offset - JBOD_BLOCK_SIZE;
          length = JBOD_BLOCK_SIZE;

        }
      
      }


    }
  }
  return len;
}



int mdadm_write(uint32_t addr, uint32_t len, const uint8_t *buf) {
  if(mounted != 1 || len > 1024 || addr+len > JBOD_DISK_SIZE*16 || (len == 0) & (buf != NULL) || (buf == NULL) & (len > 0)){
    return -1;
  }
  
  uint32_t diskID = addr/JBOD_DISK_SIZE; 
  uint32_t blockID = (addr - (diskID * JBOD_DISK_SIZE)) / JBOD_BLOCK_SIZE;
  uint32_t final = addr + len; // final address
  uint32_t finalDiskID = final/JBOD_DISK_SIZE; // disk number of final address
  uint32_t finalBlockID = (final - (finalDiskID * JBOD_DISK_SIZE)) / JBOD_BLOCK_SIZE; // block id of final address
  int offset = final - ((finalDiskID * JBOD_DISK_SIZE) + (finalBlockID * JBOD_BLOCK_SIZE));
  uint32_t op;
  // uint32_t offBlock = len/JBOD_BLOCK_SIZE; // how much offset goes off the block

  if (len > JBOD_BLOCK_SIZE){
    offset = offset + ((JBOD_BLOCK_SIZE * ((final - addr)/JBOD_BLOCK_SIZE))); // case when offset goes off for more than a block
  }

  if (diskID != finalDiskID){ // when reading across disks (assume crossing only two disks)
    
    int k = 0;
    int current_addr = addr;
    int current_addr1 = addr - ((diskID*JBOD_DISK_SIZE) + (blockID*JBOD_BLOCK_SIZE));
    uint8_t a = 0;
    
    int addrToNextDisk = finalDiskID * JBOD_DISK_SIZE;

    offset = len - (JBOD_BLOCK_SIZE - current_addr1);
    uint32_t length = JBOD_BLOCK_SIZE - current_addr1;

    uint32_t diskIDDD = diskID << 22;

    op = JBOD_SEEK_TO_DISK;
    op = op << 26;

    uint32_t seekDisk = op|diskIDDD|blockID; // operation format for seeking disk
    jbod_client_operation(seekDisk, NULL);

    op = JBOD_SEEK_TO_BLOCK;
    op = op << 26; // shift bits to fit in operation format (command: 26-31)
    
    uint32_t seekBlock = op|diskIDDD|blockID; // operation format for seeking block
    jbod_client_operation(seekBlock, NULL);

    //for caching
    int diskID_cache = diskID;
    int blockID_cache = blockID;

    
    
    while (current_addr < addrToNextDisk){// before the next block
      uint8_t tb[256]; // assigning 256 bytes of block
      uint8_t *p = tb;
      uint32_t diskIDD = diskID << 22; // shift bits to fit in operation format (DiskID: 22-25)

      op = JBOD_READ_BLOCK;
      op = op << 26; // enough since other fields are ignored

      jbod_client_operation(op, p);

      op = JBOD_SEEK_TO_DISK;
      op = op << 26;
      uint32_t seekDisk = op|diskIDD|(blockID+a);
      jbod_client_operation(seekDisk, NULL);

      op = JBOD_SEEK_TO_BLOCK;
      op = op << 26;
      uint32_t seekBlock = op|diskIDD|(blockID+a);
      jbod_client_operation(seekBlock, NULL);

      op = JBOD_WRITE_BLOCK;
      op = op << 26;

      memcpy(&p[current_addr1], &buf[k], length);

      jbod_client_operation(op, p);

      //write-though policy
      cache_insert(diskID_cache, blockID_cache, p);
      

      current_addr = current_addr + length;
      k = k + length;
      current_addr1 = 0;
      a = a + 1;
      blockID_cache = blockID_cache + 1;

      if(offset < JBOD_BLOCK_SIZE){
        length = offset;
      }
      else{
        offset = offset - JBOD_BLOCK_SIZE;
        length = JBOD_BLOCK_SIZE;
      }
    }
    uint32_t diskIDD = diskID + 1;
    int b = 0;
    blockID = 0;
    diskID_cache = diskID_cache + 1;
    blockID_cache = 0;

    op = JBOD_SEEK_TO_DISK;
    op = op << 26;

    uint32_t diskID1 = diskIDD << 22;

    seekDisk = op|diskID1|blockID; // operation format for seeking disk
    jbod_client_operation(seekDisk, NULL);

    op = JBOD_SEEK_TO_BLOCK;
    op = op << 26; // shift bits to fit in operation format (command: 26-31)
    
    seekBlock = op|diskID1|blockID; // operation format for seeking block
    jbod_client_operation(seekBlock, NULL);


    while (current_addr < final){// next block

      uint8_t tb[256]; // assigning 256 bytes of block
      uint8_t *p = tb;


      op = JBOD_READ_BLOCK;
      op = op << 26; // enough since other fields are ignored

      jbod_client_operation(op, p); // read before write


      op = JBOD_SEEK_TO_DISK;
      op = op << 26;
      seekDisk = op|diskID1|(blockID+b);
      jbod_client_operation(seekDisk, NULL);

      op = JBOD_SEEK_TO_BLOCK;
      op = op << 26;
      seekBlock = op|diskID1|(blockID+b);
      jbod_client_operation(seekBlock, NULL);

      op = JBOD_WRITE_BLOCK;
      op = op << 26;

      memcpy(&p[current_addr1], &buf[k], length);

      jbod_client_operation(op, p);

      //write-through policy
      cache_insert(diskID_cache, blockID_cache, p);

      current_addr = current_addr + length;
      k = k + length;
      current_addr1 = 0;
      b = b + 1;
      blockID_cache = blockID_cache + 1;

      if(offset < JBOD_BLOCK_SIZE){
        length = offset;
      }
      else{
        offset = offset - JBOD_BLOCK_SIZE;
        length = JBOD_BLOCK_SIZE;

      }
    }

  }
  else{// case across the blocks
    int diskIDD = diskID;

    //for caching
    int diskID_cache = diskID;
    int blockID_cache = blockID;

    diskID = diskID << 22; // shift bits to fit in operation format (DiskID: 22-25)


    op = JBOD_SEEK_TO_DISK;
    op = op << 26;

    uint32_t seekDisk = op|diskID|blockID; // operation format for seeking disk
    jbod_client_operation(seekDisk, NULL);

    op = JBOD_SEEK_TO_BLOCK;
    op = op << 26; // shift bits to fit in operation format (command: 26-31)
    
    uint32_t seekBlock = op|diskID|blockID; // operation format for seeking block
    jbod_client_operation(seekBlock, NULL);

    op = JBOD_READ_BLOCK;
    op = op << 26; // enough since other fields are ignored

    uint32_t current_addr1 = addr - ((diskIDD*JBOD_DISK_SIZE)+(blockID*JBOD_BLOCK_SIZE));




    if (blockID == finalBlockID){
      uint8_t tb[256];
      uint8_t *p = tb;

      jbod_client_operation(op, p); //read operation before write

      op = JBOD_SEEK_TO_DISK;
      op = op << 26;
      uint32_t seekDisk = op|diskID|blockID;
      jbod_client_operation(seekDisk, NULL);

      op = JBOD_SEEK_TO_BLOCK;
      op = op << 26;
      uint32_t seekBlock = op|diskID|blockID;
      jbod_client_operation(seekBlock, NULL);


      op = JBOD_WRITE_BLOCK;
      op = op << 26;

      memcpy(&p[current_addr1], &buf[0], len);

      jbod_client_operation(op, p);

      //cache write-through policy
      cache_insert(diskID_cache, blockID_cache, p);
    }
    else{// across the blocks
      uint32_t a = 0; //To count blockID
      uint32_t current_addr = addr;
      uint32_t current_addr1 = addr - ((diskIDD*JBOD_DISK_SIZE)+(blockID*JBOD_BLOCK_SIZE));
      offset = len - (JBOD_BLOCK_SIZE - current_addr1);
      uint32_t k = 0;
      uint32_t length = JBOD_BLOCK_SIZE - current_addr1;

      //for caching
      int diskID_cache = diskID;
      int blockID_cache = blockID;

      while(current_addr < final){

        uint8_t tb[256]; // assigning 256 bytes of block
        uint8_t *p = tb;

        op = JBOD_READ_BLOCK;
        op = op << 26; // enough since other fields are ignored

        jbod_client_operation(op, p);

        op = JBOD_SEEK_TO_DISK;
        op = op << 26;
        seekDisk = op|diskID|(blockID+a);
        jbod_client_operation(seekDisk, NULL);

        op = JBOD_SEEK_TO_BLOCK;
        op = op << 26;
        seekBlock = op|diskID|(blockID+a);
        jbod_client_operation(seekBlock, NULL);

        op = JBOD_WRITE_BLOCK;
        op = op << 26;

        memcpy(&p[current_addr1], &buf[k], length);

        jbod_client_operation(op, p);
        //write-through caching policy
        cache_insert(diskID_cache, blockID_cache, p);

        current_addr = current_addr + length;
        k = k + length;
        current_addr1 = 0;
        a = a + 1;
        blockID_cache ++;

        if(offset < JBOD_BLOCK_SIZE){
          length = offset;
        }
        else{
          offset = offset - JBOD_BLOCK_SIZE;
          length = JBOD_BLOCK_SIZE;

        }
      
      }

    }
  }
  return len;
}