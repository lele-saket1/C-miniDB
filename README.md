# C-miniDB: A Student Record Manager

## Overview

This project is a command-line student database manager built from scratch in C. My primary motivation is to develop a deep, practical understanding of low-level C programming, focusing specifically on robust file I/O, disciplined dynamic memory management, and implementing data structures from first principles.

## Version History

### Commit 2: The Indexing Update (Current)
* **Hash Table Implementation:** Introduced a `Hashtable_t` structure to manage student records in memory.
* **Collision Resolution:** Implemented **Separate Chaining** using linked lists (`HashNode_t`) to handle hash collisions effectively.
* **Bulk Indexing:** Added `insert_to_hash` to "hydrate" the hash table immediately after loading data from the disk, enabling potential $O(1)$ lookups.
* **Memory Safety:** Refined memory management to ensure all dynamically allocated buckets and nodes are properly freed upon exit.

### Commit 1: The Foundation
* **Core Data Structure:** Defined the `Student_t` struct (ID, Name, GPA) to serve as the primary in-memory record format.
* **CSV File Handling:** Implemented `readData` and `writeData` to handle persistence using a comma-separated text format (`Students.txt`). 
* **Input Validation:** Created `getData` with input buffer clearing logic to safely handle user input and prevent buffer overflows.
* **Dynamic Loading:** Engineered the file parser to count records first, allocate exact heap memory, and then populate the array in a single execution flow using `rewind()`.

---

## Core Features

* **Structured Data:** Utilizes a `Student_t` struct for a clean and efficient in-memory representation of student records.
* **Dynamic Allocation:** Implements `malloc()` and `calloc()` to dynamically handle the student record array and hash buckets at runtime.
* **Persistent Storage:** Reads from and writes structured data to a flat file (`Students.txt`), ensuring data persistence between sessions.
* **Fast Indexing:** Uses a custom hash function to map Student IDs to memory addresses, preparing the system for instant search and retrieval.

## Key Engineering Decisions

A core focus of this project is writing efficient, high-performance C code, inspired by principles used in systems and embedded programming.

### 1. Single-Pass File Processing
To minimize OS overhead, the `readData` function is designed to perform all file I/O in a single, efficient pass. It avoids redundant open/close operations by using `rewind()` after counting records, ensuring minimal latency.

### 2. Head-Insertion for Hashing
To optimize the `insert_to_hash` function, I utilized **Head Insertion** for the linked list chains. This guarantees $O(1)$ insertion time regardless of the chain length, avoiding the $O(n)$ penalty of traversing to the tail.

### 3. Deterministic Memory Allocation
I intentionally avoided using `realloc()` for building the primary data array. Instead, memory is allocated **precisely once** with `malloc()` after the total number of records is determined, ensuring predictable performance.

---

## Future Roadmap (The Vision)

The immediate next steps are to transform this backend engine into a fully interactive user application:

1.  **Interactive Menu System:** Implement a `while(1)` loop in `main.c` to handle user commands (INPUT, VIEW, SEARCH, DELETE, EXIT).
2.  **Search ($O(1)$):** Implement `search_student(id)` to retrieve records instantly using the Hash Table.
3.  **Delete:** Implement `delete_student(id)` to remove specific nodes from the hash chains and mark records for deletion.
4.  **Sorting:** Implement `sort_students()` to generate a sorted view (by GPA or Name) using an auxiliary array of pointers.
5.  **State Overwrite:** Update the file saving logic to **overwrite** `Students.txt` with the current hash table state upon exit, ensuring deletions and updates are persisted.

## Build and Run

To compile all source files and create an executable:
```sh
gcc -o mini_db main.c student.c -Wall -Wextra -g