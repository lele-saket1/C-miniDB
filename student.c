#include "student.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//Function definitions for internal logic / helper-functions: 

//function to clear the input-buffer after 'bad' input:
void clear_input_buffer(void) {     
    int c;     
    while ((c = getchar()) != '\n' && c != EOF); 
}

//function to create new student records:
int get_fresh_slot(StudentDB* db, int* realloc_flag) {
    if(!db || !realloc_flag) {
        return -1;
    }

    *realloc_flag = 0;

    int i = 0;
    while (i < db->capacity) {
        if ((db->ptr + i)->id == -1) {

            if (i >= db->count) {
                db->count = i + 1;
            }
            return i;
        }
        i++;
    }
    
    *realloc_flag = 1;
    int new_cap = (db->capacity == 0)? 10 : (db->capacity * 2);
    Student_t* temp_ptr = (Student_t *)realloc(db->ptr, new_cap * sizeof(Student_t));
    if (!temp_ptr) {
        return -1;
    }
    db->ptr = temp_ptr;

    for (int i = db->capacity; i < new_cap; i++) {
        (db->ptr + i)->id = -1;
    }

    db->capacity = new_cap;
    temp_ptr = NULL;

    int index = db->count;
    db->count += 1;
    return index;
}

//Function definitions for file-I/O functions:

//Function to write or O/p data to the text-files:
int writeData (Student_t* s_ptr, int n, const char* filename) {
    //writing in write mode to rewrite the file with updated contents
    FILE* fp = fopen (filename, "w");     
    if (!fp) {         
        fprintf(stderr, "Error opening file in write mode!\n");    

        return -1;     
    }

    for (int i = 0; i < n; i++) {     
        //writing in csv format for ease of parsing and user-readability
        if ((s_ptr + i)->id != -1) {
            fprintf(fp, "%d,%s,%.2f\n", (s_ptr + i)->id, (s_ptr + i)->name, (s_ptr + i)->gpa);     
        }
    }

    fclose(fp);     
    fp = NULL;      

    return 0; 
} 

//Function to read or I/p data from the text files and return the number of records, capacity and pointer to the Student_t struct:
StudentDB readData (const char* filename) {     
    StudentDB ret_struct;     

    FILE* fp = fopen (filename, "r");     
    if (!fp) {         
        fprintf(stderr, "Error opening file in read mode!\n"); 

        ret_struct.count = 0;         
        ret_struct.ptr = NULL;   

        return ret_struct;     
    } 

    ret_struct.count = 0;     
    int c;     
    while ((c = fgetc(fp)) != EOF) {   
        if (c == '\n') {          
            ret_struct.count++;        //counting number of lines (number of existing records):
        }     
    }     

    //We perform a single file pass 
    // to determine the record count (by counting the number newlines'\n') 
    // then use rewind() before parsing the data. 
    // This avoids the inefficiency of opening/closing the file twice, 
    // which would incur significant OS overhead of FAT lookups and releasing file-handles twice

    rewind(fp);     

    int buffer_margin = 10;
    ret_struct.capacity = ret_struct.count + buffer_margin;

    Student_t* temp_ptr = (Student_t *)malloc(ret_struct.capacity * sizeof(Student_t)); 
    
    if (!temp_ptr) {         
        fprintf(stderr, "Error in memory allocation!\n");         
        ret_struct.count = 0;
        ret_struct.capacity = 0; 
        ret_struct.ptr = NULL;                 
        return ret_struct;     
    }

    memset(temp_ptr, 0, ret_struct.capacity * sizeof(Student_t));

    for(int i = ret_struct.count; i < ret_struct.capacity; i++) {
        (temp_ptr + i)->id = -1;    // Tombstone/Empty marker
    }

    ret_struct.ptr = temp_ptr;     
    char line_buffer[100];
     
    for (int i = 0; i < ret_struct.count; i++) {         
        if (fgets(line_buffer, sizeof(line_buffer), fp)) {       
            //using sscanf to parse memory and using the special scanset to deal with white spaces in strings (names)     
            if (sscanf(line_buffer, "%d,%49[^,],%f", &(temp_ptr + i)->id, (temp_ptr + i)->name, &(temp_ptr + i)->gpa) != 3) {                 
                fprintf(stderr, "Warning: Malformed line %d in file. Skipping.\n", i + 1);             
            }         
        }     
    }  

    fclose(fp);     
    fp = NULL; 

    return ret_struct; 
}

//UI wrapper functions:

//Function to display the current State of the Student_t struct array:
int displayData (const Student_t* s_ptr, int count) {     

    if (!s_ptr) {
        fprintf(stderr, "Error! Invalid pointer!\n");

        return -1;     
    }    

    printf("Printing data for %d students: ", count);
    for (int i = 0; i < count; i++) {
        if ((s_ptr + i)->id != -1) {       //check for tombstone condition (id == -1)

            printf("Student %d ID: %d\n", i + 1, (s_ptr + i)->id);     
            printf("Student %d Name: %s\n", i + 1, (s_ptr + i)->name);         
            printf("Student %d GPA: %.2f\n", i + 1, (s_ptr + i)->gpa);

            printf("\n");
        }
    }

    return 0;
}

//UI wrapper function that invokes the 'get_fresh_slot' logic function and deals with the hash table:
void handle_create_student(StudentDB* db, Hashtable_t* ht) {
    if (!db || !ht) return;

    int n;
    printf("How many students do you want to add? ");
    if (scanf(" %d", &n) != 1 || n <= 0) {
        printf("Invalid number.\n");
        clear_input_buffer();
        return;
    }
    clear_input_buffer();

    //Setup Tracking
    int* new_indices = (int*)malloc(n * sizeof(int));
    if (!new_indices) {
        fprintf(stderr, "Memory allocation failed for tracking array.\n");
        return;
    }

    int global_realloc = 0; 
    int success_count = 0;

    //Batch Input Loop
    for (int i = 0; i < n; i++) {
        printf("\n--- Entering Student %d of %d ---\n", i + 1, n);
        
        int local_realloc = 0;
        int idx = get_fresh_slot(db, &local_realloc);
        
        if (idx == -1) {
            fprintf(stderr, "Critical: Database full or allocation error.\n");
            break; 
        }

        if (local_realloc) {
            global_realloc = 1;
        }

        *(new_indices + i) = idx;
        Student_t* s = (db->ptr + idx);
        
        // User Input Block:
        printf("Enter ID: ");
        if (scanf(" %d", &s->id) != 1) {
            printf("Invalid ID. Skipping.\n");
            s->id = -1; 
            clear_input_buffer();
            continue; 
        }
        clear_input_buffer();

        // Safety: Only check for duplicates if pointers are still valid
        if (!global_realloc) {
             if (search_student(ht, s->id) != NULL) {
                 printf("Error: ID %d already exists! Skipping.\n", s->id);
                 s->id = -1;
                 continue;
             }
        } 
        
        printf("Enter Name: ");
        if (!fgets(s->name, sizeof(s->name), stdin)) {
            s->id = -1;
            continue;
        }
        s->name[strcspn(s->name, "\n")] = 0;

        printf("Enter GPA: ");
        if (scanf(" %f", &s->gpa) != 1) {
             printf("Invalid GPA.\n");
             s->id = -1;
             clear_input_buffer();
             continue;
        }
        clear_input_buffer();

        success_count++;
    }

    //The Commit Phase (The Logic Split)
    if (global_realloc) {
        printf("System: Memory expanded. Re-indexing entire database...\n");
        
        // A. Clear the old, broken pointers
        hashtable_clear(ht); 

        // B. Repopulate: Loop through the NEW array and index everyone
        for (int i = 0; i < db->capacity; i++) {
            if ((db->ptr + i)->id != -1) {
                insert_to_hash((db->ptr + i), 1, ht);
            }
        }

    } else {
        // Optimization: Just insert the new records
        printf("System: Integrating %d new records...\n", success_count);
        
        for (int i = 0; i < n; i++) {
            int idx = *(new_indices + i);
            Student_t* s = (db->ptr + idx);
            
            // Only hash if the input was successful (not a tombstone)
            if (s->id != -1) {
                insert_to_hash(s, 1, ht);
            }
        }
    }

    free(new_indices);
    printf("Batch operation complete.\n");
}

//Helper and wrapper functions for sorting:

//Callback function for qsort function:
int compare_gpa_indirect(const void* a, const void* b) {
    Student_t** A = (Student_t **)a;
    Student_t** B = (Student_t **)b;

    if ((*A)->gpa > (*B)->gpa) {
        return -1;
    }
    else if ((*A)->gpa < (*B)->gpa) {
        return 1;
    }
    else {
        return 0;
    }
}

//wrapper function for sorting (invokes qsort):
int handle_sort_and_display(StudentDB* db) {
    if (!db->ptr || !db->count) {
        return -1;
    }

    Student_t* s_p = db->ptr;

    Student_t** idx_arr = (Student_t **)calloc(db->capacity, sizeof(Student_t *));
    if (!idx_arr) {
        return -1;
    }

    int j = 0;
    for (int i = 0; i < db->capacity; i++) {
        if ((s_p + i)->id != -1) {
            *(idx_arr + j) = (s_p + i);
            j++;
        }
    }

    qsort(idx_arr, j, sizeof(Student_t *), compare_gpa_indirect);

    printf("Displaying sorted student records: \n");
    for (int i = 0; i < j; i++) {
        printf("Displaying Data for Student '%d': \n", i + 1);
        printf("Student ID: %d\n", (*(idx_arr + i))->id);
        printf("Student Name: %s\n", (*(idx_arr + i))->name);
        printf("Student GPA: %.2f\n", (*(idx_arr + i))->gpa);
    }

    free(idx_arr);

    return 0;
}