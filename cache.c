#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "cache.h"

static cache_entry_t *cache = NULL;
static int cache_size = 0;
static int clock = 0;
static int num_queries = 0;
static int num_hits = 0;

static int created = 0; // variable checking if cache is created

int cache_create(int num_entries) {
  if(((num_entries > 4096)| (num_entries < 2) | (created == 1)) ){ // condition for successful create
    return -1;
  }

  cache_size = num_entries;
  cache = calloc(cache_size, sizeof(cache_entry_t)); //allocate cache

  //initialize entry
  for(int i=0; i<(cache_size); i++){
    cache[i].valid = true;
    cache[i].disk_num = 0;
    cache[i].block_num = 0;
    cache[i].access_time = 0;
  }

  created = 1; // variable checking if cache is created
  return 1;
}

int cache_destroy(void) {
  if(created == 0){ // when cache is not created
    return -1;
  }
  cache = NULL;
  cache_size = 0;
  created = 0;
  return 1;
}

int cache_lookup(int disk_num, int block_num, uint8_t *buf) {
  if((created == 0) | (disk_num < 0) | (disk_num > JBOD_NUM_DISKS) | (block_num < 0) | (block_num > JBOD_NUM_BLOCKS_PER_DISK) | (buf == NULL)){
    return -1;
  } // condition when failing cache_lookup
  clock ++;
  num_queries ++; 
  //loop to search the specific disk_num and block_num
  for(int i=0; i<cache_size; i++){
    if((cache[i].disk_num == disk_num) && (cache[i].block_num == block_num) && (cache[i].valid == false)){
      num_hits ++;
      cache[i].access_time = clock;
      memcpy(buf, cache[i].block, JBOD_BLOCK_SIZE);
      return 1;
    }
  }
  return -1;
}

void cache_update(int disk_num, int block_num, const uint8_t *buf) {
  clock ++;
  for(int i=0; i <cache_size; i++){
    if((cache[i].disk_num == disk_num) && (cache[i].block_num == block_num) && (cache[i].valid == false)){
      cache[i].access_time = clock;
      memcpy(cache[i].block, buf, JBOD_BLOCK_SIZE);
    }
  }
}

int cache_insert(int disk_num, int block_num, const uint8_t *buf) {
  if((created == 0) | (disk_num < 0) | (disk_num > JBOD_NUM_DISKS) | (block_num < 0) | (block_num > JBOD_NUM_BLOCKS_PER_DISK) | (buf == NULL)){
    return -1;
  }
  clock ++;
  //check if the entry already exists, check if the entry is full, check the least recently used entry
  int full = 0; //check if entry is full
  int minimum = 0; //checking minimum clock
  int index = 0; //index to insert
  for(int i=0; i<cache_size; i++){
    if((cache[i].disk_num == disk_num) && (cache[i].block_num == block_num) && (cache[i].valid == false)){
      return -1;
    }
    if(cache[i].access_time < cache[minimum].access_time){
      minimum = i;
    }
    if(cache[i].valid == true){
      index = i;
      break;
    }
    full = i;
  }

  //if entry is full set index to minimum clock
  if((full+1) == cache_size){ 
    index = minimum;
  }

  cache[index].block_num = block_num;
  cache[index].disk_num = disk_num;
  cache[index].valid = false;
  cache[index].access_time = clock;
  memcpy(cache[index].block, buf, JBOD_BLOCK_SIZE);
  return 1;
}

bool cache_enabled(void) {
  if(cache_size < 2){
  return false;
  }
  return true;
}

void cache_print_hit_rate(void) {
  fprintf(stderr, "Hit rate: %5.1f%%\n", 100 * (float) num_hits / num_queries);
}
