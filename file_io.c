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
static FileIoError file_io_create_empty_file(const char *path);

static FileIoError file_io_read_superblock(FILE *file, FileSuperBlock *superblock);

static FileIoError file_io_validate_superblock(const FileSuperBlock *superblock);

/*
 * Reads a chunk's header and its record payload from the file.
 * @param file The file pointer.
 * @param chunk Pointer to the Chunk structure to populate.
 * @param expected_chunk_id The expected logical ID of the chunk being read.
 * @return FILE_IO_OK on success, or an error code on failure.
 */
static FileIoError file_io_read_chunk(
    FILE *file,
    Chunk *chunk,
    uint32_t expected_chunk_id
);

static FileIoError file_io_validate_chunk_header(
    const FileChunkHeader *header,
    uint32_t expected_chunk_id
);

/*
 * Private output-phase helper functions.
 * These functions handle writing data to the database file during save operations.
 */
static FileIoError file_io_write_superblock(
    FILE *file,
    const ChunkManager *manager
);

/*
 * Writes a complete chunk section (header + payload) to the file.
 * The caller is responsible for seeking to the correct file offset beforehand.
 * @param file The file pointer.
 * @param chunk Pointer to the Chunk structure to write.
 * @return FILE_IO_OK on success, or an error code on failure.
 */
static FileIoError file_io_write_chunk(FILE *file, const Chunk *chunk);

static FileIoError file_io_mark_chunks_clean(ChunkManager *manager);

/*
 * Private common helper functions.
 * These functions provide utility operations used by both input and output phases.
 */
static uint64_t file_io_chunk_offset(uint32_t chunk_id);

static FileIoError file_io_seek_to_chunk(FILE *file, uint32_t chunk_id);


/*
 * file_io_load
 *
 * Public function to load the database from a backing file into memory.
 * This function is typically called once during application startup.
 *
 * The loading process involves:
 *   - Opening the specified backing file.
 *   - Creating an empty FRASE file if the file does not exist or is empty.
 *   - Reading and validating the `FileSuperBlock`.
 *   - Allocating memory for the `ChunkManager`'s array of `Chunk` structures.
 *   - Iterating through the file to read each `FileChunkHeader` and its
 *     corresponding `StudentRecord` payload.
 *   - Marking all newly loaded chunks as 'clean' (not modified).
 *
 * Important:
 *   This function allocates dynamic memory for `manager->chunks` and for
 *   each `chunk->records` array. It is the caller's responsibility to
 *   free this memory when the `ChunkManager` is no longer needed.
 *
 * @param path The file path to the database backing file.
 * @param manager Pointer to the ChunkManager structure to populate with loaded data.
 * @return FILE_IO_OK on successful load, or an appropriate FileIoError code on failure.
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


/*
 * file_io_save
 *
 * Public function to save the current state of the `ChunkManager` to the
 * database backing file. This is typically called during a save operation
 * or before application shutdown.
 *
 * The saving process involves:
 *   - Opening the backing file in read/write binary mode.
 *   - Iterating through all chunks and writing only the 'dirty' ones
 *     (those that have been modified) back to their fixed offsets in the file.
 *   - Writing the updated `FileSuperBlock` to reflect the current state.
 *   - Flushing all buffered data to disk and closing the file.
 *   - Marking all chunks as 'clean' (not modified) only after the entire
 *     save operation has successfully completed.
 *
 * @param path The file path to the database backing file.
 * @param manager Pointer to the ChunkManager containing the data to save.
 * @return FILE_IO_OK on successful save, or an appropriate FileIoError code on failure.
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


/*
 * file_io_error_string
 *
 * Converts a `FileIoError` enumeration value into a human-readable string.
 * This function is primarily intended for logging, debugging, or displaying
 * error messages to the user.
 *
 * @param error The FileIoError code to convert.
 * @return A constant string literal describing the error.
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

        default:
            return "file I/O: unknown error";
    }
}


/*
 * file_io_create_empty_file
 *
 * Creates a new, valid, and empty FRASE database backing file at the specified path.
 * An "empty" file in this context means:
 *   - `chunk_count` is 0
 *   - `total_used_slots` is 0
 *   - `total_tombstones` is 0
 * No actual chunk sections (headers or payloads) are written to the file yet.
 * This function initializes the file with a valid `FileSuperBlock`.
 *
 * @param path The file path where the empty database file should be created.
 * @return FILE_IO_OK on success, or an appropriate FileIoError code on failure.
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


/*
 * file_io_read_superblock
 *
 * Reads the `FileSuperBlock` (global file header) from the very beginning
 * of the provided file stream. The file pointer is reset to the start before reading.
 *
 * @param file The file pointer to read from.
 * @param superblock Pointer to the FileSuperBlock structure to populate.
 * @return FILE_IO_OK on success, or an error code on failure.
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


/*
 * file_io_validate_superblock
 *
 * Performs structural validation on a `FileSuperBlock` to ensure its integrity
 * and consistency with the defined file format and internal logic.
 *
 * @param superblock Pointer to the FileSuperBlock structure to validate.
 * @return FILE_IO_OK if the superblock is valid, or an error code if corrupted.
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


/*
 * file_io_read_chunk
 *
 * Reads a complete chunk section from the current position in the file stream.
 * This includes reading the `FileChunkHeader` followed by the full
 * `StudentRecord` payload (an array of `FRASE_CHUNK_RECORD_CAPACITY` records).
 * The `Chunk` structure is populated with the data, and its `dirty` flag is
 * set to `false` as it has just been loaded from disk.
 *
 * @param file The file pointer to read from.
 * @param chunk Pointer to the Chunk structure to populate.
 * @param expected_chunk_id The expected logical ID of the chunk being read.
 * @return FILE_IO_OK on success, or an error code on failure.
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


/*
 * file_io_validate_chunk_header
 *
 * Performs structural validation on a `FileChunkHeader` to ensure its integrity
 * and consistency, including checking its `chunk_id` against an expected value.
 *
 * @param header Pointer to the FileChunkHeader structure to validate.
 * @param expected_chunk_id The logical ID that the chunk header's `chunk_id` should match.
 * @return FILE_IO_OK if the chunk header is valid, or an error code if corrupted.
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


/*
 * file_io_write_superblock
 *
 * Constructs a `FileSuperBlock` from the current state of the `ChunkManager`
 * and writes it to the beginning of the file.
 *
 * @param file The file pointer to write to.
 * @param manager Pointer to the ChunkManager containing the current database state.
 * @return FILE_IO_OK on success, or an error code on failure.
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


/*
 * file_io_write_chunk
 *
 * Writes a complete chunk section (including its `FileChunkHeader` and
 * `StudentRecord` payload) to the file. It is crucial that the caller
 * has already positioned the file cursor to the correct offset for this chunk.
 *
 * @param file The file pointer to write to.
 * @param chunk Pointer to the Chunk structure to write.
 * @return FILE_IO_OK on success, or an error code on failure.
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


/*
 * file_io_mark_chunks_clean
 *
 * Iterates through all chunks managed by the `ChunkManager` and sets their
 * `dirty` flag to `false`. This is typically called after a successful save
 * operation to indicate that the in-memory state is synchronized with the disk.
 * @param manager Pointer to the ChunkManager whose chunks should be marked clean.
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


/*
 * file_io_chunk_offset
 *
 * Calculates the byte offset within the backing file where a specific chunk
 * (identified by `chunk_id`) begins. This calculation relies on the fixed
 * file layout:
 *
 * File Layout:
 *   - `FileSuperBlock` (fixed size at the beginning)
 *   - `Chunk Section 0` (header + payload)
 *   - `Chunk Section 1` (header + payload)
 *   - `Chunk Section 2` (header + payload)
 *   - ... and so on.
 *
 * The offset for `chunk_id` N is therefore:
 *   `sizeof(FileSuperBlock) + (N * FRASE_FILE_CHUNK_SECTION_SIZE)`
 *
 * @param chunk_id The logical ID of the chunk.
 * @return The byte offset of the chunk within the file.
*/

static uint64_t file_io_chunk_offset(uint32_t chunk_id)
{
    return sizeof(FileSuperBlock)
         + ((uint64_t)chunk_id * (uint64_t)FRASE_FILE_CHUNK_SECTION_SIZE);
}


/*
 * file_io_seek_to_chunk
 *
 * Moves the file cursor (seek pointer) to the beginning of the specified
 * chunk section within the file. This uses `file_io_chunk_offset` to
 * determine the target position.
 *
 * @param file The file pointer to seek within.
 * @param chunk_id The logical ID of the chunk to seek to.
 * @return FILE_IO_OK on success, or an error code on failure.
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