#include <stdio.h>
#include <stdlib.h>
#include<string.h>
#include "student.h"
#include "hash.h"

//Helper function for UI display:
void print_menu(void) {
    printf("\nSTUDENT DATABASE ENGINE->\n");
    printf("1. View All Records (Physical Order)\n");
    printf("2. Create New Student(s)\n");
    printf("3. Search by ID\n");
    printf("4. Update Student\n");
    printf("5. Delete Student\n");
    printf("6. Sort and Display (by GPA)\n");
    printf("7. Save & Exit\n");
    printf("\n");
    printf("Select an option: ");
}

int main(void) {
    const char filename[] = "Students.txt";
    int choice;
    int id_input;
    Student_t* search_result = NULL;

    //1. Booting up the program:
    printf("[BOOT] Loading data from disk...\n");
    
    //Load data into flat array from Disk (Storage):
    StudentDB db = readData(filename); 
    
    //Safety check: Ensure that pointer is valid!
    if (!db.ptr) {
        printf("[BOOT] No existing data found or allocation error. Starting fresh.\n");
        //Initialize a fresh DB if readData returned NULL
        db.capacity = 10;
        db.count = 0;
        db.ptr = (Student_t*)calloc(db.capacity, sizeof(Student_t));
        //Initialize tombstones to mark empty or dead records:
        for(int i=0; i<db.capacity; i++) (db.ptr + i)->id = -1;
    }

    //Initializing Index (Hash Table):
    Hashtable_t* ht = hashtable_init();
    if (!ht) {
        fprintf(stderr, "[FATAL] Hash table initialization failed.\n");
        free(db.ptr);
        return -1;
    }

    //Hydrating Index: nodes in chains point to the actual structs in the student array on the heap
    //Only hash valid records (id != -1)
    int valid_records = 0;
    for (int i = 0; i < db.capacity; i++) {
        if ((db.ptr + i)->id != -1) {
            insert_to_hash((db.ptr + i), 1, ht);
            valid_records++;
        }
    }
    printf("[BOOT] System Hydrated. Indexed %d active records.\n", valid_records);


    //2. Interactive loop for basic UI:
    while (1) {
        print_menu();
        if (scanf("%d", &choice) != 1) {
            printf("Invalid input. Please enter a number.\n");
            clear_input_buffer();
            continue;
        }
        clear_input_buffer(); // Consume the newline after the number

        switch (choice) { //'choice' indicates operation chosen by user
            case 1: //VIEW operation: displays current state of the student array
                displayData(db.ptr, db.capacity);
                break;

            case 2: //CREATE operation: adds new student record to student array occupying tombstones or invoking vector growth if neccessary
                handle_create_student(&db, ht);
                break;

            case 3: //SEARCH operation: uses hash table's O(1) lookups
                printf("Enter Student ID to search: ");
                if (scanf("%d", &id_input) == 1) {
                    search_result = search_student(ht, id_input);
                    if (search_result) {
                        printf("\n[FOUND] ID: %d | Name: %s | GPA: %.2f\n", 
                               search_result->id, search_result->name, search_result->gpa);
                    } else {
                        printf("\n[404] Student with ID %d not found.\n", id_input);
                    }
                }
                clear_input_buffer();
                break;

            case 4: //UPDATE operation: uses search function to find student with particular id in the hashtable and modifies the struct directly
                printf("Enter Student ID to update: ");     //hence, the student array gets automatically updated
                if (scanf("%d", &id_input) == 1) {
                    clear_input_buffer(); // Clear buffer before update_student calls fgets
                    if (update_student(ht, id_input) == 0) {
                        printf("[SUCCESS] Record updated.\n");
                    } else {
                        printf("[FAIL] Could not update (ID not found or invalid input).\n");
                    }
                } else {
                    clear_input_buffer();
                }
                break;

            case 5: //DELETE operation: deletes node from hashtable after searching for it. it marks a tombstone in the struct in the student array
                printf("Enter Student ID to delete: ");
                if (scanf("%d", &id_input) == 1) {
                    if (delete_student_from_hash(ht, id_input) == 0) {
                        printf("[SUCCESS] Student %d deleted (Tombstoned).\n", id_input);
                    } else {
                        printf("[FAIL] ID not found.\n");
                    }
                }
                clear_input_buffer();
                break;

            case 6: //SORT & DISPLAY operation: sorts internal array of pointers to the pointers of the student structs using qsort, based on gpa
                handle_sort_and_display(&db);   //result is displayed, with all records sorted in secondary array, based on gpa
                break;

            case 7: //EXIT operation: flushes contents of student array onto the disk to ensure persistent storage
                printf("[SHUTDOWN] Saving changes to disk...\n");
                if (writeData(db.ptr, db.capacity, filename) == 0) {
                    printf("[SHUTDOWN] Data persisted successfully.\n");
                } else {
                    fprintf(stderr, "[ERROR] Failed to save data!\n");
                }
                
                printf("[SHUTDOWN] Cleaning up memory.\n");
                DBcleanup(&ht, &db);    //cleans up hash table and array of student structs
                
                printf("Goodbye.\n");
                return 0;

            default:
                printf("Err: Unknown command.\n");
        }
    }
}