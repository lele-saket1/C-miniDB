#ifndef STUDENT_H
#define STUDENT_H

#include <stdio.h>

//Forward declaration to deal with dependancy
typedef struct Hashtable Hashtable_t;

typedef struct {
    int id;
    char name[50];
    float gpa;
} Student_t;

typedef struct {    
    int count;      
    int capacity;   
    Student_t* ptr; 
} StudentDB;

//Function definitions:

//functions to deal with file I/O:
StudentDB readData(const char* filename);
int writeData(Student_t* s_ptr, int n, const char* filename);

//internal logic / helper functions:
int get_fresh_slot(StudentDB* db, int* realloc_flag);
void clear_input_buffer(void);

//UI wrapper functions:
void handle_create_student(StudentDB* db, Hashtable_t* ht);
int displayData(const Student_t* s_ptr, int count);

//helper and wrapper functions for sorting:
int compare_gpa_indirect(const void* a, const void* b);
int handle_sort_and_display(StudentDB* db);

#endif