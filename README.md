
# C-miniDB: A Student Record Manager

## Overview

This project is a command-line student database manager built from scratch in C. My primary motivation is to develop a deep, practical understanding of low-level C programming, focusing specifically on robust file I/O, disciplined dynamic memory management, and implementing data structures from first principles.

## Version History

### Commit 4: The Deletion Update (Current)

* **Hybrid Deletion Logic:** Implemented a robust deletion mechanism (`delete_student_from_hash`) that physically removes nodes from the Hash Table linked lists to free memory immediately.
* **Tombstone Strategy:** Adopted a "soft delete" approach for the master data array by marking deleted student IDs as `-1` (tombstones). This preserves array integrity without requiring expensive memory shifting during runtime.
* **Stability Fixes:** Resolved linker errors by reorganizing global variable definitions and fixed potential infinite loops in traversal functions.

### Commit 3: The Search & Sort Update

* **High-Performance Search:** Implemented a targeted  lookup function using the hash table index to retrieve student records by ID.
* **Generic Sorting Engine:** Integrated the C Standard Library's `qsort` to reorder the master record array by GPA.
* **Abstraction via Callbacks:** Leveraged function pointers and custom comparator logic to handle floating-point precision during descending GPA sorts.
* **Defensive Programming:** Hardened search and sort functions with NULL-pointer checks and memory-safety guards.

### Commit 2: The Indexing Update

* **Hash Table Implementation:** Introduced a `Hashtable_t` structure to manage student records in memory.
* **Collision Resolution:** Implemented **Separate Chaining** using linked lists (`HashNode_t`) to handle hash collisions effectively.
* **Bulk Indexing:** Added `insert_to_hash` to "hydrate" the hash table immediately after loading data from disk.

### Commit 1: The Foundation

* **Core Data Structure:** Defined the `Student_t` struct (ID, Name, GPA) to serve as the primary in-memory record format.
* **CSV File Handling:** Implemented `readData` and `writeData` to handle persistence using a comma-separated text format.
* **Dynamic Loading:** Engineered a two-pass file parser to count records and allocate exact heap memory.


## Key Engineering Decisions

### 1. Inversion of Control (Sorting)

Instead of hard-coding a specific sorting algorithm, I utilized the `qsort` library with a custom callback function. This demonstrates an understanding of **Inversion of Control** and interfacing with standard system APIs, allowing the library to handle high-performance partitioning while I define the comparison logic for `Student_t` structs.

### 2.  Indexing via Hashing

The system maintains a primary data array on the heap for persistent storage while building a separate Hash Table of pointers for indexing. This dual-structure approach allows for  search performance without sacrificing the ability to perform linear operations like sorting or filtering on the main array.

### 3. Memory Safety & Destructor Patterns

The project follows a strict "allocation must have a corresponding free" philosophy. The cleanup phase in `main.c` traverses the entire hash structure to free nodes and buckets before releasing the master data array, ensuring zero memory leaks as verified by standard testing practices.


## Future Roadmap

1. **Interactive Menu System:** Implement a `while(1)` loop to handle user commands (INPUT, VIEW, SEARCH, DELETE, EXIT).
2. **State Persistence (Flush):** Update the file saving logic to overwrite `Students.txt` with the current RAM state upon exit, ensuring "tombstone" records are filtered out.
3. **Heap Compaction:** Develop a utility to "defragment" the master array by removing tombstone gaps and reallocating memory to reduce footprint.
4. **Global Destructor:** Implement a centralized cleanup function to handle the orderly deallocation of the Hash Table, linked lists, and master data array, emulating a C++ destructor pattern for the main application context.

## Build and Run

To compile all source files and create an executable:
```sh
gcc -o mini_db main.c student.c -Wall -Wextra -g