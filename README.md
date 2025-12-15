# C-miniDB: A Student Record Manager

## Overview

This project is a command-line student database manager built from scratch in C. My primary motivation was to develop a deep, practical understanding of low-level C programming, focusing specifically on robust file I/O and disciplined dynamic memory management. It's an exploration of how foundational design decisions impact performance and efficiency.

## Core Features (Version 1)

This initial version establishes the essential architecture for a reliable data management tool:

*   **Structured Data:** Utilizes a `Student_t` struct for a clean and efficient in-memory representation of student records.
*   **Dynamic Allocation:** Implements `malloc()` to dynamically handle the student record array at runtime based on user input.
*   **Persistent Storage:** Reads from and writes structured data to a flat file (`Students.txt`), ensuring data persistence between sessions.

## Key Engineering Decisions

A core focus of this project was writing efficient, high-performance C code, inspired by principles used in systems and embedded programming.

### 1. Single-Pass File Processing

To minimize OS overhead, the `readData` function is designed to perform all file I/O in a single, efficient pass. Instead of opening the file, counting records, closing it, and then re-opening to read, it:
1.  Opens the file once.
2.  Counts the records by iterating through newlines.
3.  Uses `rewind()` to return to the start of the file.
4.  Parses the data into memory.

This approach avoids the significant overhead of redundant system calls and file handle operations.

### 2. Deterministic Memory Allocation

I intentionally avoided using `realloc()` for building the primary data array. Instead, memory is allocated **precisely once** with `malloc()` after the total number of records is determined. This choice ensures predictable, deterministic performance by sidestepping the potential for memory fragmentation and the hidden costs of data copying that can occur with `realloc()`.

## Future Roadmap

This foundational version is the first step. The next stage is to evolve this into a fully interactive tool by implementing:

*   **Full CRUD Functionality:** Add interactive options to Create, Read, Update, and Delete individual student records.
*   **Enhanced User Interface:** Develop a menu-driven system for a more user-friendly experience.
*   **Search and Sort:** Implement features to search for specific students and sort records by different fields (e.g., ID, name, GPA).

## Build and Run

To compile all source files and create an executable:
```sh
gcc -o mini_db main.c student.c -Wall -Wextra -g
```
