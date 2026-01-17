#ifndef STUDENT_H
#define STUDENT_H

#include <stdio.h>
#include "utils.h"

//Forward declaration to deal with dependancy
typedef struct Hashtable Hashtable_t;

//Student_t struct to represent a student record in the database:
typedef struct {
    int id;
    char name[50];
    float gpa;
} Student_t;

//StudentDB struct to manage flat array of student structs on the heap:
typedef struct {    
    int count;          //Tracks highest index used + 1, or initial file records.
    int capacity;       //current capacity of struct (including tombstones)
    Student_t* ptr;     //pointer to the student struct on the heap
    int active_records_count; //Number of non-tombstoned student records.
} StudentDB;

//Function definitions:


//functions to deal with file I/O:

//Function to calculate number of records in file and to read data from file into Flat Array:
Status_e readData(const char* filename, StudentDB* out_db);
//Function to write data from Flat array into file in CSV format:
Status_e writeData(Student_t* s_ptr, int n, const char* filename);


//internal logic / helper functions:
//function to create new student records and first fresh index:
int get_fresh_slot(StudentDB* db, int* realloc_flag);
//function to clear out struct of type StudentDB:
void clear_StudentDB(StudentDB* db);

//UI wrapper functions:

//Function to implement geometric growth algorithm and employs 'get_fresh_slot' function:
Status_e handle_create_student(StudentDB* db, Hashtable_t* ht);
//'View' functionality function to display all student records as in RAM:
Status_e displayData(Student_t* s_ptr, int count);


//helper and wrapper functions for sorting:

//helper function/callback function for qsort:
int compare_gpa_indirect(const void* a, const void* b);
//UI/wrapper function to call qsort and handle display:
Status_e handle_sort_and_display(StudentDB* db);

//Global cleanup function:
Status_e DBcleanup (Hashtable_t **ht, StudentDB *db);

#endif