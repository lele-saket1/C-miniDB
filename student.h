#ifndef STUDENT_H
#define STUDENT_H

//Student_t struct (instance of a student)
typedef struct {
    int id;
    char name[50];
    float gpa;
} Student_t;

//Pair_t struct for dual return values
typedef struct {    
    int count;      //representative of number of records in persistent file
    Student_t* ptr; //pointer pointing to the block of memory on heap, storing student data after reading
} Pair_t;

//Prototypes for Basic Functions
int getData(Student_t* s_ptr, int n); 

int writeData(Student_t* s_ptr, int n, const char* filename);

Pair_t readData(const char* filename);

int displayData(const Student_t* s_ptr, int count);

void clear_input_buffer(void);

int compare_gpa(const void* a, const void* b);

Student_t* sort(const Student_t* s_ptr, int count);

int (*cmp_ptr) (const void*, const void*) = &compare_gpa;

int display_topper (Student_t* s_ptr);

#endif