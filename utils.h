#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>

//Enum for more robust error handling:
typedef enum {      
    STATUS_OK = 0,
    
    //Enumerators for memory & pointer errors:
    ERR_ALLOCATION_FAILED,
    ERR_NULL_POINTER,       //for when checks like if(!db) fail
    
    //Enumerators for logic/data errors
    ERR_INVALID_INPUT,      //bad scanf, or n <= 0
    ERR_NOT_FOUND,          //ID not in DB
    ERR_DUPLICATE_ID,       //ID already exists
    ERR_DB_EMPTY,           //for display/sort operations
    
    //Enumerators for file I/O errors
    ERR_FILE_OPEN_FAILED,
    ERR_FILE_READ_FAILED,
    ERR_FILE_WRITE_FAILED,

    ERR_INVALID_INT_ARG
} Status_e;

//Function prototypes:
//Resuable function to clear input buffer for bad input:
void clear_input_buffer(void);

//Function to print error messages recieved from functions in string form:
const char* get_status_msg(Status_e status);

#endif