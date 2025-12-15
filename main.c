#include "student.h"
#include<stdio.h>
#include<stdlib.h>


int main (void) {     //main function currently for testing and implementation
    int n;     
    const char filename[] = "Students.txt"; 

    printf("Enter the number of students to be stored in the record: \n");     
    if (scanf(" %d", &n) != 1 || n <= 0) {         
        fprintf(stderr, "Invalid input!\n");  

        return -1;     
    }     

    Student_t *s_ptr = (Student_t *)malloc(n * sizeof(Student_t));     
    if (!s_ptr) {         
        fprintf(stderr, "Memory allocation!\n");  

        return -1;     
    }  

    if (getData(s_ptr, n) != 0) {         
        fprintf(stderr, "Error while getting student data.\n");     

        free(s_ptr);         
        s_ptr = NULL; 

        return -1;     
    }     

    if (writeData(s_ptr, n, filename) != 0) {         
        fprintf(stderr, "Error while writing data into file!\n");    

        free(s_ptr);         
        s_ptr = NULL;  

        return -1;     
    }     

    Pair_t file_data = readData(filename);     
    if (file_data.count <= 0 || !file_data.ptr) {         
        fprintf(stderr, "Error in reading data from the file!\n"); 

        free(file_data.ptr);         

        return -1;     
    }     

    if (displayData(file_data.ptr, file_data.count) != 0) {         
        fprintf(stderr, "Error printing data!\n");        

        return -1;     
    }     

    free(file_data.ptr);     
    file_data.ptr = NULL;     
    free(s_ptr);     
    s_ptr = NULL;   

    return 0; 
}