#include "hash.h"
#include <stdio.h>
#include <stdlib.h>

//hash function definition:
int hash_function(int id) {     
    return (id >= 0) ? (id % HASH_SIZE) : (-id % HASH_SIZE);    //returns index corresponding to student id
}


Hashtable_t* hashtable_init(void) {                                 //constructor (initializer for the hash table)
    Hashtable_t* ht = (Hashtable_t*)malloc(sizeof(Hashtable_t));    
    if (!ht) {
        return NULL;                                                
    }

    ht->size = HASH_SIZE;
    ht->p_buckets = (HashNode_t**)calloc(ht->size, sizeof(HashNode_t*));    //allocating array of buckets and making all the buckets point to null using calloc
    
    if (!ht->p_buckets) {

        free(ht);
        ht = NULL;

        return NULL;
    }
    return ht;
}

//Function to hydrate hash table from flat array of structs:
int insert_to_hash(Student_t* s_ptr, int n, Hashtable_t* ht) {
    if (!ht || !s_ptr) {
        return -1;
    }

    for (int i = 0; i < n; i++) {
        int idx = hash_function((s_ptr + i)->id);     //invokes hash function to generate idx corresponding to id of student
        
        HashNode_t* new_node = (HashNode_t*)malloc(sizeof(HashNode_t));    //creating new hashnode
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
    if (!ht || id == -1) {
        return NULL;
    }

    int idx = hash_function(id);    //invokes hash function to obtain idx of bucket corresponding to given ID
    HashNode_t* ptr = *(ht->p_buckets + idx);     //seeks out bucket with that particular idx

    while (ptr) {       //searches in the chain linked to that bucket
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

//Function that updates student record by searching in hash table:
int update_student (Hashtable_t* ht, int id) {
    if (!ht || id == -1) {
        return -1;
    }

    Student_t* s_p = search_student(ht, id);    //invokes search function to find record using hash table
    if (!s_p) {
        return -1;
    }

    printf("Student found: \n");
    int res1 = displayData(s_p, 1);     //displays current data for the student record
    if (res1 == -1) {
        return -1;
    }

    clear_input_buffer();

    printf("Update Student Data: \n");      //updates student data from user input (id is immutable)
    printf("Update Student Name: \n");
    if (!fgets(s_p->name, sizeof(s_p->name), stdin)) {
        return -1;
    }

    (s_p)->name[strcspn((s_p)->name, "\n")] = 0; 

    printf("Update Student GPA: \n");
    if (scanf(" %f", &s_p->gpa) != 1) {
        return -1;
    }

    printf("Updated Data: \n");     //displays updates data for the student record
    int res3 = displayData(s_p, 1);
    if (res3 == -1) {
        return -1;
    }

    clear_input_buffer();

    return 0;
}