#ifndef FRASE_CHUNK_H
#define FRASE_CHUNK_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <assert.h>

/*
 * This header defines the core data structures and constants for the FRASE
 * database's chunk management system. It includes definitions for `StudentRecord`,
 * `Chunk`, and `ChunkManager`, along with constants governing their sizes and
 * behavior.
 *
 * Each StudentRecord is designed to be exactly 64 bytes.
 * Each chunk contains a fixed number of records, specifically 1024.
 *
 * This results in the following chunk size:
 *   1024 records * 64 bytes/record = 65536 bytes = 64 KiB
*/

#define FRASE_CHUNK_RECORD_CAPACITY 1024u // The maximum number of StudentRecord objects a single chunk can hold.
#define FRASE_STUDENT_NAME_SIZE     56u   // The fixed size for the student's name field within a StudentRecord.

/*
 * Defines a special identifier used to mark records as deleted.
 * Student IDs must never be equal to this value.
 * A record with an ID matching FRASE_TOMBSTONE_ID is considered logically deleted.
*/
#define FRASE_TOMBSTONE_ID UINT32_MAX

/*
 * StudentRecord
 *
 * Represents a single student entry. This structure defines the physical layout
 * of records stored in memory chunks and persisted to disk.
 *
 * Layout details:
 *   id   : 4 bytes (uint32_t) - Unique student identifier.
 *   gpa  : 4 bytes (float)    - Student's Grade Point Average.
 *   name : 56 bytes (char[])  - Student's name, null-terminated if shorter.
 *
 * Total size: 64 bytes
*/
typedef struct StudentRecord {
    uint32_t id;   // Unique student identifier.
    float    gpa;  // Student's Grade Point Average.
    char     name[FRASE_STUDENT_NAME_SIZE]; // Student's name, null-terminated if shorter than FRASE_STUDENT_NAME_SIZE.
} StudentRecord;

/*
 * Chunk
 *
 * Represents a single fixed-size block of records in memory. Chunks are the
 * fundamental units of storage and retrieval within the database system.
 *
 * Members:
 *   chunk_id   : A logical identifier for this chunk. This ID also dictates
 *                the chunk's position within the backing file on disk.
 *   records    : A pointer to a dynamically allocated array of `StudentRecord`
 *                objects, with a capacity of `FRASE_CHUNK_RECORD_CAPACITY`.
 *   used_slots : The count of slots within this chunk that have ever been
 *                assigned a record. This value does not decrease when records
 *                are deleted, indicating the high-water mark of usage.
 *   tombstones : The number of `used_slots` within this chunk that currently
 *                contain logically deleted (tombstoned) records.
 *   dirty      : A boolean flag indicating whether this chunk has been modified
 *                during the current session. If true, the chunk must be written
 *                back to disk during a save operation.
*/
typedef struct Chunk {
    uint32_t       chunk_id;   // Logical identifier for this chunk, determining its position in the backing file.
    StudentRecord *records;    // Pointer to the array of StudentRecord objects held by this chunk.
    size_t         used_slots; // Count of slots that have ever been assigned a record (high-water mark).
    size_t         tombstones; // Number of logically deleted records within this chunk.
    bool           dirty;      // Flag indicating if the chunk has been modified and needs to be written to disk.
} Chunk;

/*
 * ChunkManager
 *
 * Manages the collection of `Chunk` structures, providing an interface for
 * dynamic allocation and tracking of database chunks.
 *
 * Members:
 *   chunks           : A pointer to a dynamically allocated array of `Chunk`
 *                      structures. This array can expand as more chunks are needed.
 *   chunk_count      : The current number of active chunks managed by this instance.
 *   chunk_capacity   : The total capacity of the `chunks` array. This indicates
 *                      how many `Chunk` structures can be stored before the
 *                      array needs to be reallocated and grown.
 *   total_used_slots : The cumulative sum of `used_slots` across all managed chunks.
 *                      Represents the total number of slots ever assigned.
 *   total_tombstones : The cumulative sum of `tombstones` across all managed chunks.
 *                      Represents the total number of logically deleted records.
*/
typedef struct ChunkManager {
    Chunk  *chunks;           // Dynamic array of Chunk structures.
    size_t  chunk_count;      // Current number of active chunks.
    size_t  chunk_capacity;   // Total capacity of the 'chunks' array.
    size_t  total_used_slots; // Cumulative sum of 'used_slots' across all managed chunks.
    size_t  total_tombstones; // Cumulative sum of 'tombstones' across all managed chunks.
} ChunkManager;

/*
 * Compile-time assertion to ensure `StudentRecord` maintains its exact size.
 * This check will cause a compilation error if the `StudentRecord` structure
 * deviates from its intended 64-byte size, helping to prevent layout issues.
*/
static_assert(sizeof(StudentRecord) == 64, "StudentRecord must be exactly 64 bytes");

#endif