#include "utils.h"
#include<stdio.h>
#include<string.h>
#include<stdlib.h>

//function to clear the input-buffer after 'bad' input:
void clear_input_buffer (void) {     
    int c;     
    while ((c = getchar()) != '\n' && c != EOF); 
}

//function to convert error enumerators to strings:
const char* get_status_msg (Status_e status) {
    //implementing static dispatch instead of traditional switch-case structure to improve efficiency
    static const char* arr[] = {    //made array 'static' to ensure persistence on data segment accross all calls
        "Operation successful.",
        "Err: Failed Allocation!",
        "Err: Null or invalid pointer!",
        "Err: Invalid input!",
        "Err: ID not found!",
        "Err: Duplicate ID!",
        "Err: Empty Database!",
        "Err: File open failed!",
        "Err: File read failed!",
        "Err: File write failed!",
        "Err: Invalid integer argument!"
    };

    size_t num_messages = sizeof(arr) / sizeof(*arr);   

    if (status >= 0 && status < num_messages) {     
        return *(arr + status);     //dispatch message according to index
    } else {    //out of bounds condition:
        return "Err: Unknown status code!";
    }
}