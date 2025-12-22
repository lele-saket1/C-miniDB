#include "student.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//function definitions for basic functions:
void clear_input_buffer(void) {     //function to clear the input-buffer after 'bad' input.
    int c;     
    while ((c = getchar()) != '\n' && c != EOF); 
}

int getData (Student_t* s_ptr, int n) {    //function to fetch data from the user with minimal user-input validation and store it in the provided heap memory 
    for (int i = 0; i < n; i++) {         
        printf("Enter data for student %d:\n", i + 1);         
        printf("Enter student ID: \n");  

        if (scanf(" %d", &(s_ptr + i)->id) != 1) {   

            return -1;         
        }         

        clear_input_buffer();         
        printf("Enter student Name: \n");         
        if (!fgets((s_ptr + i)->name, sizeof((s_ptr + i)->name), stdin)) {      

            return -1;         
        }

        (s_ptr + i)->name[strcspn((s_ptr + i)->name, "\n")] = 0;         

        printf("Enter student GPA: \n");         
        if (scanf(" %f", &(s_ptr + i)->gpa) != 1) { 

            return -1;         
        }

        clear_input_buffer();     
    }  

    return 0; 
}

int writeData (Student_t* s_ptr, int n, const char* filename) {     //function to write data to the text files
    FILE* fp = fopen (filename, "w");     // writing in write mode to rewrite the file with updated contents
    if (!fp) {         
        fprintf(stderr, "Error opening file in write mode!\n");    

        return -1;     
    }

    for (int i = 0; i < n; i++) {     
        if ((s_ptr + i)->id != -1) {
            fprintf(fp, "%d,%s,%.2f\n", (s_ptr + i)->id, (s_ptr + i)->name, (s_ptr + i)->gpa);    //writing in csv format for ease of parsing and user-readability 
        }
    }

    fclose(fp);     
    fp = NULL;      

    return 0; 
} 
    
Pair_t readData (const char* filename) {     //function to count number of lines and parse data from the text files and storing it in malloc'd heap memory
    Pair_t ret_pair;     

    FILE* fp = fopen (filename, "r");     
    if (!fp) {         
        fprintf(stderr, "Error opening file in read mode!\n"); 

        ret_pair.count = 0;         
        ret_pair.ptr = NULL;   

        return ret_pair;     
    } 

    ret_pair.count = 0;     
    int c;     
    while ((c = fgetc(fp)) != EOF) {         //counting number of lines (number of existing records)
        if (c == '\n') {             
            ret_pair.count++;         
        }     
    }     

    //We perform a single file pass 
    // to determine the record count (by counting the number newlines'\n') 
    // then use rewind() before parsing the data. 
    // This avoids the inefficiency of opening/closing the file twice, 
    // which would incur significant OS overhead of FAT lookups and releasing file-handles twice

    rewind(fp);     
                        
    Student_t* temp_ptr = (Student_t *)malloc(ret_pair.count * sizeof(Student_t));     //using malloc after to allocate memory only once after calculating the number of bytes required from the number of records
                                                                                       //thereby, avoiding using realloc, which extremely inefficient memory allocation and potential performance overhead and fragmentation 
    if (!temp_ptr) {         
        fprintf(stderr, "Error in memory allocation!\n");         

        ret_pair.count = 0;         
        ret_pair.ptr = NULL;                 

        return ret_pair;     
    }

    ret_pair.ptr = temp_ptr;     
    char line_buffer[100];     
    for (int i = 0; i < ret_pair.count; i++) {         
        if (fgets(line_buffer, sizeof(line_buffer), fp)) {       
                                                                                        //using sscanf to parse memory and using the special scanset to deal with white spaces in strings (names)     
            if (sscanf(line_buffer, "%d,%49[^,],%f", &(temp_ptr + i)->id, (temp_ptr + i)->name, &(temp_ptr + i)->gpa) != 3) {                 
                fprintf(stderr, "Warning: Malformed line %d in file. Skipping.\n", i + 1);             
            }         
        }     
    }  

    fclose(fp);     
    fp = NULL; 

    return ret_pair; 
}

int displayData (const Student_t* s_ptr, int count) {     //function to display data stored in malloc'd heap memory (minimal formatting)

    if (!s_ptr) {
        fprintf(stderr, "Error! Invalid pointer!\n");

        return -1;     
    }    

    printf("Current state of the file: \n");    //displays the contents of the file if the data is flushed to the file then
    printf("Printing data for %d students: ", count);
    for (int i = 0; i < count; i++) {
        if ((s_ptr + i)->id != -1) {     //check for tombstone condition (id == -1)
            printf("Student %d ID: %d\n", i + 1, (s_ptr + i)->id);     
            printf("Student %d Name: %s\n", i + 1, (s_ptr + i)->name);         
            printf("Student %d GPA: %.2f\n", i + 1, (s_ptr + i)->gpa);

            printf("\n");
        }     
    }

    return 0;
}

int compare_gpa(const void* a, const void* b) {     //callback function for qsort

    Student_t* A = (Student_t *)a;
    Student_t* B = (Student_t *)b;

    if (A->gpa > B->gpa) {
        return -1;
    }
    else if (A->gpa < B->gpa) {
        return 1;
    }
    
    return 0;
}

Student_t* sort(Student_t* s_ptr, int count) {      //function that calls the qsort function
    if (!s_ptr) {
        return NULL;
    }

    qsort(s_ptr, count, sizeof(Student_t), compare_gpa);

    return s_ptr;
}

int display_topper (Student_t* s_ptr, int count) {
    if (!s_ptr) {
        return -1;
    }

    Student_t* s_p = s_ptr;
    while (s_p - s_ptr < count) {
        if (s_p->id != -1) {
            printf("TOPPER:\nName: %s\n", s_p->name);
            printf("ID: %d\n", s_p->id);
            printf("GPA: %.2f\n", s_p->gpa);
            return 0;
        }
        s_p++;
    }
    
    return -1;
}
