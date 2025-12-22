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

        new_node->p_student = (s_ptr + i);      //assigning student record to insert into the hash table, to the node
        
        new_node->p_next = *(ht->p_buckets + idx);      //inserting new_node at the head
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

    while (ptr) {
        if (ptr->p_student->id == id) {
            return ptr->p_student;
        }
        ptr = ptr->p_next;
    }

    return NULL;
}

int delete_student_from_hash (Hashtable_t* ht, int id) {    //function to delete student record with given id from hash table
    if (!ht) {
        return -1;
    }

    int idx = hash_function(id);
    HashNode_t* current = *(ht->p_buckets + idx);
    HashNode_t* prev = NULL;

    while (current) {
        if (current->p_student->id == id) {
            current->p_student->id = -1;     //marking the tombstone for the original student array
            
            if (prev == NULL && current == *(ht->p_buckets + idx)) {    //if the node with student record with matching id is found:
                (*(ht->p_buckets + idx)) = current->p_next;     //case 1: if node in question is the head
            }
            else {
                prev->p_next = current->p_next;     //case 2: if node in question is in the middle or at the tail
            }

            free(current);    //freeing the node with the student record from the hash table
            return 0;
        }
        prev = current;
        current = current->p_next;
    }

    return -1;
}
