#include "student.h"
#include <stdio.h>
#include <stdlib.h>

int main (void) {     
    int n;     
    const char filename[] = "Students.txt"; 

    //Data Ingestion (User Input)
    printf("Enter the number of students to be stored in the record: \n");     
    if (scanf(" %d", &n) != 1 || n <= 0) {         
        fprintf(stderr, "Invalid input!\n");  
        return -1;     
    }     

    Student_t *s_ptr = (Student_t *)malloc(n * sizeof(Student_t));     
    if (!s_ptr) {         
        fprintf(stderr, "Memory allocation failed!\n");  
        return -1;     
    }  

    if (getData(s_ptr, n) != 0) {         
        fprintf(stderr, "Error while getting student data.\n");     
        free(s_ptr);         
        return -1;     
    }     

    if (writeData(s_ptr, n, filename) != 0) {         
        fprintf(stderr, "Error while writing data into file!\n");    
        free(s_ptr);         
        return -1;     
    }     

    //Cleanup the input buffer
    free(s_ptr);     
    s_ptr = NULL;   

    // ---------------------------------------------------------
    // PHASE 2: HYDRATION & HASHING
    // ---------------------------------------------------------
    
    //loading Data from Disk
    Pair_t file_data = readData(filename);     
    if (file_data.count <= 0 || !file_data.ptr) {         
        fprintf(stderr, "Error or No Data in file!\n"); 
        if (file_data.ptr) free(file_data.ptr);
        return -1;     
    }     

    //initializing Hash Table
    Hashtable_t* ht = hashtable_init();
    if (!ht) {
        fprintf(stderr, "Failed to initialize hash table!\n");
        free(file_data.ptr);
        return -1;
    }

    //populating Hash Table (Bulk Insert)
    if (insert_to_hash(file_data.ptr, file_data.count, ht) != 0) {
        fprintf(stderr, "Warning: Partial or failed hash insertion.\n");
    } else {
        printf("System Hydrated: %d records indexed successfully.\n", file_data.count);
    }

    //verification (Display)
    if (displayData(file_data.ptr, file_data.count) != 0) {         
        fprintf(stderr, "Error printing data!\n");        
        // Continue to cleanup...
    }     

    // ---------------------------------------------------------
    // CLEANUP
    // ---------------------------------------------------------
    
    //freeing Hash Table Nodes & Buckets (Simple implementation)
    //freeing the structure.
    for (int i = 0; i < ht->size; i++) {
        HashNode_t* current = ht->p_buckets[i];
        while (current != NULL) {
            HashNode_t* temp = current;
            current = current->p_next;
            free(temp);
        }
    }
    free(ht->p_buckets);
    free(ht);

    //freeing the Master Data Array
    free(file_data.ptr);     
    file_data.ptr = NULL;     

    return 0; 
}