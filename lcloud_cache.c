////////////////////////////////////////////////////////////////////////////////
//
//  File           : lcloud_cache.c
//  Description    : This is the cache implementation for the LionCloud
//                   assignment for CMPSC311.
//
//   Author        : Patrick McDaniel
//   Last Modified : Thu 19 Mar 2020 09:27:55 AM EDT
//

// Includes 
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <cmpsc311_log.h>
#include <lcloud_cache.h>


// User defined structs
////////////////////////////////////////////////////////////////////////////////

// Cache line (doubly linked-list)
typedef struct listNode {

    struct listNode* prev;
    struct listNode* next;
    LcDeviceId did;
    uint16_t sec;
    uint16_t blk;
    char block[256];

} listNode;

// Cache linked-list storing lines of cached data
typedef struct linkedList {
    listNode* head;
    listNode* tail;
    int maxblocks;
    int currentblocks;
} linkedList;

struct linkedList* cache;
//
// Functions
int cacheReplaceLine(listNode* node);
int cacheAddLine(listNode* node);
////////////////////////////////////////////////////////////////////////////////
//
// Function     : lcloud_getcache
// Description  : Search the cache for a block 
//
// Inputs       : did - device number of block to find
//                sec - sector number of block to find
//                blk - block number of block to find
// Outputs      : cache block if found (pointer), NULL if not or failure


char * lcloud_getcache(LcDeviceId did, uint16_t sec, uint16_t blk ) {
    listNode* node = cache->head;
    if (node == NULL || cache->currentblocks == 0) {
        return (NULL);
    }
    if ((node->next == NULL) && (node->did == did) && (node->sec == sec) && (node->blk == blk)) {
        if (cacheReplaceLine(node) != -1) {
                return(node->block);
        }
    }
    while (node != NULL) {
        if ((node->did == did) && (node->sec == sec) && (node->blk == blk)) {
            // Put the recent used block to the head of the cache (linked-list)
            if (cacheReplaceLine(node) != -1) {
                return(node->block);
            }
            return(NULL);
        }
        node = node->next;
    }
    return( NULL );
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : lcloud_putcache
// Description  : Put a value in the cache 
//
// Inputs       : did - device number of block to insert
//                sec - sector number of block to insert
//                blk - block number of block to insert
// Outputs      : 0 if succesfully inserted, -1 if failure

int lcloud_putcache( LcDeviceId did, uint16_t sec, uint16_t blk, char *block ) {
    listNode* node = cache->head;
    if (cache->tail!= NULL) {
    }
    while (node != NULL) {
        //printf("------------------------------node: sec:%d blk:%d\n",node->sec,node->blk );
        if ((node->did == did) && (node->sec == sec) && (node->blk == blk)) {
            if (cacheReplaceLine(node) != -1) {
                memcpy(&node->block[0], &block[0], 256);
                return(0);
            }
        }
        node = node->next;
    }

    listNode* newNode = malloc(sizeof(listNode));
    newNode->did = did;
    newNode->sec = sec;
    newNode->blk = blk;
    memcpy(&newNode->block[0], &block[0], 256);
    cacheAddLine(newNode);

    return(0);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : lcloud_initcache
// Description  : Initialze the cache by setting up metadata a cache elements.
//
// Inputs       : maxblocks - the max number number of blocks 
// Outputs      : 0 if successful, -1 if failure

int lcloud_initcache( int maxblocks ) {
    cache = malloc(sizeof(linkedList *));

    cache->head = NULL;
    cache->tail = NULL;
    cache->maxblocks = maxblocks;
    cache->currentblocks = 0;
    return(1);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : lcloud_closecache
// Description  : Clean up the cache when program is closing
//
// Inputs       : none
// Outputs      : 0 if successful, -1 if failure

int lcloud_closecache( void ) {
    listNode* node = cache->head;
    listNode* next = NULL;
    while (node != NULL) {
        next = node->next;
        free(node);
        node = next;
    }
    cache->head = NULL;
    cache->tail = NULL;
    cache->currentblocks = 0;
    cache->maxblocks = 0;
    return(0);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : CacheReplaceLine
// Description  : Put current node to the head
//
// Inputs       : listNode* node
// Outputs      : 0 if successful, -1 if failure
int cacheReplaceLine(listNode* node) {
    //printf("BLK: %d  SEC: %d\n", node->next->blk, node->next->sec);
    if (node == NULL || cache->head == NULL) {
        return(-1);
    }
    if (node->did == cache->head->did && node->sec == cache->head->sec && 
        node->blk == cache->head->blk) {
        return(0);
    }
    if (node->next != NULL) {
        node->next->prev = node->prev;
    }
    if (node->prev != NULL) {
        node->prev->next = node->next;
    }

    cache->head->prev = node;
    node->next = cache->head;
    node->prev = NULL;
    cache->head = node;

    return(0);
}

////////////////////////////////////////////////////////////////////////////////
//
// Function     : CacheAddLine
// Description  : Add a new line to the cache head
//
// Inputs       : listNode* node
// Outputs      : 0 if successful, -1 if failure
int cacheAddLine(listNode* node) {
    if (cache->currentblocks == cache->maxblocks) {
        cache->maxblocks *= 2;
    }
    if (cache->head == NULL) {
        node->next = NULL;
        node->prev = NULL;
        cache->head = node;
        cache->tail = node;
        cache->currentblocks ++;
        return(0);
    }
    if (cache->tail == cache->head) {
        cache->head->prev = node;
        cache->head = node;
        node->next = cache->tail;
        cache->tail->prev = node;
        cache->currentblocks ++;
        return(0);
    }
    cache->head->prev = node;
    node->next = cache->head;
    cache->head = node;
    node->prev = NULL;
    cache->currentblocks ++;
    return(0);
}
