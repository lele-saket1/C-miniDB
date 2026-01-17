#include "student.h"
#include "hash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "utils.h"

//Function definitions for internal logic / helper-functions: 

//function to create new student records:
int get_fresh_slot (StudentDB* db, int* realloc_flag) {    //enum is returned through eax or rax as an int (safer than returning an address)
    if(!db || !realloc_flag) {
        return -1;
    }

    //set realloc flag to zero
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
    
    //realloc has been used. set realloc flag to 1:
    *realloc_flag = 1;
    int new_cap = (db->capacity == 0)? 10 : (db->capacity * 2);
    Student_t* temp_ptr = (Student_t *)realloc(db->ptr, new_cap * sizeof(Student_t));
    if (!temp_ptr) {
        return -1;
    }
    db->ptr = temp_ptr;

    //initialize new slots as tombstones (unused):
    for (int i = db->capacity; i < new_cap; i++) {
        (db->ptr + i)->id = -1;
    }

    //update capacity:
    db->capacity = new_cap;
    temp_ptr = NULL;

    int index = db->count;
    db->count += 1;

    //return first 'fresh' index:
    return index;
}

//function to clear-out StudentDb struct:
void clear_StudentDB (StudentDB* db) {
    db->ptr = NULL;
    db->capacity = 0;
    db->count = 0;
    db->active_records_count = 0;
}

//Function definitions for file-I/O functions:

//Function to write or O/p data to the text-files:
Status_e writeData (Student_t* s_ptr, int n, const char* filename) {
    if (!s_ptr) {
        return ERR_NULL_POINTER;
    }
    //writing in write mode to rewrite the file with updated contents
    FILE* fp = fopen (filename, "w");     
    if (!fp) {            

        return ERR_FILE_WRITE_FAILED;  
    }

    for (int i = 0; i < n; i++) {     
        //writing in csv format for ease of parsing and user-readability
        if ((s_ptr + i)->id != -1) {
            fprintf(fp, "%d,%s,%.2f\n", (s_ptr + i)->id, (s_ptr + i)->name, (s_ptr + i)->gpa);     
        }
    }

    fclose(fp);     
    fp = NULL;      

    return STATUS_OK; 
} 

//Function to read or I/p data from the text files and return the number of records, capacity and pointer to the Student_t struct:
Status_e readData (const char* filename, StudentDB* out_db) {     
    if (!out_db) {
        return ERR_NULL_POINTER;
    }

    FILE* fp = fopen (filename, "r");     
    if (!fp) {         

        out_db->count = 0;         
        out_db->ptr = NULL; 

        return ERR_FILE_OPEN_FAILED;  
    } 

    out_db->count = 0;     
    int c;     
    while ((c = fgetc(fp)) != EOF) {   
        if (c == '\n') {          
            out_db->count++;        //counting number of lines (number of existing records):
        }     
    }     

    //We perform a single file pass 
    // to determine the record count (by counting the number newlines'\n') 
    // then use rewind() before parsing the data. 
    // This avoids the inefficiency of opening/closing the file twice, 
    // which would incur significant OS overhead of FAT lookups and releasing file-handles twice

    rewind(fp);     

    int buffer_margin = 10;
    out_db->capacity = out_db->count + buffer_margin;

    Student_t* temp_ptr = (Student_t *)malloc(out_db->capacity * sizeof(Student_t)); 
    
    if (!temp_ptr) {         
        fprintf(stderr, "Error in memory allocation!\n");         
        out_db->count = 0;
        out_db->capacity = 0; 
        out_db->ptr = NULL;   
        fclose(fp);

        return ERR_ALLOCATION_FAILED;    
    }

    memset(temp_ptr, 0, out_db->capacity * sizeof(Student_t));

    for(int i = out_db->count; i < out_db->capacity; i++) {
        (temp_ptr + i)->id = -1;    // Tombstone/Empty marker
    }

    out_db->ptr = temp_ptr;     
    char line_buffer[100];
     
    for (int i = 0; i < out_db->count; i++) {         
        if (fgets(line_buffer, sizeof(line_buffer), fp)) {       
            //using sscanf to parse memory and using the special scanset to deal with white spaces in strings (names)     
            if (sscanf(line_buffer, "%d,%49[^,],%f", &(temp_ptr + i)->id, (temp_ptr + i)->name, &(temp_ptr + i)->gpa) != 3) {                 
                fprintf(stderr, "Warning: Malformed line %d in file. Skipping.\n", i + 1);             
            }         
        }     
    }  

    fclose(fp);     
    fp = NULL; 

    return STATUS_OK; 
}

//UI wrapper functions:

//Function to display the current State of the Student_t struct array:
Status_e displayData (Student_t* s_ptr, int count) {     

    if (!s_ptr) {

        return ERR_NULL_POINTER;
    }    

    if (count <= 0) {

        return ERR_INVALID_INT_ARG;
    }

    printf("Printing data for %d students: \n", count);
    for (int i = 0; i < count; i++) {
        if ((s_ptr + i)->id != -1) {       //check for tombstone condition (id == -1)
            printf("[Student %d: ID: %d | Name: %s | GPA: %.2f]\n", 
                            i + 1, (s_ptr + i)->id, (s_ptr + i)->name, (s_ptr + i)->gpa);
                            
            printf("\n");
        }
    }

    return STATUS_OK;
}

//UI wrapper function that invokes the 'get_fresh_slot' logic function and deals with the hash table:
Status_e handle_create_student(StudentDB* db, Hashtable_t* ht) {
    if (!db || !ht) {
        return ERR_NULL_POINTER;
    }

    int n;
    printf("How many students do you want to add? ");
    if (scanf(" %d", &n) != 1 || n <= 0) {
        printf("Invalid number.\n");
        clear_input_buffer();
        return ERR_INVALID_INPUT;
    }
    clear_input_buffer();

    //set up tracking array for new usable indices returned by 'get_fresh_slot' function:
    int* new_indices = (int*)malloc(n * sizeof(int));
    if (!new_indices) {

        return ERR_ALLOCATION_FAILED;
    }

    int success_count = 0;

    for (int i = 0; i < n; i++) {
        printf("\nEntering Student %d of %d:\n", i + 1, n);
        
        int local_realloc = 0;
        int idx = get_fresh_slot(db, &local_realloc);
        
        if (idx == -1) {
            fprintf(stderr, "Critical: Database full or allocation error.\n");
            break; 
        }

        //if memory moved, update the Hash Table immediately so search_student works for next check:
        if (local_realloc) {
            hashtable_clear(ht); 
            for (int j = 0; j < db->capacity; j++) {
                if ((db->ptr + j)->id != -1) {
                    insert_to_hash((db->ptr + j), 1, ht);
                }
            }
        }

        //update index-tracking array:
        *(new_indices + i) = idx;
        Student_t* s = (db->ptr + idx);
        
        printf("Enter ID: ");
        if (scanf(" %d", &s->id) != 1) {
            printf("Invalid ID. Skipping.\n");
            s->id = -1; 
            clear_input_buffer();
            continue; 
        }
        clear_input_buffer();

        //valid check due to hash table being synced before:
        //searching student to check for duplicates:
        if (search_student(ht, s->id) != NULL) {
            printf("Error: ID %d already exists! Skipping.\n", s->id);
            s->id = -1;
            continue;
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

        //add to hash table immediately for the next duplicate check:
        insert_to_hash(s, 1, ht);
        success_count++;
    }

    free(new_indices);
    printf("[Batch operation complete. %d records added.]\n", success_count);
    return STATUS_OK;
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
Status_e handle_sort_and_display(StudentDB* db) {
    if (!db->ptr) {
        return ERR_NULL_POINTER;
    }

    if (!db->count) {
        return ERR_DB_EMPTY;
    }

    Student_t* s_p = db->ptr;

    //initialize array of pointers pointinf to students structs in main array:
    Student_t** idx_arr = (Student_t **)calloc(db->capacity, sizeof(Student_t *));  
    if (!idx_arr) {
        return ERR_ALLOCATION_FAILED;
    }

    int j = 0;  //set counter variable for idx_arr
    for (int i = 0; i < db->capacity; i++) {
        if ((s_p + i)->id != -1) {
            *(idx_arr + j) = (s_p + i);
            j++;
        }
    }

    //invoke qsort function by passing call-back function
    qsort(idx_arr, j, sizeof(Student_t *), compare_gpa_indirect);   

    printf("Displaying sorted student records: \n");
    for (int i = 0; i < j; i++) {
       printf("[Student %d: ID: %d | Name: %s | GPA: %.2f]\n", 
                    i + 1, (*(idx_arr + i))->id, (*(idx_arr + i))->name, (*(idx_arr + i))->gpa);
    }
    printf("\n");

    free(idx_arr);

    return STATUS_OK;
}

//Global destructor function:
Status_e DBcleanup(Hashtable_t** ht_ref, StudentDB* db) {
    if (!ht_ref || !(*ht_ref) || !db) {
        return ERR_NULL_POINTER;
    }

    //clears and deletes hash table:
    hashtable_clear (*(ht_ref));    //empties hash-table, setting buckets to null
    free((*ht_ref)->p_buckets);     //clears array of buckets
    free(*ht_ref);                  //clears hash-table
    (*ht_ref) = NULL;               //gets rid of dangling pointer

    //clears and deletes student struct array:
    free(db->ptr);    //frees array of student structs;
    db->ptr = NULL;    //gets rid of dangling pointer
    db->capacity = 0;   
    db->count = 0;

    return STATUS_OK;
}