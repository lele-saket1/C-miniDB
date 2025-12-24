# C-miniDB: A Student Record Manager

## Overview

This project is a command-line student database manager built from scratch in C. My primary motivation is to develop a deep, practical understanding of low-level C programming, focusing specifically on robust file I/O, disciplined dynamic memory management, and implementing data structures from first principles.

## Version History

### Commit 5: The Architecture & Integrity Update (Current)

* **Referential Integrity (Indirect Sorting):** Completely overhauled the sorting engine. Instead of sorting the main data array (which would shift memory addresses and invalidate Hash Table pointers), the system now creates and sorts an auxiliary array of pointers (`Student_t**`). This allows for ordered viewing while keeping the physical Heap Array and Hash Table consistent.
* **Data Encapsulation:** Refactored the codebase to replace the temporary `Pair_t` struct with a robust `StudentDB` context structure. This centralizes the management of the data pointer, record count, and capacity.
* **Separation of Concerns (Create):** Split the student creation process into two distinct layers: `get_fresh_slot` (Logic/Memory Allocation) and `handle_create_student` (UI/Input Validation).
* **Update Operation:** Implemented the logic and UI wrappers for modifying existing records (`update_student`), allowing changes to Name and GPA while preserving the immutable ID.

### Commit 4: The Deletion Update

* **Hybrid Deletion Logic:** Implemented a robust deletion mechanism (`delete_student_from_hash`) that physically removes nodes from the Hash Table linked lists to free memory immediately.
* **Tombstone Strategy:** Adopted a "soft delete" approach for the master data array by marking deleted student IDs as `-1` (tombstones). This preserves array integrity without requiring expensive memory shifting during runtime.
* **Stability Fixes:** Resolved linker errors by reorganizing global variable definitions and fixed potential infinite loops in traversal functions.

### Commit 3: The Search & Sort Update

* **High-Performance Search:** Implemented a targeted lookup function using the hash table index to retrieve student records by ID.
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

### 1. Preserving Referential Integrity (Indirect Sorting)
The most critical architectural challenge was maintaining consistency between the Hash Table and the Heap Array. 
* **The Problem:** Standard sorting of the main `Student_t` array would physically move memory blocks. This would immediately invalidate every pointer held by the Hash Table nodes, causing dangling pointers and undefined behavior.
* **The Solution:** I implemented an **Indirect Sort** strategy. The `qsort` function operates on a temporary array of *pointers* (`Student_t**`) rather than the data itself. This allows the system to present a sorted "view" to the user while keeping the physical memory layout immutable, ensuring the Hash Table remains valid without needing a costly rebuild.

### 2. Deterministic Memory Allocation (Two-Pass Loading)
To prevent heap fragmentation during file loading, I rejected the common "realloc-as-you-go" approach.
* **The Logic:** `realloc` is an expensive operation that often requires the OS to find a new contiguous memory block and `memcpy` the old data.
* **The Implementation:** The `readData` function performs a fast initial pass over the file solely to count newlines. This allows the program to perform a single, exact `malloc` for the entire dataset before parsing. While this incurs a second I/O pass, it vastly improves memory stability and reduces allocator overhead.

### 3. Amortized O(1) Growth Strategy (Vector Semantics)
When new records exceed current capacity, the system does not grow linearly (e.g., adding 10 slots).
* **The Decision:** I implemented a **Geometric Growth** strategy (doubling capacity: `new_cap = capacity * 2`).
* **The Benefit:** This ensures that the amortized time complexity of inserting a new record remains **O(1)**. While a doubling event is expensive (O(N)), it happens with decreasing frequency as the dataset grows, preventing the "performance cliff" associated with linear resizing.

### 4. Hybrid Storage Architecture
The system decouples **storage** from **indexing**, mimicking the design of production-grade database engines.
* **Heap Array (Storage):** Data is stored in a contiguous array to maximize **spatial locality**, ensuring CPU cache hits during linear operations like file I/O or iteration.
* **Hash Table (Index):** A separate chaining hash table provides **O(1) average-case lookup**. Critically, the hash nodes store *pointers* to the heap array rather than duplicating data, minimizing the memory footprint.

### 5. Inversion of Control via Callbacks
Rather than writing a custom sorting algorithm, I utilized the standard C library's `qsort`.
* **The Engineering Principle:** This demonstrates **Inversion of Control**. I provide the logic (the `compare_gpa_indirect` callback) while delegating the execution to the highly optimized system library. This makes the code more maintainable and robust against edge cases in partitioning logic.

### 6. The "Tombstone" Deletion Pattern
Physical deletion in an array is an O(N) operation requiring the shifting of all subsequent elements.
* **The Approach:** I adopted a **Soft Delete** strategy. Deleted records are marked with a sentinel value (`id = -1`).
* **The Trade-off:** This sacrifices a small amount of memory for significant performance gains, as deletion becomes an O(1) operation. These "tombstones" can be cleaned up during a future "compaction" phase or ignored during file persistence.


## Future Roadmap

1. **Hash Table Lifecycle:** Implement `hashtable_clear` (destructor) and internal reset functions to handle state changes (like array re-allocation) cleanly.
2. **Interactive Menu Loop:** Replace the linear execution flow with a persistent `while(1)` loop handling user commands (CREATE, READ, UPDATE, DELETE, SORT, EXIT).
3. **Final System Testing:** Perform comprehensive validation of all CRUD operations and memory states.

## Build and Run

To compile all source files and create an executable:
```sh
gcc -o mini_db main.c student.c hash.c -Wall -Wextra -g