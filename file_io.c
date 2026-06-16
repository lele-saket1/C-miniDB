#include "file_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

/*
 * Private input-phase helper functions.
 * These functions are responsible for reading data from the database file
 * and validating its structure during the loading process.
 */
/**
 * @brief Creates a new, valid, and empty FRASE database backing file.
 * @param path The file path where the empty database file should be created.
 * @return     `FILE_IO_OK` on success, or an appropriate `FileIoError` code on failure.
 */
static FileIoError file_io_create_empty_file(const char *path);

/**
 * @brief Reads the `FileSuperBlock` from the beginning of the file stream.
 * @param file       The file pointer to read from.
 * @param superblock Pointer to the `FileSuperBlock` structure to populate.
 * @return           `FILE_IO_OK` on success, or an error code on failure.
 */
static FileIoError file_io_read_superblock(FILE *file, FileSuperBlock *superblock);

/**
 * @brief Performs structural validation on a `FileSuperBlock`.
 * @param superblock Pointer to the `FileSuperBlock` structure to validate.
 * @return           `FILE_IO_OK` if the superblock is valid, or an error code if corrupted.
 */
static FileIoError file_io_validate_superblock(const FileSuperBlock *superblock);

/**
 * @brief Reads a chunk's header and its record payload from the file.
 * @param file              The file pointer.
 * @param chunk             Pointer to the `Chunk` structure to populate.
 * @param expected_chunk_id The expected logical ID of the chunk being read.
 * @return                  `FILE_IO_OK` on success, or an error code on failure.
 */
static FileIoError file_io_read_chunk(
    FILE *file,
    Chunk *chunk,
    uint32_t expected_chunk_id
);

/**
 * @brief Performs structural validation on a `FileChunkHeader`.
 * @param header            Pointer to the `FileChunkHeader` structure to validate.
 * @param expected_chunk_id The logical ID that the chunk header's `chunk_id` should match.
 * @return                  `FILE_IO_OK` if the chunk header is valid, or an error code if corrupted.
 */
static FileIoError file_io_validate_chunk_header(
    const FileChunkHeader *header,
    uint32_t expected_chunk_id
);

/*
 * Private output-phase helper functions.
 * These functions handle writing data to the database file during save operations.
 */
/**
 * @brief Constructs a `FileSuperBlock` from the `ChunkManager` and writes it to the beginning of the file.
 * @param file    The file pointer to write to.
 * @param manager Pointer to the `ChunkManager` containing the current database state.
 * @return        `FILE_IO_OK` on success, or an error code on failure.
 */
static FileIoError file_io_write_superblock(
    FILE *file,
    const ChunkManager *manager
);

/*
 * Writes a complete chunk section (header + payload) to the file.
 * @param file  The file pointer.
 * @param chunk Pointer to the `Chunk` structure to write.
 * @return      `FILE_IO_OK` on success, or an error code on failure.
 */
static FileIoError file_io_write_chunk(FILE *file, const Chunk *chunk);

/**
 * @brief Iterates through all chunks in the `ChunkManager` and sets their `dirty` flag to `false`.
 * @param manager Pointer to the `ChunkManager` whose chunks should be marked clean.
 * @return        `FILE_IO_OK` on success, or `FILE_IO_ERR_NULL_ARGUMENT` if manager is NULL.
 */
static FileIoError file_io_mark_chunks_clean(ChunkManager *manager);

/*
 * Private common helper functions.
 * These functions provide utility operations used by both input and output phases.
 */
/**
 * @brief Calculates the byte offset within the backing file where a specific chunk begins.
 * @param chunk_id The logical ID of the chunk.
 * @return         The byte offset of the chunk within the file.
 */
static uint64_t file_io_chunk_offset(uint32_t chunk_id);

/**
 * @brief Moves the file cursor to the beginning of the specified chunk section.
 * @param file     The file pointer to seek within.
 * @param chunk_id The logical ID of the chunk to seek to.
 * @return         `FILE_IO_OK` on success, or an error code on failure.
 */
static FileIoError file_io_seek_to_chunk(FILE *file, uint32_t chunk_id);

/*
 * Private compaction helper functions.
 * These functions are used internally by `file_io_compact` to manage the compaction process.
 */
/**
 * @brief Generates a temporary file path by appending ".tmp" to the original path.
 * @param path The original file path.
 * @return     A dynamically allocated string containing the temporary path,
 *             or `NULL` on allocation failure. The caller is responsible for freeing this string.
 */
static char *file_io_make_temp_path(const char *path);

/**
 * @brief Computes the ceiling division of two unsigned 64-bit integers.
 * @param numerator   The dividend.
 * @param denominator The divisor.
 * @return            `ceil(numerator / denominator)`. Returns 0 if denominator is 0 or numerator is 0.
 */
static uint64_t file_io_ceil_div_u64(uint64_t numerator, uint64_t denominator);

/**
 * @brief Writes a new, compacted `FileSuperBlock` to the temporary file.
 * @param new_file       The file pointer to the new (compacted) file.
 * @param old_superblock Pointer to the `FileSuperBlock` from the original file.
 * @return               `FILE_IO_OK` on success, or an error code on failure.
 */
static FileIoError file_io_write_compacted_superblock(
    FILE *new_file,
    const FileSuperBlock *old_superblock
);

/**
 * @brief Writes a single compacted chunk (header + payload) to the temporary file.
 * @param new_file   The file pointer to the new (compacted) file.
 * @param records    Pointer to an array of `StudentRecord`s to write.
 * @param used_slots The number of live records in this output chunk.
 * @param chunk_id   The new logical ID for this compacted chunk.
 * @return           `FILE_IO_OK` on success, or an error code on failure.
 */
static FileIoError file_io_write_compacted_chunk(
    FILE *new_file,
    const StudentRecord *records,
    size_t used_slots,
    uint32_t chunk_id
);

/**
 * @brief Copies all live records from the old database file into the new, compacted file.
 *
 * This function iterates through the old file, reads chunks, filters out tombstoned
 * records, and writes the live records into new, full chunks in the compacted file.
 * @param old_file       The file pointer to the original database file.
 * @param new_file       The file pointer to the new (compacted) database file.
 * @param old_superblock Pointer to the `FileSuperBlock` from the original file.
 * @return               `FILE_IO_OK` on success, or an error code on failure.
 */
static FileIoError file_io_copy_live_records_into_compacted_file(
    FILE *old_file,
    FILE *new_file,
    const FileSuperBlock *old_superblock
);

/**
 * @brief Replaces the old database file with the newly compacted temporary file.
 * @param old_path The path to the original database file.
 * @param temp_path The path to the temporary compacted file.
 * @return          `FILE_IO_OK` on success, or `FILE_IO_ERR_RENAME_FAILED` if the rename operation fails.
 */
static FileIoError file_io_replace_file(
    const char *old_path,
    const char *temp_path
);



/**
 * @brief Loads the database from a backing file into memory.
 *
 * This function is typically called once during application startup to
 * initialize the `ChunkManager` with data from the persistent storage.
 * It handles file opening, superblock validation, and reading all chunks.
 *
 * @param path    The file path to the database backing file.
 * @param manager Pointer to the `ChunkManager` structure to populate with loaded data.
 * @return        `FILE_IO_OK` on successful load, or an appropriate `FileIoError` code on failure.
 */

FileIoError file_io_load(const char *path, ChunkManager *manager)
{
    FILE *file;
    FileSuperBlock superblock;
    FileIoError error;
    size_t chunk_count;
    size_t i;
    uint64_t summed_used_slots = 0;
    uint64_t summed_tombstones = 0;

    if (path == NULL || manager == NULL) {
        return FILE_IO_ERR_NULL_ARGUMENT;
    }

    /*
     * This load function expects an empty/uninitialized manager.
     * We zero it here to ensure that all fields are in a known, safe state,
     * especially important for error handling paths.
    */

    memset(manager, 0, sizeof(*manager));

    file = fopen(path, "rb");

    if (file == NULL) {
        /*
         * If the file doesn't exist, attempt to create a new empty database file.
         */
        error = file_io_create_empty_file(path);

        if (error != FILE_IO_OK) {
            return error;
        }

        file = fopen(path, "rb");

        if (file == NULL) {
            return FILE_IO_ERR_OPEN_FAILED;
        }
    }

    /*
     * If the file exists but is empty (e.g., a newly created file that wasn't
     * properly initialized, or truncated), replace it with a valid empty FRASE file.
     * This ensures a consistent starting state.
    */

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return FILE_IO_ERR_SEEK_FAILED;
    }

    {
        long file_size = ftell(file);

        if (file_size < 0) {
            fclose(file);
            return FILE_IO_ERR_SEEK_FAILED;
        }

        if (file_size == 0) {
            /*
             * File is empty, close it and create a new empty FRASE file.
             */
            fclose(file);

            error = file_io_create_empty_file(path);

            if (error != FILE_IO_OK) {
                return error;
            }

            file = fopen(path, "rb");

            if (file == NULL) {
                return FILE_IO_ERR_OPEN_FAILED;
            }
        }
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return FILE_IO_ERR_SEEK_FAILED;
    }

    /*
     * Read and validate the global superblock.
     */
    error = file_io_read_superblock(file, &superblock);

    if (error != FILE_IO_OK) {
        fclose(file);
        return error;
    }

    error = file_io_validate_superblock(&superblock);

    if (error != FILE_IO_OK) {
        fclose(file);
        return error;
    }

    /*
     * Perform sanity checks on superblock values against system limits.
     */
    if (superblock.chunk_count > (uint64_t)SIZE_MAX) {
        fclose(file);
        return FILE_IO_ERR_CORRUPT_SUPERBLOCK;
    }

    if (superblock.total_used_slots > (uint64_t)SIZE_MAX ||
        superblock.total_tombstones > (uint64_t)SIZE_MAX) {
        fclose(file);
        return FILE_IO_ERR_CORRUPT_SUPERBLOCK;
    }

    chunk_count = (size_t)superblock.chunk_count;

    manager->chunk_count = chunk_count;
    manager->chunk_capacity = chunk_count;
    manager->total_used_slots = (size_t)superblock.total_used_slots;
    manager->total_tombstones = (size_t)superblock.total_tombstones;

    /*
     * If there are no chunks, the manager is initialized, and we can exit.
     */
    if (chunk_count == 0) {
        manager->chunks = NULL;

        if (fclose(file) != 0) {
            return FILE_IO_ERR_CLOSE_FAILED;
        }

        return FILE_IO_OK;
    }

    /*
     * Allocate memory for the array of Chunk structures.
     */
    if (chunk_count > SIZE_MAX / sizeof(Chunk)) {
        fclose(file);
        memset(manager, 0, sizeof(*manager));
        return FILE_IO_ERR_ALLOCATION_FAILED;
    }

    manager->chunks = calloc(chunk_count, sizeof(Chunk));

    if (manager->chunks == NULL) {
        fclose(file);
        memset(manager, 0, sizeof(*manager));
        return FILE_IO_ERR_ALLOCATION_FAILED;
    }

    /*
     * Read each chunk's header and payload, populating the ChunkManager.
     */
    for (i = 0; i < chunk_count; i++) {
        error = file_io_read_chunk(file, &manager->chunks[i], (uint32_t)i);

        if (error != FILE_IO_OK) {
            size_t j;

            for (j = 0; j < i; j++) {
                free(manager->chunks[j].records);
                manager->chunks[j].records = NULL;
            }

            free(manager->chunks);
            memset(manager, 0, sizeof(*manager));

            fclose(file);
            return error;
        }

        summed_used_slots += (uint64_t)manager->chunks[i].used_slots;
        summed_tombstones += (uint64_t)manager->chunks[i].tombstones;
    }

    /*
     * Verify consistency: The global totals in the superblock must match
     * the sum of local totals from all individual chunk headers.
    */
    if (summed_used_slots != superblock.total_used_slots ||
        summed_tombstones != superblock.total_tombstones) {
        for (i = 0; i < chunk_count; i++) {
            free(manager->chunks[i].records);
            manager->chunks[i].records = NULL;
        }

        free(manager->chunks);
        memset(manager, 0, sizeof(*manager));

        fclose(file);
        return FILE_IO_ERR_CORRUPT_SUPERBLOCK;
    }

    /*
     * Close the file and return success.
     */
    if (fclose(file) != 0) {
        for (i = 0; i < chunk_count; i++) {
            free(manager->chunks[i].records);
            manager->chunks[i].records = NULL;
        }

        free(manager->chunks);
        memset(manager, 0, sizeof(*manager));

        return FILE_IO_ERR_CLOSE_FAILED;
    }

    return FILE_IO_OK;
}


/**
 * @brief Saves the current state of the `ChunkManager` to the database backing file.
 *
 * This function is typically called during a save operation or before application
 * shutdown. It writes only 'dirty' (modified) chunks back to disk and updates
 * the `FileSuperBlock`.
 *
 * @param path    The file path to the database backing file.
 * @param manager Pointer to the `ChunkManager` containing the data to save.
 * @return        `FILE_IO_OK` on successful save, or an appropriate `FileIoError` code on failure.
 */

FileIoError file_io_save(const char *path, ChunkManager *manager)
{
    FILE *file;
    FileIoError error;
    size_t i;

    if (path == NULL || manager == NULL) {
        return FILE_IO_ERR_NULL_ARGUMENT;
    }

    file = fopen(path, "r+b");

    if (file == NULL) {
        /*
         * If the file doesn't exist, create an empty one first.
         */
        error = file_io_create_empty_file(path);

        if (error != FILE_IO_OK) {
            return error;
        }

        file = fopen(path, "r+b");

        if (file == NULL) {
            return FILE_IO_ERR_OPEN_FAILED;
        }
    }

    for (i = 0; i < manager->chunk_count; i++) {
        Chunk *chunk = &manager->chunks[i]; // Get a pointer to the current chunk.

        if (chunk->dirty) {
            /*
             * If the chunk is dirty, it needs to be written back to disk.
             * First, perform a sanity check on its ID.
             */
            if (chunk->chunk_id != (uint32_t)i) {
                // This indicates an internal corruption or logic error.
                // The chunk_id should match its index in the manager's array.
                fclose(file);
                return FILE_IO_ERR_CORRUPT_CHUNK_HEADER;
            }

            error = file_io_seek_to_chunk(file, chunk->chunk_id);

            if (error != FILE_IO_OK) {
                fclose(file);
                return error;
            }

            error = file_io_write_chunk(file, chunk);

            if (error != FILE_IO_OK) {
                fclose(file);
                return error;
            }
        }
    }

    /*
     * Write the updated superblock to the beginning of the file.
     */
    error = file_io_write_superblock(file, manager);

    if (error != FILE_IO_OK) {
        fclose(file);
        return error;
    }

    /*
     * Flush any buffered data to ensure it's written to physical disk.
     */
    if (fflush(file) != 0) {
        fclose(file);
        return FILE_IO_ERR_FLUSH_FAILED;
    }

    if (fclose(file) != 0) {
        return FILE_IO_ERR_CLOSE_FAILED;
    }

    /*
     * If the save was successful, mark all chunks as clean.
     */
    error = file_io_mark_chunks_clean(manager);

    if (error != FILE_IO_OK) {
        return error;
    }

    return FILE_IO_OK;
}


/**
 * @brief Converts a `FileIoError` enumeration value into a human-readable string.
 * @param error The `FileIoError` code to convert.
 * @return      A constant string literal describing the error.
 */

const char *file_io_error_string(FileIoError error)
{
    switch (error) {
        case FILE_IO_OK:
            return "file I/O: ok";

        case FILE_IO_ERR_NULL_ARGUMENT:
            return "file I/O: null argument";

        case FILE_IO_ERR_OPEN_FAILED:
            return "file I/O: open failed";

        case FILE_IO_ERR_READ_FAILED:
            return "file I/O: read failed";

        case FILE_IO_ERR_WRITE_FAILED:
            return "file I/O: write failed";

        case FILE_IO_ERR_SEEK_FAILED:
            return "file I/O: seek failed";

        case FILE_IO_ERR_FLUSH_FAILED:
            return "file I/O: flush failed";

        case FILE_IO_ERR_CLOSE_FAILED:
            return "file I/O: close failed";

        case FILE_IO_ERR_BAD_MAGIC:
            return "file I/O: bad file magic";

        case FILE_IO_ERR_UNSUPPORTED_VERSION:
            return "file I/O: unsupported file version";

        case FILE_IO_ERR_CORRUPT_SUPERBLOCK:
            return "file I/O: corrupt superblock";

        case FILE_IO_ERR_CORRUPT_CHUNK_HEADER:
            return "file I/O: corrupt chunk header";

        case FILE_IO_ERR_ALLOCATION_FAILED:
            return "file I/O: allocation failed";

        case FILE_IO_ERR_RENAME_FAILED:
            return "file I/O: rename failed";

        default:
            return "file I/O: unknown error";

    }
}


/**
 * @brief Creates a new, valid, and empty FRASE database backing file.
 *
 * An "empty" file means a valid `FileSuperBlock` with zero chunks and records.
 * @param path The file path where the empty database file should be created.
 * @return     `FILE_IO_OK` on success, or an appropriate `FileIoError` code on failure.
 */

static FileIoError file_io_create_empty_file(const char *path)
{
    FILE *file;
    ChunkManager empty_manager;
    FileIoError error;

    if (path == NULL) {
        return FILE_IO_ERR_NULL_ARGUMENT;
    }

    memset(&empty_manager, 0, sizeof(empty_manager));

    file = fopen(path, "wb");

    if (file == NULL) {
        return FILE_IO_ERR_OPEN_FAILED;
    }

    error = file_io_write_superblock(file, &empty_manager);

    if (error != FILE_IO_OK) {
        fclose(file);
        return error;
    }

    if (fflush(file) != 0) {
        fclose(file);
        return FILE_IO_ERR_FLUSH_FAILED;
    }

    if (fclose(file) != 0) {
        return FILE_IO_ERR_CLOSE_FAILED;
    }

    return FILE_IO_OK;
}


/**
 * @brief Reads the `FileSuperBlock` from the beginning of the file stream.
 *
 * The file pointer is reset to the start before reading.
 * @param file       The file pointer to read from.
 * @param superblock Pointer to the `FileSuperBlock` structure to populate.
 * @return           `FILE_IO_OK` on success, or an error code on failure.
 */

static FileIoError file_io_read_superblock(FILE *file, FileSuperBlock *superblock)
{
    if (file == NULL || superblock == NULL) {
        return FILE_IO_ERR_NULL_ARGUMENT;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        return FILE_IO_ERR_SEEK_FAILED;
    }

    if (fread(superblock, sizeof(*superblock), 1, file) != 1) {
        return FILE_IO_ERR_READ_FAILED;
    }

    return FILE_IO_OK;
}


/**
 * @brief Performs structural validation on a `FileSuperBlock`.
 *
 * Ensures its integrity and consistency with the defined file format.
 * @param superblock Pointer to the `FileSuperBlock` structure to validate.
 * @return           `FILE_IO_OK` if the superblock is valid, or an error code if corrupted.
 */

static FileIoError file_io_validate_superblock(const FileSuperBlock *superblock)
{
    uint64_t max_possible_used_slots;

    if (superblock == NULL) {
        return FILE_IO_ERR_NULL_ARGUMENT;
    }

    if (superblock->magic != FRASE_FILE_MAGIC) {
        return FILE_IO_ERR_BAD_MAGIC;
    }

    if (superblock->version != FRASE_FILE_VERSION) {
        return FILE_IO_ERR_UNSUPPORTED_VERSION;
    }

    if (superblock->total_tombstones > superblock->total_used_slots) {
        return FILE_IO_ERR_CORRUPT_SUPERBLOCK;
    }

    if (superblock->chunk_count > UINT64_MAX / FRASE_CHUNK_RECORD_CAPACITY) {
        return FILE_IO_ERR_CORRUPT_SUPERBLOCK;
    }

    max_possible_used_slots =
        superblock->chunk_count * (uint64_t)FRASE_CHUNK_RECORD_CAPACITY;

    if (superblock->total_used_slots > max_possible_used_slots) {
        return FILE_IO_ERR_CORRUPT_SUPERBLOCK;
    }

    if (superblock->chunk_count == 0 &&
        (superblock->total_used_slots != 0 ||
         superblock->total_tombstones != 0)) {
        return FILE_IO_ERR_CORRUPT_SUPERBLOCK;
    }

    return FILE_IO_OK;
}


/**
 * @brief Reads a complete chunk section (header + payload) from the file.
 *
 * The `Chunk` structure is populated, and its `dirty` flag is set to `false`.
 * @param file              The file pointer to read from.
 * @param chunk             Pointer to the `Chunk` structure to populate.
 * @param expected_chunk_id The expected logical ID of the chunk being read.
 * @return                  `FILE_IO_OK` on success, or an error code on failure.
 */

static FileIoError file_io_read_chunk(
    FILE *file,
    Chunk *chunk,
    uint32_t expected_chunk_id
)
{
    FileChunkHeader header;
    FileIoError error;

    if (file == NULL || chunk == NULL) {
        return FILE_IO_ERR_NULL_ARGUMENT;
    }

    if (fread(&header, sizeof(header), 1, file) != 1) {
        return FILE_IO_ERR_READ_FAILED;
    }

    error = file_io_validate_chunk_header(&header, expected_chunk_id);

    if (error != FILE_IO_OK) {
        return error;
    }

    chunk->records = calloc(
        FRASE_CHUNK_RECORD_CAPACITY,
        sizeof(StudentRecord)
    );

    if (chunk->records == NULL) {
        return FILE_IO_ERR_ALLOCATION_FAILED;
    }

    if (fread(
            chunk->records,
            sizeof(StudentRecord),
            FRASE_CHUNK_RECORD_CAPACITY,
            file
        ) != FRASE_CHUNK_RECORD_CAPACITY) {
        free(chunk->records);
        chunk->records = NULL;
        return FILE_IO_ERR_READ_FAILED;
    }

    chunk->chunk_id = header.chunk_id;
    chunk->used_slots = (size_t)header.used_slots;
    chunk->tombstones = (size_t)header.tombstones;
    chunk->dirty = false;

    return FILE_IO_OK;
}


/**
 * @brief Performs structural validation on a `FileChunkHeader`.
 *
 * Ensures its integrity and consistency, including checking its `chunk_id`.
 * @param header            Pointer to the `FileChunkHeader` structure to validate.
 * @param expected_chunk_id The logical ID that the chunk header's `chunk_id` should match.
 * @return                  `FILE_IO_OK` if the chunk header is valid, or an error code if corrupted.
 */

static FileIoError file_io_validate_chunk_header(
    const FileChunkHeader *header,
    uint32_t expected_chunk_id
)
{
    if (header == NULL) {
        return FILE_IO_ERR_NULL_ARGUMENT;
    }

    if (header->chunk_id != expected_chunk_id) {
        return FILE_IO_ERR_CORRUPT_CHUNK_HEADER;
    }

    if (header->used_slots > FRASE_CHUNK_RECORD_CAPACITY) {
        return FILE_IO_ERR_CORRUPT_CHUNK_HEADER;
    }

    if (header->tombstones > header->used_slots) {
        return FILE_IO_ERR_CORRUPT_CHUNK_HEADER;
    }

    if (header->used_slots > (uint64_t)SIZE_MAX ||
        header->tombstones > (uint64_t)SIZE_MAX) {
        return FILE_IO_ERR_CORRUPT_CHUNK_HEADER;
    }

    return FILE_IO_OK;
}


/**
 * @brief Constructs a `FileSuperBlock` from the `ChunkManager` and writes it to the beginning of the file.
 * @param file    The file pointer to write to.
 * @param manager Pointer to the `ChunkManager` containing the current database state.
 * @return        `FILE_IO_OK` on success, or an error code on failure.
 */

static FileIoError file_io_write_superblock(
    FILE *file,
    const ChunkManager *manager
)
{
    FileSuperBlock superblock;

    if (file == NULL || manager == NULL) {
        return FILE_IO_ERR_NULL_ARGUMENT;
    }

    memset(&superblock, 0, sizeof(superblock));

    superblock.magic = FRASE_FILE_MAGIC;
    superblock.version = FRASE_FILE_VERSION;
    superblock.chunk_count = (uint64_t)manager->chunk_count;
    superblock.total_used_slots = (uint64_t)manager->total_used_slots;
    superblock.total_tombstones = (uint64_t)manager->total_tombstones;

    if (superblock.total_tombstones > superblock.total_used_slots) {
        return FILE_IO_ERR_CORRUPT_SUPERBLOCK;
    }

    if (superblock.chunk_count > UINT64_MAX / FRASE_CHUNK_RECORD_CAPACITY) {
        return FILE_IO_ERR_CORRUPT_SUPERBLOCK;
    }

    if (superblock.total_used_slots >
        superblock.chunk_count * (uint64_t)FRASE_CHUNK_RECORD_CAPACITY) {
        return FILE_IO_ERR_CORRUPT_SUPERBLOCK;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        return FILE_IO_ERR_SEEK_FAILED;
    }

    if (fwrite(&superblock, sizeof(superblock), 1, file) != 1) {
        return FILE_IO_ERR_WRITE_FAILED;
    }

    return FILE_IO_OK;
}


/**
 * @brief Writes a complete chunk section (header + payload) to the file.
 *
 * The caller must ensure the file cursor is at the correct offset.
 * @param file  The file pointer to write to.
 * @param chunk Pointer to the `Chunk` structure to write.
 * @return      `FILE_IO_OK` on success, or an error code on failure.
 */

static FileIoError file_io_write_chunk(FILE *file, const Chunk *chunk)
{
    FileChunkHeader header;

    if (file == NULL || chunk == NULL || chunk->records == NULL) {
        return FILE_IO_ERR_NULL_ARGUMENT;
    }

    if (chunk->used_slots > FRASE_CHUNK_RECORD_CAPACITY) {
        return FILE_IO_ERR_CORRUPT_CHUNK_HEADER;
    }

    if (chunk->tombstones > chunk->used_slots) {
        return FILE_IO_ERR_CORRUPT_CHUNK_HEADER;
    }

    memset(&header, 0, sizeof(header));

    header.chunk_id = chunk->chunk_id;
    header.used_slots = (uint64_t)chunk->used_slots;
    header.tombstones = (uint64_t)chunk->tombstones;

    if (fwrite(&header, sizeof(header), 1, file) != 1) {
        return FILE_IO_ERR_WRITE_FAILED;
    }

    if (fwrite(
            chunk->records,
            sizeof(StudentRecord),
            FRASE_CHUNK_RECORD_CAPACITY,
            file
        ) != FRASE_CHUNK_RECORD_CAPACITY) {
        return FILE_IO_ERR_WRITE_FAILED;
    }

    return FILE_IO_OK;
}


/**
 * @brief Iterates through all chunks in the `ChunkManager` and sets their `dirty` flag to `false`.
 *
 * This is typically called after a successful save operation.
 * @param manager Pointer to the `ChunkManager` whose chunks should be marked clean.
 * @return        `FILE_IO_OK` on success, or `FILE_IO_ERR_NULL_ARGUMENT` if manager is NULL.
 */

static FileIoError file_io_mark_chunks_clean(ChunkManager *manager)
{
    size_t i;

    if (manager == NULL) {
        return FILE_IO_ERR_NULL_ARGUMENT;
    }

    for (i = 0; i < manager->chunk_count; i++) {
        manager->chunks[i].dirty = false;
    }

    return FILE_IO_OK;
}


/**
 * @brief Calculates the byte offset within the backing file where a specific chunk begins.
 *
 * This calculation relies on the fixed file layout: `sizeof(FileSuperBlock) + (N * FRASE_FILE_CHUNK_SECTION_SIZE)`.
 * @param chunk_id The logical ID of the chunk.
 * @return         The byte offset of the chunk within the file.
 */

static uint64_t file_io_chunk_offset(uint32_t chunk_id)
{
    return sizeof(FileSuperBlock)
         + ((uint64_t)chunk_id * (uint64_t)FRASE_FILE_CHUNK_SECTION_SIZE);
}


/**
 * @brief Moves the file cursor to the beginning of the specified chunk section.
 *
 * Uses `file_io_chunk_offset` to determine the target position.
 * @param file     The file pointer to seek within.
 * @param chunk_id The logical ID of the chunk to seek to.
 * @return         `FILE_IO_OK` on success, or an error code on failure.
 */

static FileIoError file_io_seek_to_chunk(FILE *file, uint32_t chunk_id)
{
    uint64_t offset;
    if (file == NULL) {
        return FILE_IO_ERR_NULL_ARGUMENT;
    }

    offset = file_io_chunk_offset(chunk_id);

    /*
     * `fseek` typically uses `long` for offsets. While this is often 64-bit
     * on modern Linux systems, it can be 32-bit on others. This check
     * prevents silent truncation of large 64-bit offsets to a smaller `long`
     * type, which could lead to incorrect seeking on platforms where `long` is 32-bit.
    */

    if (offset > (uint64_t)LONG_MAX) {
        return FILE_IO_ERR_SEEK_FAILED;
    }

    if (fseek(file, (long)offset, SEEK_SET) != 0) {
        return FILE_IO_ERR_SEEK_FAILED;
    }

    return FILE_IO_OK;
}

/**
 * @brief Reads file statistics (metadata) from the superblock without loading all chunks.
 * @param path  The file path to the database backing file.
 * @param stats Pointer to a `FileStats` structure to populate with the read statistics.
 * @return      `FILE_IO_OK` on success, or an appropriate `FileIoError` code on failure.
 */

FileIoError file_io_read_stats(const char *path, FileStats *stats)
{
    FILE *file;
    FileSuperBlock superblock;
    FileIoError error;

    if (path == NULL || stats == NULL) {
        return FILE_IO_ERR_NULL_ARGUMENT;
    }

    memset(stats, 0, sizeof(*stats));

    file = fopen(path, "rb");

    if (file == NULL) {
        /*
            If the file does not exist yet, treat it as an empty database.
            The later file_io_load() call can create the actual empty file.
        */
        return FILE_IO_OK;
    }

    error = file_io_read_superblock(file, &superblock);

    if (error != FILE_IO_OK) {
        fclose(file);
        return error;
    }

    error = file_io_validate_superblock(&superblock);

    if (error != FILE_IO_OK) {
        fclose(file);
        return error;
    }

    stats->chunk_count = superblock.chunk_count;
    stats->total_used_slots = superblock.total_used_slots;
    stats->total_tombstones = superblock.total_tombstones;

    if (fclose(file) != 0) {
        return FILE_IO_ERR_CLOSE_FAILED;
    }

    return FILE_IO_OK;
}

/**
 * @brief Compacts the database file by removing tombstoned records and rewriting
 *        live records into a new, optimized file layout.
 *
 * This function creates a temporary compacted file, copies only active records,
 * and then replaces the original file with the compacted version.
 * After compaction, `total_tombstones` will be 0, and `total_used_slots` will
 * reflect the count of active records. All non-tail chunks will be full.
 *
 * @param path The file path to the database backing file to compact.
 * @return     `FILE_IO_OK` on successful compaction, or an appropriate `FileIoError` code on failure.
 */

FileIoError file_io_compact(const char *path)
{
    FILE *old_file;
    FILE *new_file;

    char *temp_path;

    FileSuperBlock old_superblock;
    FileIoError error;

    if (path == NULL) {
        return FILE_IO_ERR_NULL_ARGUMENT;
    }

    temp_path = file_io_make_temp_path(path);

    if (temp_path == NULL) {
        return FILE_IO_ERR_ALLOCATION_FAILED;
    }

    old_file = fopen(path, "rb");

    if (old_file == NULL) {
        free(temp_path);
        return FILE_IO_ERR_OPEN_FAILED;
    }

    error = file_io_read_superblock(old_file, &old_superblock);

    if (error != FILE_IO_OK) {
        fclose(old_file);
        free(temp_path);
        return error;
    }

    error = file_io_validate_superblock(&old_superblock);

    if (error != FILE_IO_OK) {
        fclose(old_file);
        free(temp_path);
        return error;
    }

    new_file = fopen(temp_path, "wb");

    if (new_file == NULL) {
        fclose(old_file);
        free(temp_path);
        return FILE_IO_ERR_OPEN_FAILED;
    }

    error = file_io_write_compacted_superblock(new_file, &old_superblock);

    if (error != FILE_IO_OK) {
        fclose(old_file);
        fclose(new_file);
        remove(temp_path);
        free(temp_path);
        return error;
    }

    error = file_io_copy_live_records_into_compacted_file(
        old_file,
        new_file,
        &old_superblock
    );

    if (error != FILE_IO_OK) {
        fclose(old_file);
        fclose(new_file);
        remove(temp_path);
        free(temp_path);
        return error;
    }

    if (fflush(new_file) != 0) {
        fclose(old_file);
        fclose(new_file);
        remove(temp_path);
        free(temp_path);
        return FILE_IO_ERR_FLUSH_FAILED;
    }

    if (fclose(old_file) != 0) {
        fclose(new_file);
        remove(temp_path);
        free(temp_path);
        return FILE_IO_ERR_CLOSE_FAILED;
    }

    if (fclose(new_file) != 0) {
        remove(temp_path);
        free(temp_path);
        return FILE_IO_ERR_CLOSE_FAILED;
    }

    error = file_io_replace_file(path, temp_path);

    if (error != FILE_IO_OK) {
        remove(temp_path);
        free(temp_path);
        return error;
    }

    free(temp_path);

    return FILE_IO_OK;
}

/**
 * @brief Generates a temporary file path by appending ".tmp" to the original path.
 *
 * The caller is responsible for freeing the returned string.
 * @param path The original file path.
 * @return     A dynamically allocated string containing the temporary path,
 *             or `NULL` on allocation failure.
 */

static char *file_io_make_temp_path(const char *path)
{
    size_t path_length;
    char *temp_path;

    if (path == NULL) {
        return NULL;
    }

    path_length = strlen(path);

    temp_path = malloc(path_length + 5);

    if (temp_path == NULL) {
        return NULL;
    }

    memcpy(temp_path, path, path_length);
    memcpy(temp_path + path_length, ".tmp", 5);

    return temp_path;
}


/**
 * @brief Computes the ceiling division of two unsigned 64-bit integers.
 * @param numerator   The dividend.
 * @param denominator The divisor.
 * @return            `ceil(numerator / denominator)`. Returns 0 if denominator is 0 or numerator is 0.
 */

static uint64_t file_io_ceil_div_u64(uint64_t numerator, uint64_t denominator)
{
    if (denominator == 0) {
        return 0;
    }

    if (numerator == 0) {
        return 0;
    }

    return 1 + ((numerator - 1) / denominator);
}


/**
 * @brief Writes a new, compacted `FileSuperBlock` to the temporary file.
 *
 * This function calculates the new `chunk_count`, `total_used_slots`, and
 * sets `total_tombstones` to 0 based on the old superblock's data.
 * @param new_file       The file pointer to the new (compacted) file.
 * @param old_superblock Pointer to the `FileSuperBlock` from the original file.
 * @return               `FILE_IO_OK` on success, or an error code on failure.
 */

static FileIoError file_io_write_compacted_superblock(
    FILE *new_file,
    const FileSuperBlock *old_superblock
)
{
    FileSuperBlock new_superblock;
    uint64_t active_records;
    uint64_t new_chunk_count;

    if (new_file == NULL || old_superblock == NULL) {
        return FILE_IO_ERR_NULL_ARGUMENT;
    }

    if (old_superblock->total_tombstones > old_superblock->total_used_slots) {
        return FILE_IO_ERR_CORRUPT_SUPERBLOCK;
    }

    active_records =
        old_superblock->total_used_slots -
        old_superblock->total_tombstones;

    new_chunk_count = file_io_ceil_div_u64(
        active_records,
        (uint64_t)FRASE_CHUNK_RECORD_CAPACITY
    );

    memset(&new_superblock, 0, sizeof(new_superblock));

    new_superblock.magic = FRASE_FILE_MAGIC;
    new_superblock.version = FRASE_FILE_VERSION;
    new_superblock.chunk_count = new_chunk_count;
    new_superblock.total_used_slots = active_records;
    new_superblock.total_tombstones = 0;

    if (fseek(new_file, 0, SEEK_SET) != 0) {
        return FILE_IO_ERR_SEEK_FAILED;
    }

    if (fwrite(&new_superblock, sizeof(new_superblock), 1, new_file) != 1) {
        return FILE_IO_ERR_WRITE_FAILED;
    }

    return FILE_IO_OK;
}


/**
 * @brief Writes a single compacted chunk (header + payload) to the temporary file.
 * @param new_file   The file pointer to the new (compacted) file.
 * @param records    Pointer to an array of `StudentRecord`s to write. This buffer
 *                   is expected to be `FRASE_CHUNK_RECORD_CAPACITY` in size.
 * @param used_slots The number of live records in this output chunk.
 * @param chunk_id   The new logical ID for this compacted chunk.
 * @return           `FILE_IO_OK` on success, or an error code on failure.
 */

static FileIoError file_io_write_compacted_chunk(
    FILE *new_file,
    const StudentRecord *records,
    size_t used_slots,
    uint32_t chunk_id
)
{
    FileChunkHeader header;

    if (new_file == NULL || records == NULL) {
        return FILE_IO_ERR_NULL_ARGUMENT;
    }

    if (used_slots > FRASE_CHUNK_RECORD_CAPACITY) {
        return FILE_IO_ERR_CORRUPT_CHUNK_HEADER;
    }

    memset(&header, 0, sizeof(header));

    header.chunk_id = chunk_id;
    header.used_slots = (uint64_t)used_slots;
    header.tombstones = 0;

    if (fwrite(&header, sizeof(header), 1, new_file) != 1) {
        return FILE_IO_ERR_WRITE_FAILED;
    }

    if (fwrite(
            records,
            sizeof(StudentRecord),
            FRASE_CHUNK_RECORD_CAPACITY,
            new_file
        ) != FRASE_CHUNK_RECORD_CAPACITY) {
        return FILE_IO_ERR_WRITE_FAILED;
    }

    return FILE_IO_OK;
}


/**
 * @brief Copies all live records from the old database file into the new, compacted file.
 *
 * This is the main compaction loop. It reads chunks from the `old_file`,
 * filters out tombstoned records, and buffers live records. When the buffer
 * is full, it writes a new compacted chunk to `new_file`.
 * Handles writing any remaining partial tail chunk at the end.
 * @param old_file       The file pointer to the original database file.
 * @param new_file       The file pointer to the new (compacted) database file.
 * @param old_superblock Pointer to the `FileSuperBlock` from the original file.
 * @return               `FILE_IO_OK` on success, or an error code on failure.
 */

static FileIoError file_io_copy_live_records_into_compacted_file(
    FILE *old_file,
    FILE *new_file,
    const FileSuperBlock *old_superblock
)
{
    StudentRecord *buffer;

    uint64_t old_chunk_index;
    uint32_t output_chunk_id;

    size_t buffer_used;

    FileIoError error;

    if (old_file == NULL || new_file == NULL || old_superblock == NULL) {
        return FILE_IO_ERR_NULL_ARGUMENT;
    }

    buffer = calloc(FRASE_CHUNK_RECORD_CAPACITY, sizeof(StudentRecord));

    if (buffer == NULL) {
        return FILE_IO_ERR_ALLOCATION_FAILED;
    }

    buffer_used = 0;
    output_chunk_id = 0;

    for (old_chunk_index = 0;
         old_chunk_index < old_superblock->chunk_count;
         old_chunk_index++) {

        FileChunkHeader old_header;
        size_t slot;

        if (fread(&old_header, sizeof(old_header), 1, old_file) != 1) {
            free(buffer);
            return FILE_IO_ERR_READ_FAILED;
        }

        error = file_io_validate_chunk_header(
            &old_header,
            (uint32_t)old_chunk_index
        );

        if (error != FILE_IO_OK) {
            free(buffer);
            return error;
        }

        /*
            Read all 1024 payload records so the file cursor naturally lands
            at the next chunk header.

            But only slots below old_header.used_slots are logically meaningful.
        */

        for (slot = 0; slot < FRASE_CHUNK_RECORD_CAPACITY; slot++) {
            StudentRecord record;

            if (fread(&record, sizeof(record), 1, old_file) != 1) {
                free(buffer);
                return FILE_IO_ERR_READ_FAILED;
            }

            if ((uint64_t)slot >= old_header.used_slots) {
                continue;
            }

            if (record.id == FRASE_TOMBSTONE_ID) {
                continue;
            }

            buffer[buffer_used] = record;
            buffer_used++;

            if (buffer_used == FRASE_CHUNK_RECORD_CAPACITY) {
                error = file_io_write_compacted_chunk(
                    new_file,
                    buffer,
                    buffer_used,
                    output_chunk_id
                );

                if (error != FILE_IO_OK) {
                    free(buffer);
                    return error;
                }

                memset(
                    buffer,
                    0,
                    FRASE_CHUNK_RECORD_CAPACITY * sizeof(StudentRecord)
                );

                buffer_used = 0;
                output_chunk_id++;
            }
        }
    }

    /*
        Write the final partially filled tail chunk, if any live records remain.
    */

    if (buffer_used > 0) {
        error = file_io_write_compacted_chunk(
            new_file,
            buffer,
            buffer_used,
            output_chunk_id
        );

        if (error != FILE_IO_OK) {
            free(buffer);
            return error;
        }
    }

    free(buffer);

    return FILE_IO_OK;
}


/**
 * @brief Replaces the old database file with the newly compacted temporary file.
 *
 * This involves removing the original file and renaming the temporary file.
 * Note: This implementation is not fully crash-safe, but is acceptable for FRASE V1.
 * @param old_path  The path to the original database file.
 * @param temp_path The path to the temporary compacted file.
 * @return          `FILE_IO_OK` on success, or `FILE_IO_ERR_RENAME_FAILED` if the rename operation fails.
 */

static FileIoError file_io_replace_file(
    const char *old_path,
    const char *temp_path
)
{
    if (old_path == NULL || temp_path == NULL) {
        return FILE_IO_ERR_NULL_ARGUMENT;
    }

    /*
        If remove fails because the file does not exist, rename will catch the
        real problem anyway. We keep this simple for Version 1.
    */

    remove(old_path);

    if (rename(temp_path, old_path) != 0) {
        return FILE_IO_ERR_RENAME_FAILED;
    }

    return FILE_IO_OK;
}