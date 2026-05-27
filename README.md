# C-miniDB VERSION 1:

# C-miniDB: A Student Record Manager

## High-level Overview:

This project is a command-line student database manager built from scratch in C. The fundamental unit for storage is a `Student_t` struct with `id`, `name` and `gpa` fields. It offers multiple options to the user: Viewing the current contents of the database, Adding new records to the database, searching for the records in the database according to `id`, Sorting and viewing all the records according to the `gpa`, Deleting records from the database, Updating records (after searching using `id`), and Saving/Exiting the program.

## Program Design (Data-lifecycle):
It utilizes a CSV-file for cold storage, where it stores these records line-by-line. When the Program boots up, the files contents are read into an in-RAM `Student_t` struct-array. The elements of this array are of course, the `Student_t` structs themselves. this is the 'main array' for the program. An in-memory hashtable is hydrated alongside the array, with a 101 buckets and a chain for each bucket. The Student records are loaded into the Hashtable, using their ID's to determine the bucket index. The hashtable's `Hashnodes` contain pointers to the respective `Student_t` structs in the 'main' array. ***NOTE: The Hashtable is INEXTENSIBLE***

When it comes time for adding records and the 'main' array does not have enough space to accomodate the incoming batch of `Student_t` structs, it utilizes a 'Geometric Growth Algorithm', to where it grows to double its original size. It uses a `realloc` call to achieve this. So, the pointers that the `Hashnode`s hold become (potentially) invalidated. Hence, the entire Hashtable is torn down and then rehydrated, in accordance with the current state of the main struct-array. ***NOTE: I know how inefficient this sounds, because it is. But, hey, this was my first project ever, and it's only version 1. So, I ask that you excuse this transgression.***

For the Sorting functionality, a temporary in-RAM array of pointers pointing to the original array of `Student_t` structs is created. This array is then run through a qsort algorithm to sort in the ascending order. Since it is a 'pointer' array, sorting it does not invalidate the main array and consequently the Hashtable nodes. ***NOTE: I am well aware that this is not in the least bit scalable.***

For deletion, the user needs to enter the `id` of the record that they want to delete from the database. This record is located using the Hashtable. The `id` is marked as '-1' for the `Student_t` struct on the main array. The corresponding `Hashnode` is then taken off the Hashtable. 

For the 'Save and Exit' optionality, the Hashtable is torn down. Then, the Contents of the 'main' array of `Student_t` structs is flushed to the disk (ommitting the tombstones), overwriting the previous contents. Then, the array too, is torn down, before the program finally exits.

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


## VERSION 1: END LOG: THANK YOU!

## Future Roadmap (for Version 2): 

2. Implement testing mechanisms on the particular parameters.
3. Optimize the Hash Table mechanism and implement an extensible 'buckets' array.
4. Implement a more efficient growth algorithm, saving on re-hashing costs.

## Build and Run

To compile all source files and create an executable:
```sh
gcc -o mini_db main.c student.c hash.c -Wall -Wextra -g
