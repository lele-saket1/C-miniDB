#ifndef FRASE_FILE_IO_H
#define FRASE_FILE_IO_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "Chunk.h"

/*
 * This header defines the on-disk file format for the FRASE database,
 * including the superblock, chunk headers, and error codes for file I/O operations.
 *
 * The physical structure of the backing file is organized as follows:
 *
 *   1. FileSuperBlock: The global header for the entire database file.
 *   2. Chunk Section 0:
 *      - FileChunkHeader for chunk 0
 *      - StudentRecord[FRASE_CHUNK_RECORD_CAPACITY] payload for chunk 0
 *   3. Chunk Section 1:
 *      - FileChunkHeader for chunk 1
 *      - StudentRecord[FRASE_CHUNK_RECORD_CAPACITY] payload for chunk 1
 *   4. ... and so on for subsequent chunks.
 *
 * Each chunk's record payload is always written as a full, fixed-size array,
 * regardless of the number of active records within it.
 *
 * Consequently, every on-disk chunk section (header + payload) has a fixed size:
 *   sizeof(FileChunkHeader) + (FRASE_CHUNK_RECORD_CAPACITY * sizeof(StudentRecord))
 *
 * This fixed-size layout allows for direct calculation of the file offset
 * for any given chunk N, enabling efficient random access.
*/

#define FRASE_FILE_MAGIC   0x46524153u // Magic number to identify FRASE database files ('FRAS' in ASCII).
#define FRASE_FILE_VERSION 1u          // Current version of the FRASE file format.

/*
 * FileIoError
 *
 * Enumerates possible error conditions encountered by the file I/O module.
 * This module is responsible for tasks such as opening/closing files,
 * validating the file format, reading data into memory chunks, and
 * writing modified chunks back to persistent storage.
*/
typedef enum FileIoError {
    FILE_IO_OK = 0,                 // No error, operation completed successfully.

    FILE_IO_ERR_NULL_ARGUMENT,      // A required argument was NULL.
    FILE_IO_ERR_OPEN_FAILED,        // Failed to open the specified file.
    FILE_IO_ERR_READ_FAILED,        // Failed to read data from the file.
    FILE_IO_ERR_WRITE_FAILED,       // Failed to write data to the file.
    FILE_IO_ERR_SEEK_FAILED,        // Failed to change the file position.
    FILE_IO_ERR_FLUSH_FAILED,       // Failed to flush buffered data to disk.
    FILE_IO_ERR_CLOSE_FAILED,       // Failed to close the file.

    FILE_IO_ERR_BAD_MAGIC,          // The file's magic number does not match FRASE_FILE_MAGIC.
    FILE_IO_ERR_UNSUPPORTED_VERSION,// The file's version is not supported by this software.
    FILE_IO_ERR_CORRUPT_SUPERBLOCK, // The FileSuperBlock data is invalid or corrupted.
    FILE_IO_ERR_CORRUPT_CHUNK_HEADER,// A FileChunkHeader data is invalid or corrupted.

    FILE_IO_ERR_ALLOCATION_FAILED   // Memory allocation failed.
} FileIoError;

/*
 * FileSuperBlock
 *
 * Represents the global header for the entire FRASE database file.
 * It contains essential metadata about the file's structure and overall state.
 *
 * Members:
 *   magic            : A magic number (`FRASE_FILE_MAGIC`) to confirm the file type.
 *   version          : The file format version (`FRASE_FILE_VERSION`) used by this file.
 *   chunk_count      : The total number of chunk sections currently stored in the file.
 *   total_used_slots : The cumulative sum of `used_slots` across all chunks in the file.
 *   total_tombstones : The cumulative sum of `tombstones` across all chunks in the file.
 *   reserved         : Reserved bytes for future expansion, currently unused in Version 1.
*/
typedef struct FileSuperBlock {
    uint32_t magic;            // Magic number to identify the file as a FRASE database.
    uint32_t version;          // Version of the file format.

    uint64_t chunk_count;      // Number of chunk sections present in the file.
    uint64_t total_used_slots; // Sum of 'used_slots' from all chunks in the file.
    uint64_t total_tombstones; // Sum of 'tombstones' from all chunks in the file.

    uint8_t  reserved[32];     // Padding for future use; unused in current version.
} FileSuperBlock;

/*
 * FileChunkHeader
 *
 * This structure serves as the header for each individual chunk section
 * stored on disk, preceding its `StudentRecord` payload.
 *
 * Members:
 *   chunk_id   : The logical identifier of this chunk, corresponding to its in-memory `chunk_id`.
 *   reserved0  : Reserved for alignment or future use.
 *   used_slots : The count of slots that have ever been assigned a record within this chunk.
 *   tombstones : The number of `used_slots` in this chunk that contain records
 *                marked with `FRASE_TOMBSTONE_ID`.
 *   reserved   : Reserved bytes for future expansion, currently unused in Version 1.
*/
typedef struct FileChunkHeader {
    uint32_t chunk_id;   // Logical ID of this chunk.
    uint32_t reserved0;  // Reserved for alignment or future use.

    uint64_t used_slots; // Number of slots that have ever been assigned in this chunk.
    uint64_t tombstones; // Number of used slots in this chunk whose record ID is FRASE_TOMBSTONE_ID.

    uint8_t  reserved[32]; // Padding for future use; unused in current version.
} FileChunkHeader;

/*
 * File Layout Size Helpers
 *
 * These macros define the fixed sizes of components within the on-disk file format,
 * facilitating calculations for file offsets and memory allocation.
*/
#define FRASE_FILE_CHUNK_PAYLOAD_SIZE \
    (FRASE_CHUNK_RECORD_CAPACITY * sizeof(StudentRecord)) // Total bytes occupied by the StudentRecord payload of a single chunk.

#define FRASE_FILE_CHUNK_SECTION_SIZE \
    (sizeof(FileChunkHeader) + FRASE_FILE_CHUNK_PAYLOAD_SIZE) // Total bytes for one complete on-disk chunk section (header + payload).

/*
 * Compile-time Layout Checks
 *
 * These `static_assert` statements are crucial for maintaining the integrity
 * and stability of the file format during development. They ensure that key
 * data structures adhere to their expected sizes, preventing subtle layout
 * issues that could lead to data corruption or incompatibility.
*/
static_assert(sizeof(StudentRecord) == 64, "StudentRecord must be exactly 64 bytes");

//Public File Operation Functions:

//Loads the chunks from the files during program boot:
FileIoError file_io_load(const char *path, ChunkManager *manager);

//Flushes the chunks to the files during program shutdown:
FileIoError file_io_save(const char *path, ChunkManager *manager);

//Converts error message to string format:
const char *file_io_error_string(FileIoError error);

#endif