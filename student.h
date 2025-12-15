#ifndef STUDENT_H
#define STUDENT_H

typedef struct {
    int id;
    char name[50];
    float gpa;
} Student_t;

typedef struct {    //Pair_t struct to allow returning of 2 values from the readData function
    int count;      //i.e, the number of records in the persistant file (number of lines)
    Student_t* ptr; //and a pointer to the first byte of memory malloc'd by the readData function, now containing all the data read from the files.
} Pair_t;

//prototypes for basic functions:
int getData(Student_t* s_ptr, int n); 
int writeData(Student_t* s_ptr, int n, const char* filename);
Pair_t readData(const char* filename);
int displayData(const Student_t* s_ptr, int count);

#endif
