#include <stdio.h>
#include <stdlib.h>
#include<string.h>
#include "student.h"
#include "hash.h"
#include "utils.h"

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
    StudentDB db;
    clear_StudentDB (&db);

    //1. Booting up the program:
    printf("[BOOT] Loading data from disk...\n");
    
    //Load data into flat array from Disk (Storage):
    Status_e boot_status = readData (filename, &db);
    if (boot_status != STATUS_OK) {
        fprintf(stderr, "[BOOT WARNING] %s\n", get_status_msg(boot_status));

        if (boot_status == ERR_ALLOCATION_FAILED) {
            fprintf(stderr, "[FATAL] System cannot run without memory.\n");

            return -1;
        }
    }
    
    //Safety check: Ensure that pointer is valid!
    if (!db.ptr) {
        printf("[BOOT WARNING] No existing data found or allocation error. Starting fresh.\n");
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
            Status_e hydration_status = insert_to_hash((db.ptr + i), 1, ht);

            //Check if hashnode insertion was successful:
            if (hydration_status != STATUS_OK) {
                //if not successful, print error message and Cleanup everything that has been allocated so far:
                fprintf(stderr, "[FATAL] Hash table hydration failed. System state inconsistent.\n");
                DBcleanup(&ht, &db);    
                return -1;
            }

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
                Status_e view_result =  displayData(db.ptr, db.capacity);
                if (view_result != STATUS_OK) {
                    fprintf(stderr, "[FAIL] View operation failed.  %s\n", get_status_msg(view_result));
                    continue;
                }
                break;

            case 2: //CREATE operation: adds new student record to student array occupying tombstones or invoking vector growth if neccessary
                Status_e create_status =  handle_create_student(&db, ht);
                if (create_status != STATUS_OK) {
                    fprintf(stderr, "[FAIL] Create operation failed. %s\n", get_status_msg(create_status));
                    continue;
                }
                break;

            case 3: //SEARCH operation: uses hash table's O(1) lookups
                printf("Enter Student ID to search: ");
                if (scanf("%d", &id_input) == 1) {
                    search_result = search_student(ht, id_input);
                    if (search_result) {
                        printf("\n[FOUND] ID: %d | Name: %s | GPA: %.2f\n", 
                               search_result->id, search_result->name, search_result->gpa);
                    } else {
                        printf("\n[FAIL] Student with ID %d not found.\n", id_input);
                        continue;
                    }
                }
                clear_input_buffer();
                break;

            case 4: //UPDATE operation: uses search function to find student with particular id in the hashtable and modifies the struct directly
                printf("Enter Student ID to update: ");     //hence, the student array gets automatically updated
                if (scanf("%d", &id_input) == 1) {
                    clear_input_buffer(); //clear buffer before update_student calls fgets

                    Status_e update_status = update_student(ht, id_input);
                    if (update_status == STATUS_OK) {
                        printf("[SUCCESS] Record updated.\n");
                    } else {
                        fprintf(stderr, "[FAIL] Update operation failed. %s\n", get_status_msg(update_status));
                        continue;
                    }
                } else {
                    clear_input_buffer();
                }
                break;

            case 5: //DELETE operation: deletes node from hashtable after searching for it. it marks a tombstone in the struct in the student array
                printf("Enter Student ID to delete: ");
                if (scanf("%d", &id_input) == 1) {
                    Status_e delete_status = delete_student_from_hash(ht, id_input, &db);
                    if (delete_status == STATUS_OK) {
                        printf("[SUCCESS] Student with ID %d deleted (Tombstoned).\n", id_input);
                    } else {
                        fprintf(stderr, "[FAIL] Delete operation failed. %s\n", get_status_msg(delete_status));
                        continue;
                    }
                }
                clear_input_buffer();
                break;

            case 6: //SORT & DISPLAY operation: sorts internal array of pointers to the pointers of the student structs using qsort, based on gpa
                Status_e sort_status = handle_sort_and_display(&db);   //result is displayed, with all records sorted in secondary array, based on gpa
                if (sort_status == STATUS_OK) {
                    printf("[SUCCESS] Student Records List sorted. \n");
                } else {
                    fprintf(stderr, "[FAIL] Sort operation failed. %s\n", get_status_msg(sort_status));
                }
                break;

            case 7: //EXIT operation: flushes contents of student array onto the disk to ensure persistent storage
                printf("[SHUTDOWN] Saving changes to disk...\n");
                Status_e write_status = writeData(db.ptr, db.count, filename);
                if (write_status == STATUS_OK) {
                    printf("[SHUTDOWN] Data persisted successfully.\n");
                } else {
                    fprintf(stderr, "[ERROR] Failed to save data! %s\n", get_status_msg(write_status));
                    continue;
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