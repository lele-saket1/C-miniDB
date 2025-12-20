#include "hash.h"
#include <stdio.h>
#include <stdlib.h>


int hash_function(int id) {     //hash function definition:
    return (id >= 0) ? (id % HASH_SIZE) : (-id % HASH_SIZE);    //returns index corresponding to student id
}


Hashtable_t* hashtable_init(void) {                                 //constructor (initializer for the hash table)
    Hashtable_t* ht = (Hashtable_t*)malloc(sizeof(Hashtable_t));    
    if (!ht) {
        return NULL;                                                
    }

    ht->size = HASH_SIZE;
    ht->p_buckets = (HashNode_t**)calloc(ht->size, sizeof(HashNode_t*));
    
    if (!ht->p_buckets) {

        free(ht);
        ht = NULL;

        return NULL;
    }
    return ht;
}

int insert_to_hash(Student_t* s_ptr, int n, Hashtable_t* ht) {      //insert from files ->student structs -> hash table
    if (!ht || !s_ptr) {
        return -1;
    }

    for (int i = 0; i < n; i++) {
        int idx = hash_function((s_ptr + i)->id); 
        
        HashNode_t* new_node = (HashNode_t*)malloc(sizeof(HashNode_t));     //will soon group malloc calls
        if (!new_node) {
            return -1;
        }

        new_node->p_student = (s_ptr + i);  
        
        new_node->p_next = *(ht->p_buckets + idx);      //head insertion
        *(ht->p_buckets + idx) = new_node;
    }
    return 0;
}

Student_t* search_student(Hashtable_t* ht, int id) {     //search for students based on id
    if (!ht) {
        return NULL;
    }

    int idx = hash_function(id);
    HashNode_t* ptr = *(ht->p_buckets + idx);

    while (ptr->p_next) {
        if (ptr->p_student->id == id) {
            return ptr->p_student;
        }
        ptr++;
    }

    return NULL;
}
