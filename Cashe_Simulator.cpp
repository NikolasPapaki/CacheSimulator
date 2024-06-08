/*
 * Copyright 2002-2019 Intel Corporation.
 * 
 * This software is provided to you as Sample Source Code as defined in the accompanying
 * End User License Agreement for the Intel(R) Software Development Products ("Agreement")
 * section 1.L.
 * 
 * This software and the related documents are provided as is, with no express or implied
 * warranties, other than those that are expressly stated in the License.
 */

/*
 *  This file contains an ISA-portable PIN tool for tracing memory accesses.
 */

#include <stdio.h>
#include "pin.H"
#include <cstdio>
#include <math.h>
FILE * trace;

#define SIZE (32*1024)
#define WAYS 2
#define SETS (SIZE/64/WAYS)



struct CacheAddress{
    UINT64 tag;
    UINT64 index;
};


class NwaySetAssociative {
public:
    struct Way {
        bool dirty;
        bool valid;
        UINT64  tag;
    };

    struct Set {
        Way ways[WAYS];
        int lru[WAYS];
        
    };



private:
    // Cache Information
    int numSets;
    int cacheSize;
    int blockSize;
    int numWays;
    Set Sets[SETS];
    int lru_hits[WAYS];  //Track for each cache hit the position of the block in the LRU stack. MRU, MRU-1 ... LRU


    // Cache Statistics
    INT64 accesses ;
    INT64 misses ;
    INT64 hits_clean ;
    INT64 hits_dirty ;
    INT64 replacements_clean ;
    INT64 replacements_dirty ;
    INT64 writebacks ;
    INT64 dirty_blocks ;   


public:
    // Constructor
    NwaySetAssociative(int cacheSize, int blockSize, int ways){
        this->numWays = ways;
        this->numSets = cacheSize / (blockSize * ways);
        this->cacheSize = cacheSize;
        this->blockSize = blockSize;
    
       
        Initialize();

        accesses = 0;
        misses = 0;
        hits_clean = 0;
        hits_dirty = 0;
        replacements_clean = 0;
        replacements_dirty = 0;
        writebacks = 0;
        dirty_blocks = 0;   
    }

    // Used for debuging. Prints all sets 
    void Print_Debug(){
        fprintf(trace, "\n---------------------------------------------------------------------------\n");
        char buffer[15];

        fprintf(trace,"%-10s", "Index");
        for (int i = 0; i < numWays; i++) {
            sprintf(buffer, "Way-%d", i);
            fprintf(trace,"%-15s", buffer);    
        }
        fprintf(trace,"\n");


            for (int i=0; i<numSets; i++){
                fprintf(trace,"%-10d", i);
                for(int j=0; j<numWays; j++){
                    fprintf(trace,"0x%-13lx", Sets[i].ways[j].tag);
                }
                fprintf(trace,"\n");
            }
    }

    void Print(){
        fprintf(trace, "\n---------------------------------------------------------------------------\n");
        for (int i=0; i<numSets; i++){
            fprintf(trace,"%d ", i);
            for(int j=0; j<numWays; j++){
                fprintf(trace,"%d,%d,%lx ", Sets[i].ways[j].valid ? 1:0 , Sets[i].ways[j].dirty ? 1:0 , Sets[i].ways[j].tag);
            }
            fprintf(trace,"\n");
        }
    }



    // Write operation function
    void Write(UINT64 address) {
       accesses++;
        // Call the helper function to derive the tag, index
        CacheAddress cache_address = GetCacheAddress(address);

        bool Hit = false;
        int found_way = -1;
        // Check in each way if the block is precent
        for (int way=0; way<numWays; way++){

            if (Sets[cache_address.index].ways[way].tag == cache_address.tag && Sets[cache_address.index].ways[way].valid){ //Hit
                    found_way = way;
                    Hit = true;
                    if (Sets[cache_address.index].ways[way].dirty){
                        hits_dirty++;
                    }
                    else{
                        hits_clean++;
                        Sets[cache_address.index].ways[way].dirty = true;  
                    }
                
            }
        } 
        


        if(!Hit){ //The address was not found in an of the cache ways. Miss
            misses++;
            // Find the way that the block will be stored
            int victim_way = get_victim(cache_address);

            // Write the block in that way
            
            if(Sets[cache_address.index].ways[victim_way].valid){
                
                if(Sets[cache_address.index].ways[victim_way].dirty){
                    writebacks++;
                    replacements_dirty++;
                }else{
                    replacements_clean++;
                }
                Sets[cache_address.index].ways[victim_way].tag = cache_address.tag;
                Sets[cache_address.index].ways[victim_way].dirty = false;
                Sets[cache_address.index].ways[victim_way].valid = true; 

            }else{
                Sets[cache_address.index].ways[victim_way].tag = cache_address.tag;
                Sets[cache_address.index].ways[victim_way].dirty = false;
                Sets[cache_address.index].ways[victim_way].valid = true; 
            }

            // Write-alocate , new block is in the MRU position
            Update_Policy(victim_way, cache_address);
        }else{
            // Block Hit now is in the MRU position
            Update_Policy(found_way, cache_address);
            Track_Hits(found_way,cache_address);
        }

        
        
    }

    // // Write operation function
    void Read(UINT64 address) {
        accesses++;
        // Call the helper function to derive the tag, index
        CacheAddress cache_address = GetCacheAddress(address);

        bool Hit = false;
        int found_way = -1;
        // Check in each way if the block is precent
        for (int way=0; way<numWays; way++){

            if (Sets[cache_address.index].ways[way].tag == cache_address.tag && Sets[cache_address.index].ways[way].valid){ //Hit
                    found_way = way;
                    Hit = true;
                    if (Sets[cache_address.index].ways[way].dirty){
                        hits_dirty++;
                    }
                    else{
                        hits_clean++;
                    }
            }
        } 
        


        if(!Hit){ //The address does was not found in an of the cache ways. Miss
            misses++;
            // Find the way that the block will be stored
            int victim_way = get_victim(cache_address);

            // Write the block in that way and update replacement policy
            
            if(Sets[cache_address.index].ways[victim_way].valid){
                
                if(Sets[cache_address.index].ways[victim_way].dirty){
                    writebacks++;
                    replacements_dirty++;
                }else{
                    replacements_clean++;
                }
                Sets[cache_address.index].ways[victim_way].tag = cache_address.tag;
                Sets[cache_address.index].ways[victim_way].dirty = false;
                Sets[cache_address.index].ways[victim_way].valid = true; 

            }else{
                Sets[cache_address.index].ways[victim_way].tag = cache_address.tag;
                Sets[cache_address.index].ways[victim_way].dirty = false;
                Sets[cache_address.index].ways[victim_way].valid = true;

            }

            Update_Policy(victim_way, cache_address);
        }else{
            Update_Policy(found_way, cache_address);
            Track_Hits(found_way,cache_address);
        }
    }

    void PrintStats_PKI(INT64 num_instructions){
        
        fprintf(trace, "\n---------------------------------------------------------------------------\n");
        fprintf(trace,"Accesses per kilo Instructions: %ld\n", ((accesses/num_instructions) * 1000) );
        fprintf(trace,"Misses per kilo Instructions: %ld\n", ((misses/num_instructions) * 1000));
        fprintf(trace,"Hits per kilo Instructions: %ld\n",  (((hits_clean + hits_dirty)/num_instructions) * 1000));
        fprintf(trace,"Replacements per kilo Instructions: %ld\n",  (((replacements_clean + replacements_dirty)/num_instructions) * 1000));
        fprintf(trace,"Writebacks per kilo Instructions: %ld\n",  ((writebacks/num_instructions) * 1000));
    }
    
    void PrintStats(){

        fprintf(trace, "\n---------------------------------------------------------------------------\n");
        DirtyBlocks();
        fprintf(trace,"Total Cache Accesses: %ld\n", accesses);
        fprintf(trace,"Total Number of Cache Misses: %ld\n", misses);
        fprintf(trace,"Total Number of Cache Hits: %ld\n", (hits_clean + hits_dirty));
        fprintf(trace,"Total Number of Cache Replacements: %ld\n", (replacements_clean + replacements_dirty));
        fprintf(trace,"Total Number of Cache Writebacks: %ld\n", writebacks);
        fprintf(trace,"Number of dirty blocks: %ld\n", dirty_blocks); 

    }

    void Print_LRU_Hits(){
        fprintf(trace, "\n---------------------------------------------------------------------------\n");
        char buffer[20];

        for (int i = 0; i < numWays; i++) {
            if(i==0){
                sprintf(buffer, "LRU");
            }else if (i<numWays-1){
                sprintf(buffer, "MRU-%d", (WAYS-1-i));
            }else{
                sprintf(buffer, "MRU");
            }
            
            fprintf(trace,"%-15s", buffer);    
        }
        fprintf(trace,"\n");

        for(int j=0; j<numWays; j++){
                    fprintf(trace,"%-13d", lru_hits[j]);
                }
                fprintf(trace,"\n");

    }

private:

    // Initializer
    void Initialize(){
        for (int i=0; i<numSets; i++){
            for(int j=0; j<numWays; j++){
                Sets[i].ways[j].dirty = false;
                Sets[i].ways[j].valid = false;
                Sets[i].ways[j].tag = 0;
                Sets[i].lru[j] = j;
                lru_hits[j] = 0;
            }
        }

    }

    // Track what blocks in the LRU stack are getting Hits
    void Track_Hits(int way, CacheAddress cache_address){
        for (int i=0; i<numWays; i++){
           if(i == way){
                int lru_stack_position = Sets[cache_address.index].lru[i];
                lru_hits[lru_stack_position]++;
            }
        }
    }


    // Update the LRU table after a new block was added
    void Update_Policy(int updated_way, CacheAddress cache_address){
        //fprintf(trace,"BEFORE - %d\t%d\t%d\t%d\n",Sets[cache_address.index].lru[0], Sets[cache_address.index].lru[1], Sets[cache_address.index].lru[2], Sets[cache_address.index].lru[3]);
        // If block is already in the MRU position then no need to update the policy
        if(Sets[cache_address.index].lru[updated_way] != (WAYS-1)){
            
            // If block is in the LRU position then it was Miss and replacement
            if(Sets[cache_address.index].lru[updated_way] == 0){
                for (int i=0; i<numWays; i++){
                    if(i == updated_way){
                        Sets[cache_address.index].lru[i] = (WAYS-1);
                    }else{
                        Sets[cache_address.index].lru[i]-=1;
                        
                        // Sanity Check
                        if(Sets[cache_address.index].lru[i] == -1){
                            //fprintf(trace, "Updated_way : %d\n", updated_way);
                            // fprintf(trace,"AFTER - %d\t%d\t%d\t%d\n",Sets[cache_address.index].lru[0], Sets[cache_address.index].lru[1], Sets[cache_address.index].lru[2], Sets[cache_address.index].lru[3]);
                            fprintf(trace,"There is way with LRU value of -1\n");
                        }
                    }
            }

            // If block is not in the LRU position then it is a HIT but block was not in MRU position
            }else{

                int old_index = Sets[cache_address.index].lru[updated_way];

                for (int i=0; i<numWays; i++){
                    // Block that got a HIT is now in the MRU positions
                    if(i == updated_way){
                        Sets[cache_address.index].lru[i] = (WAYS-1);
                    
                    // All blocks except the one in the LRU position get demoted by 1
                    }else if(Sets[cache_address.index].lru[i] > old_index){
                        Sets[cache_address.index].lru[i]-=1;
                    }    
                        // Sanity Check
                        if(Sets[cache_address.index].lru[i] == -1){
                            //fprintf(trace, "Updated_way : %d\n", updated_way);
                            // fprintf(trace,"AFTER - %d\t%d\t%d\t%d\n",Sets[cache_address.index].lru[0], Sets[cache_address.index].lru[1], Sets[cache_address.index].lru[2], Sets[cache_address.index].lru[3]);
                            fprintf(trace,"There is way with LRU value of -1\n");
                        }
                    }

                
            }
            
        }
        //fprintf(trace,"AFTER -  %d\t%d\t%d\t%d\n\n",Sets[cache_address.index].lru[0], Sets[cache_address.index].lru[1], Sets[cache_address.index].lru[2], Sets[cache_address.index].lru[3]);
    }
    

    // Search through all ways and find the least recently used. The LRU has value 0 and MRU has value 3
    int get_victim(CacheAddress cache_address){

        for (int i=0; i< numWays; i++){
            if(Sets[cache_address.index].lru[i] == 0){
                //fprintf(trace, "Get_Victim : %d\n", i);
                return i;
            }
        }
        //Something Went wrong
        fprintf(trace,"Something Went wrong when finding victim way\n");
        return 0;
    }


    // Helper function to derive the tag, index fields from an address
    CacheAddress GetCacheAddress(UINT64 address) {
        CacheAddress cache_address;

        // Calculate the index and tag values
        UINT64 index_bits = log2(numSets);
        UINT64 offset_bits = log2(blockSize);
        // UINT64 tag_bits = 64 - index_bits - offset_bits;

        UINT64 index_mask = ((1 << index_bits) - 1);
        
        cache_address.index = (address >> offset_bits) & index_mask;

        cache_address.tag  = address >> (index_bits + offset_bits);

        //fprintf(trace,"%lx\t\t%lx\t\t%lld\n", address, cache_address.tag,  cache_address.index);
        return cache_address;
    }

    // Calculates the dirty blocks in the cache at then of the simulation
    void DirtyBlocks(){
        for (int i=0; i<numSets; i++)
            for(int j=0; j<numWays; j++){
            if(Sets[i].ways[j].dirty){
                dirty_blocks++;
            }
        }

    }
   
};
class DirectMapped {
private:
    struct Set {
        bool dirty;
        bool valid;
        UINT64  tag;
    };

    // Cache Information
    int numSets;
    int cacheSize;
    int blockSize;
    Set* Sets;


    // Cache Statistics
    INT64 accesses;
    INT64 misses;
    INT64 hits_clean;
    INT64 hits_dirty;
    INT64 replacements_clean;
    INT64 replacements_dirty;
    INT64 writebacks;
    INT64 dirty_blocks;       // Calculated at the end of the simulation


public:
    // Constructor
    DirectMapped(int cacheSize, int blockSize){
        this->numSets = cacheSize / blockSize;
        this->cacheSize = cacheSize;
        this->blockSize = blockSize;
        this->Sets = new Set[numSets];

        Initialize();

        accesses = 0;
        misses = 0;
        hits_clean = 0;
        hits_dirty = 0;
        replacements_clean = 0;
        replacements_dirty = 0;
        writebacks = 0;
        dirty_blocks = 0; 
    }

    void Print_Debug(){
        fprintf(trace,"Index\tTag\n");
        for (int i=0; i<numSets; i++){
            fprintf(trace,"%d\t\t0x%lx\n", i, Sets[i].tag);
        }
    } 


    // Write operation function
    void Write(UINT64 address) {
        accesses++;
        // Call the helper function to derive the tag, index fields
        CacheAddress cache_address = GetCacheAddress(address);

        // Check if the block is valid
        if (Sets[cache_address.index].valid) {

            if (Sets[cache_address.index].tag == cache_address.tag){    // Hit
                
                if (Sets[cache_address.index].dirty){ 
                    hits_dirty++;
                }
                else{
                    hits_clean++;
                }
                // Block was written so now is considered dirty
                Sets[cache_address.index].dirty = true;     
            
            }else{      // Miss. Because we have a write-alocate cache the block is writen in the cache                                     

                if(Sets[cache_address.index].dirty){    //Dirty. Because we have write-through cache the block must be written to lower level cache.
                    writebacks++;
                    replacements_dirty++;
                }else{
                    replacements_clean++;
                }

                misses++;

                Sets[cache_address.index].valid = true;
                Sets[cache_address.index].dirty = false;
                Sets[cache_address.index].tag = cache_address.tag;                
            }

        } else {
            // Cache miss, update the block with the new tag and mark it as valid
            misses++;
            Sets[cache_address.index].valid = true;
            Sets[cache_address.index].dirty = false;
            Sets[cache_address.index].tag = cache_address.tag;
        }
    }

    // Write operation function
    void Read(UINT64 address) {
        accesses++;
        // Call the helper function to derive the tag, index fields
        CacheAddress cache_address = GetCacheAddress(address);

        // Check if the block is valid
        if (Sets[cache_address.index].valid) {

            if (Sets[cache_address.index].tag == cache_address.tag){ //Hit
                
                if (Sets[cache_address.index].dirty){
                    hits_dirty++;
                }
                else{
                    hits_clean++;
                }

            }
            else { // Miss. Because we have a write-alocate cache the block is writen in the cache            
                
                if(Sets[cache_address.index].dirty) {  //Dirty. Because we have write-through cache the block must be written to lower level cache.
                    writebacks++;
                    replacements_dirty++;
                }else{
                    replacements_clean++;
                }

                misses++;  
                Sets[cache_address.index].valid = true;
                Sets[cache_address.index].dirty = false;
                Sets[cache_address.index].tag = cache_address.tag;    
            }
   
        } else {
            // Cache miss, update the block with the new tag and mark it as valid
            misses++;
            Sets[cache_address.index].valid = true;
            Sets[cache_address.index].dirty = false;
            Sets[cache_address.index].tag = cache_address.tag; 
        }
    }

    void Print(){
        for (int i=0; i<numSets; i++){           
            fprintf(trace,"%d %d,%d,%lx\n", i , Sets[i].valid ? 1:0 , Sets[i].dirty ? 1:0 , Sets[i].tag);
        }
    }
    

    void PrintStats_PKI(INT64 num_instructions){
        fprintf(trace,"Accesses per kilo Instructions: %ld\n", ((accesses/num_instructions) * 1000) );
        fprintf(trace,"Misses per kilo Instructions: %ld\n", ((misses/num_instructions) * 1000));
        fprintf(trace,"Hits per kilo Instructions: %ld\n",  (((hits_clean + hits_dirty)/num_instructions) * 1000));
        fprintf(trace,"Replacements per kilo Instructions: %ld\n",  (((replacements_clean + replacements_dirty)/num_instructions) * 1000));
        fprintf(trace,"Writebacks per kilo Instructions: %ld\n",  ((writebacks/num_instructions) * 1000));
    }

    void PrintStats(){

        DirtyBlocks();
        fprintf(trace,"Total Cache Accesses: %ld\n", accesses);
        fprintf(trace,"Total Number of Cache Misses: %ld\n", misses);
        fprintf(trace,"Total Number of Cache Hits: %ld\n", (hits_clean + hits_dirty));
        fprintf(trace,"Total Number of Cache Replacements: %ld\n", (replacements_clean + replacements_dirty));
        fprintf(trace,"Total Number of Cache Writebacks: %ld\n", writebacks);
        fprintf(trace,"Number of dirty blocks: %ld\n", dirty_blocks); 

    }

    void Print_LRU_Hits(){
        return;
    }

private:

    // Initializer
    void Initialize(){
        for (int i=0; i<numSets; i++){
            Sets[i].dirty = false;
            Sets[i].valid = false;
            Sets[i].tag = 0; //Tag bits
        }

    }

    // Helper function to derive the tag, index fields from an address
    CacheAddress GetCacheAddress(UINT64 address) {
        CacheAddress cache_address;

        // Calculate the index and tag values
        UINT64 index_bits = log2(numSets);
        UINT64 offset_bits = log2(blockSize);
        //UINT64 tag_bits = 64 - index_bits - offset_bits;

        //UINT64 tag_mask = ((1 << tag_bits) - 1);
        UINT64 index_mask = ((1 << index_bits) - 1);
        
        cache_address.index = (address >> offset_bits) & index_mask;

        cache_address.tag  = (address >> (index_bits + offset_bits));

        // fprintf(trace,"%lx\t\t%lx\t\t%lld\n", address, cache_address.tag,  cache_address.index);
        return cache_address;
    }

    // Calculates the dirty blocks in the cache at then of the simulation
    void DirtyBlocks(){

        for (int i=0; i<numSets; i++){
            if(Sets[i].dirty){
                dirty_blocks++;
            }
        }


    }
   
};

class NwaySetAssociative {
public:
    struct Way {
        bool dirty;
        bool valid;
        UINT64  tag;
    };

    struct Set {
        Way* ways;
        int* lru;
        int* lru_hits;  //Track for each cache hit the position of the block in the LRU stack. MRU, MRU-1 ... LRU
    };

    Set createSet() {
        Set set;
        set.ways = new Way[numWays];
        set.lru = new int[numWays];
        set.lru_hits = new int[numWays];
        return set;
    }

private:
    // Cache Information
    int numSets;
    int cacheSize;
    int blockSize;
    int numWays;
    Set* Sets;


    // Cache Statistics
    INT64 accesses ;
    INT64 misses ;
    INT64 hits_clean ;
    INT64 hits_dirty ;
    INT64 replacements_clean ;
    INT64 replacements_dirty ;
    INT64 writebacks ;
    INT64 dirty_blocks ;   


public:
    // Constructor
    NwaySetAssociative(int cacheSize, int blockSize, int ways){
        this->numWays = ways;
        this->numSets = cacheSize / (blockSize * ways);
        this->cacheSize = cacheSize;
        this->blockSize = blockSize;
    
        this->Sets = new Set[numSets];
        for (int i = 0; i < numSets; ++i) {
            this->Sets[i] = createSet();
        }
        
        Initialize();

        accesses = 0;
        misses = 0;
        hits_clean = 0;
        hits_dirty = 0;
        replacements_clean = 0;
        replacements_dirty = 0;
        writebacks = 0;
        dirty_blocks = 0;   
    }

    // Used for debuging. Prints all sets 
    void Print_Debug(){

        char buffer[15];

        fprintf(trace,"%-10s", "Index");
        for (int i = 0; i < numWays; i++) {
            sprintf(buffer, "Way-%d", i);
            fprintf(trace,"%-15s", buffer);    
        }
        fprintf(trace,"\n");


            for (int i=0; i<numSets; i++){
                fprintf(trace,"%-10d", i);
                for(int j=0; j<numWays; j++){
                    fprintf(trace,"0x%-13lx", Sets[i].ways[j].tag);
                }
                fprintf(trace,"\n");
            }
    }

    void Print(){
        for (int i=0; i<numSets; i++){
            fprintf(trace,"%d ", i);
            for(int j=0; j<numWays; j++){
                fprintf(trace,"%d,%d,%lx ", Sets[i].ways[j].valid ? 1:0 , Sets[i].ways[j].dirty ? 1:0 , Sets[i].ways[j].tag);
            }
            fprintf(trace,"\n");
        }
    }



    // Write operation function
    void Write(UINT64 address) {
       accesses++;
        // Call the helper function to derive the tag, index
        CacheAddress cache_address = GetCacheAddress(address);

        bool Hit = false;
        int found_way = -1;
        // Check in each way if the block is present
        for (int way=0; way<numWays; way++){

            if (Sets[cache_address.index].ways[way].tag == cache_address.tag && Sets[cache_address.index].ways[way].valid){ //Hit
                    found_way = way;
                    Hit = true;
                    if (Sets[cache_address.index].ways[way].dirty){
                        hits_dirty++;
                    }
                    else{
                        hits_clean++;
                        Sets[cache_address.index].ways[way].dirty = true;  
                    }
                
            }
        } 
        


        if(!Hit){ //The address was not found in an of the cache ways. Miss
            misses++;
            // Find the way that the block will be stored
            int victim_way = get_victim(cache_address);

            // Write the block in that way
            if(Sets[cache_address.index].ways[victim_way].valid){
                
                if(Sets[cache_address.index].ways[victim_way].dirty){
                    writebacks++;
                    replacements_dirty++;
                }else{
                    replacements_clean++;
                }
                Sets[cache_address.index].ways[victim_way].tag = cache_address.tag;
                Sets[cache_address.index].ways[victim_way].dirty = false;
                Sets[cache_address.index].ways[victim_way].valid = true; 

            }

            // Write-alocate , new block is in the MRU position
            Update_Policy(victim_way, cache_address);
        }else{
            // Block Hit now is in the MRU position
            Update_Policy(found_way, cache_address);
            Track_Hits(found_way,cache_address);
        }

        
        
    }

    // // Write operation function
    void Read(UINT64 address) {
        accesses++;
        // Call the helper function to derive the tag, index
        CacheAddress cache_address = GetCacheAddress(address);

        bool Hit = false;
        int found_way = -1;
        // Check in each way if the block is precent
        for (int way=0; way<numWays; way++){

            if (Sets[cache_address.index].ways[way].tag == cache_address.tag && Sets[cache_address.index].ways[way].valid){ //Hit
                    found_way = way;
                    Hit = true;
                    if (Sets[cache_address.index].ways[way].dirty){
                        hits_dirty++;
                    }
                    else{
                        hits_clean++;
                    }
            }
        } 
        


        if(!Hit){ //The address does was not found in an of the cache ways. Miss
            misses++;
            // Find the way that the block will be stored
            int victim_way = get_victim(cache_address);

            // Write the block in that way and update replacement policy
            
            if(Sets[cache_address.index].ways[victim_way].valid){
                
                if(Sets[cache_address.index].ways[victim_way].dirty){
                    writebacks++;
                    replacements_dirty++;
                }else{
                    replacements_clean++;
                }
                Sets[cache_address.index].ways[victim_way].tag = cache_address.tag;
                Sets[cache_address.index].ways[victim_way].dirty = false;
                Sets[cache_address.index].ways[victim_way].valid = true; 

            }else{
                Sets[cache_address.index].ways[victim_way].tag = cache_address.tag;
                Sets[cache_address.index].ways[victim_way].dirty = false;
                Sets[cache_address.index].ways[victim_way].valid = true;

            }

            Update_Policy(victim_way, cache_address);
        }else{
            Track_Hits(found_way,cache_address);
        }
    }

    void PrintStats_PKI(INT64 num_instructions){
        fprintf(trace,"Accesses per kilo Instructions: %ld\n", ((accesses/num_instructions) * 1000) );
        fprintf(trace,"Misses per kilo Instructions: %ld\n", ((misses/num_instructions) * 1000));
        fprintf(trace,"Hits per kilo Instructions: %ld\n",  (((hits_clean + hits_dirty)/num_instructions) * 1000));
        fprintf(trace,"Replacements per kilo Instructions: %ld\n",  (((replacements_clean + replacements_dirty)/num_instructions) * 1000));
        fprintf(trace,"Writebacks per kilo Instructions: %ld\n",  ((writebacks/num_instructions) * 1000));
    }
    
    void PrintStats(){

        DirtyBlocks();
        fprintf(trace,"Total Cache Accesses: %ld\n", accesses);
        fprintf(trace,"Total Number of Cache Misses: %ld\n", misses);
        fprintf(trace,"Total Number of Cache Hits: %ld\n", (hits_clean + hits_dirty));
        fprintf(trace,"Total Number of Cache Replacements: %ld\n", (replacements_clean + replacements_dirty));
        fprintf(trace,"Total Number of Cache Writebacks: %ld\n", writebacks);
        fprintf(trace,"Number of dirty blocks: %ld\n", dirty_blocks); 

    }

    void Print_LRU_Hits(){
        for (int i=0; i<numSets; i++){
            fprintf(trace,"%d ", i);
            for(int j=0; j<numWays; j++){
                fprintf(trace,"%d ", Sets[i].lru_hits[j]);
            }
            fprintf(trace,"\n");
        }
    }

private:

    // Initializer
    void Initialize(){
        for (int i=0; i<numSets; i++){
            for(int j=0; j<numWays; j++){
                Sets[i].ways[j].dirty = false;
                Sets[i].ways[j].valid = false;
                Sets[i].ways[j].tag = 0;
                Sets[i].lru[j] = j;
                Sets[i].lru_hits[j] = 0;
            }
        }

    }

    // Track what blocks in the LRU stack are getting Hits
    void Track_Hits(int way, CacheAddress cache_address){
        for (int i=0; i<numWays; i++){
           if(i == way){
                int lru_stack_position = Sets[cache_address.index].lru[i];
                Sets[cache_address.index].lru_hits[lru_stack_position]++;
            }
        }
    }


    // Update the LRU table after a new block was added
    void Update_Policy(int way, CacheAddress cache_address){
        // fprintf(trace,"%d\t%d\t%d\t%d\n",Sets[cache_address.index].lru[0], Sets[cache_address.index].lru[1], Sets[cache_address.index].lru[2], Sets[cache_address.index].lru[3]);
        // If block is already in the MRU position then no need to update the policy
        if(Sets[cache_address.index].lru[way]!=3){
            for (int i=0; i<numWays; i++){
                if(i == way){
                    Sets[cache_address.index].lru[i] = 3;
                }else{
                    Sets[cache_address.index].lru[i]-=1;
                    
                    // Sanity Check
                    if(Sets[cache_address.index].lru[i] == -1){
                        //fprintf(trace,"%d\t%d\t%d\t%d\n",Sets[cache_address.index].lru[0], Sets[cache_address.index].lru[1], Sets[cache_address.index].lru[2], Sets[cache_address.index].lru[3]);
                        fprintf(trace,"There is way with LRU value of -1\n");
                    }
                }
            }
        }
        // fprintf(trace,"%d\t%d\t%d\t%d\n",Sets[cache_address.index].lru[0], Sets[cache_address.index].lru[1], Sets[cache_address.index].lru[2], Sets[cache_address.index].lru[3]);
    }
    

    // Search through all ways and find the least recently used. The LRU has value 0 and MRU has value 3
    int get_victim(CacheAddress cache_address){

        for (int i=0; i< numWays; i++){
            if(Sets[cache_address.index].lru[i] == 0){
                return i;
            }
        }
        //Something Went wrong
        fprintf(trace,"Something Went wrong when finding victim way\n");
        return 0;
    }


    // Helper function to derive the tag, index fields from an address
    CacheAddress GetCacheAddress(UINT64 address) {
        CacheAddress cache_address;

        // Calculate the index and tag values
        UINT64 index_bits = log2(numSets);
        UINT64 offset_bits = log2(blockSize);
        // UINT64 tag_bits = 64 - index_bits - offset_bits;

        UINT64 index_mask = ((1 << index_bits) - 1);
        
        cache_address.index = (address >> offset_bits) & index_mask;

        cache_address.tag  = address >> (index_bits + offset_bits);

        //fprintf(trace,"%lx\t\t%lx\t\t%lld\n", address, cache_address.tag,  cache_address.index);
        return cache_address;
    }

    // Calculates the dirty blocks in the cache at then of the simulation
    void DirtyBlocks(){
        for (int i=0; i<numSets; i++)
            for(int j=0; j<numWays; j++){
            if(Sets[i].ways[j].dirty){
                dirty_blocks++;
            }
        }

    }
   
};


DirectMapped D1(64*1024, 64); 
//NwaySetAssociative D1(16*1024, 64, 2);


// Print a memory read record
VOID RecordMemRead(VOID * ip, VOID * addr)
{
    // fprintf(trace,"%p: R %p\n", ip, addr);
    D1.Read(UINT64(addr));
}

// Print a memory write record
VOID RecordMemWrite(VOID * ip, VOID * addr)
{
    // fprintf(trace,"%p: W %p\n", ip, addr);
    D1.Write(UINT64(addr));
}

// Is called for every instruction and instruments reads and writes
VOID Instruction(INS ins, VOID *v)
{

    // Instruments memory accesses using a predicated call, i.e.
    // the instrumentation is called iff the instruction will actually be executed.
    //
    // On the IA-32 and Intel(R) 64 architectures conditional moves and REP 
    // prefixed instructions appear as predicated instructions in Pin.
    UINT32 memOperands = INS_MemoryOperandCount(ins);

    // Iterate over each memory operand of the instruction.
    for (UINT32 memOp = 0; memOp < memOperands; memOp++)
    {
        if (INS_MemoryOperandIsRead(ins, memOp))
        {
            INS_InsertPredicatedCall(
                ins, IPOINT_BEFORE, (AFUNPTR)RecordMemRead,
                IARG_INST_PTR,
                IARG_MEMORYOP_EA, memOp,
                IARG_END);
        }
        // Note that in some architectures a single memory operand can be 
        // both read and written (for instance incl (%eax) on IA-32)
        // In that case we instrument it once for read and once for write.
        if (INS_MemoryOperandIsWritten(ins, memOp))
        {
            INS_InsertPredicatedCall(
                ins, IPOINT_BEFORE, (AFUNPTR)RecordMemWrite,
                IARG_INST_PTR,
                IARG_MEMORYOP_EA, memOp,
                IARG_END);
        }
    }

    
}

VOID Fini(INT32 code, VOID *v)
{   
    D1.PrintStats();
    D1.Print_Debug();
    D1.PrintStats_PKI(2982677948932);
    D1.Print();
    D1.Print_LRU_Hits();
    fprintf(trace, "#eof\n");
    fclose(trace);
}

/* ===================================================================== */
/* Print Help Message                                                    */
/* ===================================================================== */
   
INT32 Usage()
{
    PIN_ERROR( "This Pintool prints a trace of memory addresses\n" 
              + KNOB_BASE::StringKnobSummary() + "\n");
    return -1;
}

/* ===================================================================== */
/* Main                                                                  */
/* ===================================================================== */

int main(int argc, char *argv[])
{
    if (PIN_Init(argc, argv)) return Usage();

    trace = fopen("direct_mapped_64KB_simulation.out", "w");

    INS_AddInstrumentFunction(Instruction, 0);
    PIN_AddFiniFunction(Fini, 0);

    // Never returns
    PIN_StartProgram();
    
    return 0;
}
