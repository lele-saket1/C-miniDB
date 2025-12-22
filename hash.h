#ifndef HASH_H
#define HASH_H

#include "student.h" // Logic dependency

#define HASH_SIZE 101


typedef struct HashNode {    //defining instance of linked-list node:
    Student_t* p_student;    //points to student data in the Heap Array
    struct HashNode* p_next; //(self-referential) points to the next hashnode in the linked list (chain)
} HashNode_t;


typedef struct {              //defining instance of hash table:
    int size;                 //holds the size of the hash table (i.e., the number of buckets in the hash table)
    HashNode_t** p_buckets;   //points to the array of buckets (which point to individual 'head nodes' of the linked-list)
} Hashtable_t;

//Hash Functions:

//function to calculate hash index:
int hash_function(int id);

//function to initialize the hash table:
Hashtable_t* hashtable_init(void);
int insert_to_hash(Student_t* s_ptr, int n, Hashtable_t* ht);

//function for search functionality:
Student_t* search_student(Hashtable_t* ht, int id); 

//function to delete student hash table:
int delete_student_from_hash(Hashtable_t* ht, int id);
#endif