#include "engine.h"
#include "file_io.h"

#include <stdlib.h>
#include <string.h>

#define ENGINE_MIN_GPA 0.0f
#define ENGINE_MAX_GPA 10.0f
#define ENGINE_INITIAL_CHUNK_ARRAY_CAPACITY 4u

typedef struct EngineAppendSnapshot {
    size_t chunk_count;
    size_t total_used_slots;
    size_t total_tombstones;
    size_t tail_used_slots;
    size_t tail_tombstones;
    bool   tail_dirty;
    bool   had_tail;
} EngineAppendSnapshot;

/**
 * @brief Ensures the engine is in the `ENGINE_STATE_BOOTED` state.
 * @param engine A pointer to the `Engine` instance.
 * @return       `ENGINE_OK` if booted, `ENGINE_ERR_INVALID_STATE` otherwise.
 */
static EngineError engine_require_booted(const Engine *engine);

/**
 * @brief Stores the provided file path in the engine's `backing_path` buffer.
 * @param engine A pointer to the `Engine` instance.
 * @param path   The file path to store.
 * @return       `ENGINE_OK` on success, `ENGINE_ERR_INVALID_STATE` if path is too long.
 */
static EngineError engine_store_path(Engine *engine, const char *path);

/**
 * @brief Checks if the database file needs compaction and performs it if necessary.
 * @param path The file path to the database backing file.
 * @return     `ENGINE_OK` on success (compaction performed or not needed),
 *             `ENGINE_ERR_COMPACTION_FAILED` or other `EngineError` on failure.
 */
static EngineError engine_compact_if_needed(const char *path);

/**
 * @brief Initializes the engine's runtime components (arenas, radix, AVL).
 * @param engine A pointer to the `Engine` instance.
 * @return       `ENGINE_OK` on success, or an `EngineError` code on failure.
 */
static EngineError engine_initialize_runtime(Engine *engine);

/**
 * @brief Hydrates the radix and AVL indexes from the loaded chunk data.
 * @param engine A pointer to the `Engine` instance.
 * @return       `ENGINE_OK` on success, or an `EngineError` code on failure.
 */
static EngineError engine_hydrate_indexes(Engine *engine);

/**
 * @brief Builds an array of `AVLEntry` structures from the engine's chunks.
 * @param engine       A pointer to the `Engine` instance.
 * @param entries      A pointer to the `AVLEntry` array to populate.
 * @param expected_count The expected number of active records.
 * @return             `ENGINE_OK` on success, or an `EngineError` code on failure.
 */
static EngineError engine_build_avl_entries(Engine *engine, AVLEntry *entries, size_t expected_count);

/**
 * @brief Validates a student ID.
 * @param id The ID to validate.
 * @return   `ENGINE_OK` if valid, `ENGINE_ERR_INVALID_ID` otherwise.
 */
static EngineError engine_validate_id(uint32_t id);

/**
 * @brief Validates a GPA value.
 * @param gpa The GPA to validate.
 * @return    `ENGINE_OK` if valid, `ENGINE_ERR_INVALID_GPA` otherwise.
 */
static EngineError engine_validate_gpa(float gpa);

/**
 * @brief Validates a student name buffer.
 * @param name The name string to validate.
 * @return     `ENGINE_OK` if valid, `ENGINE_ERR_INVALID_NAME` otherwise.
 */
static EngineError engine_validate_name_buffer(const char *name);

/**
 * @brief Validates a single `StudentRecord`.
 * @param record A pointer to the `StudentRecord` to validate.
 * @return       `ENGINE_OK` if valid, or an `EngineError` code on failure.
 */
static EngineError engine_validate_record(const StudentRecord *record);

/**
 * @brief Validates a batch of `StudentRecord`s, checking for duplicates and validity.
 * @param engine  A pointer to the `Engine` instance.
 * @param records A pointer to the array of `StudentRecord`s.
 * @param count   The number of records in the batch.
 * @return        `ENGINE_OK` if valid, or an `EngineError` code on failure.
 */
static EngineError engine_validate_batch(Engine *engine, const StudentRecord *records, size_t count);

/**
 * @brief Resolves a `RecordLocation` to a direct pointer to the `StudentRecord`.
 * @param engine   A pointer to the `Engine` instance.
 * @param location The `RecordLocation` to resolve.
 * @return         A pointer to the `StudentRecord`, or `NULL` if the location is invalid.
 */
static StudentRecord *engine_resolve_location(Engine *engine, RecordLocation location);

/**
 * @brief Marks the chunk containing the given `RecordLocation` as dirty.
 * @param engine   A pointer to the `Engine` instance.
 * @param location The `RecordLocation` whose chunk should be marked dirty.
 */
static void engine_mark_location_dirty(Engine *engine, RecordLocation location);

static void engine_take_append_snapshot(const Engine *engine, EngineAppendSnapshot *snapshot);
static void engine_rollback_append(Engine *engine, const EngineAppendSnapshot *snapshot);
static EngineError engine_ensure_chunk_array_capacity(ChunkManager *manager, size_t required_capacity);
static EngineError engine_create_empty_tail_chunk(ChunkManager *manager);
static EngineError engine_prepare_insert_tail(Engine *engine);
static EngineError engine_append_batch_to_chunks(Engine *engine, const StudentRecord *records, size_t count, RecordSpan *out_span);

/**
 * @brief Calculates the `RecordLocation` for a specific offset within a `RecordSpan`.
 * @param span         A pointer to the `RecordSpan`.
 * @param offset       The zero-based offset within the span.
 * @param out_location A pointer to a `RecordLocation` to store the calculated location.
 */
static void engine_span_location(const RecordSpan *span, size_t offset, RecordLocation *out_location);

/**
 * @brief Inserts all records within a `RecordSpan` into the radix and AVL indexes.
 * @param engine A pointer to the `Engine` instance.
 * @param span   A pointer to the `RecordSpan` containing the records to index.
 * @return       `ENGINE_OK` on success, or an `EngineError` code on failure.
 */
static EngineError engine_insert_span_into_indexes(Engine *engine, const RecordSpan *span);

/**
 * @brief Rolls back the indexing of records within a `RecordSpan` up to a certain count.
 * @param engine        A pointer to the `Engine` instance.
 * @param span          A pointer to the `RecordSpan`.
 * @param indexed_count The number of records from the start of the span that were indexed.
 */
static void engine_rollback_indexed_span(Engine *engine, const RecordSpan *span, size_t indexed_count);

/**
 * @brief Frees all dynamically allocated `StudentRecord` arrays within the `ChunkManager`.
 * @param manager A pointer to the `ChunkManager`.
 */
static void engine_free_chunks(ChunkManager *manager);

/**
 * @brief Destroys all runtime components of the engine (arenas, indexes, chunks).
 * @param engine A pointer to the `Engine` instance.
 */
static void engine_destroy_runtime(Engine *engine);

/**
 * @brief Converts a `FileIoError` into an `EngineError`.
 * @param error The `FileIoError` to convert.
 * @return      The corresponding `EngineError`.
 */
static EngineError engine_from_file_error(FileIoError error);

/**
 * @brief Converts an `ArenaError` into an `EngineError`.
 * @param error The `ArenaError` to convert.
 * @return      The corresponding `EngineError`.
 */
static EngineError engine_from_arena_error(ArenaError error);

/**
 * @brief Converts a `RadixError` into an `EngineError`.
 * @param error The `RadixError` to convert.
 * @return      The corresponding `EngineError`.
 */
static EngineError engine_from_radix_error(RadixError error);

/**
 * @brief Converts an `AvlError` into an `EngineError`.
 * @param error The `AvlError` to convert.
 * @return      The corresponding `EngineError`.
 */
static EngineError engine_from_avl_error(AvlError error);


/**
 * @brief Boots the engine from a backing file, initializing all components.
 *
 * This function performs the following high-level steps:
 *   - Stores the provided file path.
 *   - Checks if boot-time compaction is needed and performs it if so.
 *   - Loads chunk data from the backing file into memory.
 *   - Initializes the radix and AVL arenas.
 *   - Initializes the radix and AVL indexes.
 *   - Hydrates the radix index from live records in chunks.
 *   - Builds the AVL index from a temporary array of `AVLEntry` values.
 *
 * Upon successful completion, the engine transitions to `ENGINE_STATE_BOOTED`.
 * @param engine A pointer to the `Engine` instance to boot.
 * @param path   The file path to the database backing file.
 * @return       `ENGINE_OK` on success, or an `EngineError` code on failure.
 */
EngineError engine_boot(Engine *engine, const char *path)
{
    EngineError engine_error;
    FileIoError file_error;

    if (engine == NULL || path == NULL) {
        return ENGINE_ERR_NULL_ARGUMENT;
    }

    memset(engine, 0, sizeof(*engine));
    engine->state = ENGINE_STATE_CREATED;

    engine_error = engine_store_path(engine, path);

    if (engine_error != ENGINE_OK) {
        return engine_error;
    }

    engine_error = engine_compact_if_needed(engine->backing_path);

    if (engine_error != ENGINE_OK) {
        return engine_error;
    }

    file_error = file_io_load(engine->backing_path, &engine->chunks);

    if (file_error != FILE_IO_OK) {
        engine_destroy_runtime(engine);
        return engine_from_file_error(file_error);
    }

    engine_error = engine_initialize_runtime(engine);

    if (engine_error != ENGINE_OK) {
        engine_destroy_runtime(engine);
        return engine_error;
    }

    engine_error = engine_hydrate_indexes(engine);

    if (engine_error != ENGINE_OK) {
        engine_destroy_runtime(engine);
        return engine_error;
    }

    engine->state = ENGINE_STATE_BOOTED;

    return ENGINE_OK;
}


/**
 * @brief Saves all dirty (modified) chunks to the backing file.
 *
 * This operation does not affect the engine's runtime state; the engine remains
 * fully functional after a successful save.
 * @param engine A pointer to the `Engine` instance.
 * @return       `ENGINE_OK` on success, or an `EngineError` code on failure.
 */
EngineError engine_save(Engine *engine)
{
    FileIoError file_error;
    EngineError engine_error;

    engine_error = engine_require_booted(engine);

    if (engine_error != ENGINE_OK) {
        return engine_error;
    }

    file_error = file_io_save(engine->backing_path, &engine->chunks);

    if (file_error != FILE_IO_OK) {
        return engine_from_file_error(file_error);
    }

    return ENGINE_OK;
}


/**
 * @brief Tears down all runtime memory and resources associated with the engine.
 *
 * If `save_before_shutdown` is true, the engine attempts to save any pending
 * changes to disk before freeing memory. This destroys all in-memory data
 * structures (chunks, arenas, indexes).
 * After this call, the engine's state transitions to `ENGINE_STATE_SHUTDOWN`.
 * @param engine             A pointer to the `Engine` instance.
 * @param save_before_shutdown If true, a save operation is performed before shutdown.
 * @return                   `ENGINE_OK` on success, or an `EngineError` code on failure.
 */
EngineError engine_shutdown(Engine *engine, bool save_before_shutdown)
{
    EngineError engine_error;

    if (engine == NULL) {
        return ENGINE_ERR_NULL_ARGUMENT;
    }

    if (engine->state == ENGINE_STATE_SHUTDOWN) {
        return ENGINE_OK;
    }

    if (engine->state != ENGINE_STATE_BOOTED) {
        engine_destroy_runtime(engine);
        engine->state = ENGINE_STATE_SHUTDOWN;
        return ENGINE_OK;
    }

    if (save_before_shutdown) {
        engine_error = engine_save(engine);

        if (engine_error != ENGINE_OK) {
            return engine_error;
        }
    }

    engine_destroy_runtime(engine);
    engine->state = ENGINE_STATE_SHUTDOWN;

    return ENGINE_OK;
}


/**
 * @brief Inserts a single `StudentRecord` into the database.
 *
 * This function performs necessary validations (e.g., unique ID, valid GPA)
 * and uses the batch insertion path internally.
 * @param engine A pointer to the `Engine` instance.
 * @param record The `StudentRecord` to insert.
 * @return       `ENGINE_OK` on success, or an `EngineError` code on failure.
 */
EngineError engine_insert_one(Engine *engine, StudentRecord record)
{
    return engine_insert_batch(engine, &record, 1u);
}


/**
 * @brief Inserts a batch of `StudentRecord` values into the database.
 *
 * This function validates the entire batch, checks for duplicate IDs,
 * appends records to chunks, and inserts them into the radix and AVL indexes.
 * The operation is atomic: either the entire batch commits, or it is rolled back.
 * @param engine  A pointer to the `Engine` instance.
 * @param records A pointer to an array of `StudentRecord`s to insert.
 * @param count   The number of records in the `records` array.
 * @return        `ENGINE_OK` on success, or an `EngineError` code on failure.
 */
EngineError engine_insert_batch(Engine *engine, const StudentRecord *records, size_t count)
{
    EngineError engine_error;
    EngineAppendSnapshot snapshot;
    RecordSpan span;

    engine_error = engine_require_booted(engine);

    if (engine_error != ENGINE_OK) {
        return engine_error;
    }

    if (count == 0) {
        return ENGINE_OK;
    }

    if (records == NULL) {
        return ENGINE_ERR_NULL_ARGUMENT;
    }

    engine_error = engine_validate_batch(engine, records, count);

    if (engine_error != ENGINE_OK) {
        return engine_error;
    }

    engine_take_append_snapshot(engine, &snapshot);

    engine_error = engine_append_batch_to_chunks(engine, records, count, &span);

    if (engine_error != ENGINE_OK) {
        engine_rollback_append(engine, &snapshot);
        return engine_error;
    }

    engine_error = engine_insert_span_into_indexes(engine, &span);

    if (engine_error != ENGINE_OK) {
        engine_rollback_append(engine, &snapshot);
        return engine_error;
    }

    return ENGINE_OK;
}


/**
 * @brief Searches for a `StudentRecord` by its unique ID.
 *
 * On success, `out_result` is populated with the record's virtual location
 * and a direct pointer to the `StudentRecord` in memory.
 * @param engine     A pointer to the `Engine` instance.
 * @param id         The unique ID of the student record to find.
 * @param out_result A pointer to an `EngineSearchResult` to store the result.
 * @return           `ENGINE_OK` on success, `ENGINE_ERR_NOT_FOUND` if the ID is not found,
 *                   or an `EngineError` code on other failures.
 */
EngineError engine_find_by_id(Engine *engine, uint32_t id, EngineSearchResult *out_result)
{
    EngineError engine_error;
    RadixError radix_error;
    RecordLocation location;
    StudentRecord *record;

    engine_error = engine_require_booted(engine);

    if (engine_error != ENGINE_OK) {
        return engine_error;
    }

    if (out_result == NULL) {
        return ENGINE_ERR_NULL_ARGUMENT;
    }

    engine_error = engine_validate_id(id);

    if (engine_error != ENGINE_OK) {
        return engine_error;
    }

    radix_error = radix_find(&engine->radix, id, &location);

    if (radix_error != RADIX_OK) {
        return engine_from_radix_error(radix_error);
    }

    record = engine_resolve_location(engine, location);

    if (record == NULL || record->id == FRASE_TOMBSTONE_ID) {
        return ENGINE_ERR_INVALID_STATE;
    }

    out_result->location = location;
    out_result->record = record;

    return ENGINE_OK;
}


/**
 * @brief Deletes a `StudentRecord` from the database by its ID.
 *
 * This operation logically deletes the record by marking it as a tombstone
 * and removing it from both the radix and AVL indexes.
 * @param engine A pointer to the `Engine` instance.
 * @param id     The ID of the student record to delete.
 * @return       `ENGINE_OK` on success, `ENGINE_ERR_NOT_FOUND` if the ID is not found,
 *               or an `EngineError` code on other failures.
 */
EngineError engine_delete_by_id(Engine *engine, uint32_t id)
{
    EngineError engine_error;
    RadixError radix_error;
    AvlError avl_error;
    RecordLocation location;
    StudentRecord *record;
    float old_gpa;

    engine_error = engine_require_booted(engine);

    if (engine_error != ENGINE_OK) {
        return engine_error;
    }

    engine_error = engine_validate_id(id);

    if (engine_error != ENGINE_OK) {
        return engine_error;
    }

    radix_error = radix_find(&engine->radix, id, &location);

    if (radix_error != RADIX_OK) {
        return engine_from_radix_error(radix_error);
    }

    record = engine_resolve_location(engine, location);

    if (record == NULL || record->id == FRASE_TOMBSTONE_ID) {
        return ENGINE_ERR_INVALID_STATE;
    }

    old_gpa = record->gpa;

    avl_error = avl_delete(&engine->avl, old_gpa, id);

    if (avl_error != AVL_OK) {
        return engine_from_avl_error(avl_error);
    }

    radix_error = radix_delete(&engine->radix, id);

    if (radix_error != RADIX_OK) {
        AVLEntry rollback_entry;

        rollback_entry.gpa = old_gpa;
        rollback_entry.id = id;
        rollback_entry.location = location;

        (void)avl_insert(&engine->avl, rollback_entry);

        return engine_from_radix_error(radix_error);
    }

    record->id = FRASE_TOMBSTONE_ID;

    engine->chunks.chunks[location.chunk_id].tombstones++;
    engine->chunks.total_tombstones++;
    engine_mark_location_dirty(engine, location);

    return ENGINE_OK;
}


/**
 * @brief Updates the name of a `StudentRecord` identified by its ID.
 *
 * This function only modifies the name field; the ID remains immutable,
 * and the GPA index is not affected.
 * @param engine   A pointer to the `Engine` instance.
 * @param id       The ID of the student record to update.
 * @param new_name The new name for the student.
 * @return         `ENGINE_OK` on success, `ENGINE_ERR_NOT_FOUND` if the ID is not found,
 *                 or an `EngineError` code on other failures.
 */
EngineError engine_update_name(Engine *engine, uint32_t id, const char *new_name)
{
    EngineError engine_error;
    EngineSearchResult result;
    size_t name_length;

    engine_error = engine_require_booted(engine);

    if (engine_error != ENGINE_OK) {
        return engine_error;
    }

    engine_error = engine_validate_name_buffer(new_name);

    if (engine_error != ENGINE_OK) {
        return engine_error;
    }

    engine_error = engine_find_by_id(engine, id, &result);

    if (engine_error != ENGINE_OK) {
        return engine_error;
    }

    name_length = strlen(new_name);

    memset(result.record->name, 0, FRASE_STUDENT_NAME_SIZE);
    memcpy(result.record->name, new_name, name_length);

    engine_mark_location_dirty(engine, result.location);

    return ENGINE_OK;
}


/**
 * @brief Updates the GPA of a `StudentRecord` identified by its ID.
 *
 * This operation involves deleting the old GPA entry from the AVL index and
 * inserting a new one. It includes rollback logic if the new insertion fails.
 * @param engine  A pointer to the `Engine` instance.
 * @param id      The ID of the student record to update.
 * @param new_gpa The new GPA for the student.
 * @return        `ENGINE_OK` on success, `ENGINE_ERR_NOT_FOUND` if the ID is not found,
 *                or an `EngineError` code on other failures.
 */
EngineError engine_update_gpa(Engine *engine, uint32_t id, float new_gpa)
{
    EngineError engine_error;
    EngineSearchResult result;
    AvlError avl_error;
    float old_gpa;
    AVLEntry old_entry;
    AVLEntry new_entry;

    engine_error = engine_require_booted(engine);

    if (engine_error != ENGINE_OK) {
        return engine_error;
    }

    engine_error = engine_validate_gpa(new_gpa);

    if (engine_error != ENGINE_OK) {
        return engine_error;
    }

    engine_error = engine_find_by_id(engine, id, &result);

    if (engine_error != ENGINE_OK) {
        return engine_error;
    }

    old_gpa = result.record->gpa;

    if (old_gpa == new_gpa) {
        return ENGINE_OK;
    }

    old_entry.gpa = old_gpa;
    old_entry.id = id;
    old_entry.location = result.location;

    new_entry.gpa = new_gpa;
    new_entry.id = id;
    new_entry.location = result.location;

    avl_error = avl_delete(&engine->avl, old_gpa, id);

    if (avl_error != AVL_OK) {
        return engine_from_avl_error(avl_error);
    }

    avl_error = avl_insert(&engine->avl, new_entry);

    if (avl_error != AVL_OK) {
        (void)avl_insert(&engine->avl, old_entry);
        return engine_from_avl_error(avl_error);
    }

    result.record->gpa = new_gpa;
    engine_mark_location_dirty(engine, result.location);

    return ENGINE_OK;
}


/**
 * @brief Counts the number of records whose GPA falls within a specified range.
 *
 * This is the first pass of a two-step range query. The returned count can be
 * used to pre-allocate an array for `engine_query_gpa_range`.
 * @param engine  A pointer to the `Engine` instance.
 * @param min_gpa The lower bound (inclusive) of the GPA range.
 * @param max_gpa The upper bound (inclusive) of the GPA range.
 * @return        The number of records found within the GPA range, or 0 on error.
 */
size_t engine_count_gpa_range(const Engine *engine, float min_gpa, float max_gpa)
{
    if (engine == NULL || engine->state != ENGINE_STATE_BOOTED) {
        return 0;
    }

    if (min_gpa > max_gpa) {
        return 0;
    }

    if (engine_validate_gpa(min_gpa) != ENGINE_OK) {
        return 0;
    }

    if (engine_validate_gpa(max_gpa) != ENGINE_OK) {
        return 0;
    }

    return avl_count_range(&engine->avl, min_gpa, max_gpa);
}


/**
 * @brief Retrieves `EngineSearchResult`s for records within a specified GPA range.
 *
 * This is the second pass of a two-step range query. It fills a caller-owned
 * array with results, sorted by GPA ascending, then ID ascending.
 * @param engine       A pointer to the `Engine` instance.
 * @param min_gpa      The lower bound (inclusive) of the GPA range.
 * @param max_gpa      The upper bound (inclusive) of the GPA range.
 * @param out_results  A pointer to an array to store the `EngineSearchResult`s.
 * @param out_capacity The maximum number of results `out_results` can hold.
 * @param out_count    A pointer to a `size_t` to store the actual number of results found.
 * @return             `ENGINE_OK` on success, `ENGINE_ERR_OUTPUT_CAPACITY` if the array is too small,
 *                     or an `EngineError` code on other failures.
 */
EngineError engine_query_gpa_range(
    Engine *engine,
    float min_gpa,
    float max_gpa,
    EngineSearchResult *out_results,
    size_t out_capacity,
    size_t *out_count
)
{
    EngineError engine_error;
    AvlError avl_error;
    RecordLocation *locations;
    size_t location_count;
    size_t index;

    engine_error = engine_require_booted(engine);

    if (engine_error != ENGINE_OK) {
        return engine_error;
    }

    if (out_count == NULL) {
        return ENGINE_ERR_NULL_ARGUMENT;
    }

    *out_count = 0;

    if (min_gpa > max_gpa) {
        return ENGINE_ERR_INVALID_GPA;
    }

    engine_error = engine_validate_gpa(min_gpa);

    if (engine_error != ENGINE_OK) {
        return engine_error;
    }

    engine_error = engine_validate_gpa(max_gpa);

    if (engine_error != ENGINE_OK) {
        return engine_error;
    }

    location_count = avl_count_range(&engine->avl, min_gpa, max_gpa);

    if (location_count == 0) {
        return ENGINE_OK;
    }

    if (out_results == NULL) {
        return ENGINE_ERR_NULL_ARGUMENT;
    }

    if (out_capacity < location_count) {
        return ENGINE_ERR_OUTPUT_CAPACITY;
    }

    locations = malloc(location_count * sizeof(*locations));

    if (locations == NULL) {
        return ENGINE_ERR_ALLOCATION_FAILED;
    }

    avl_error = avl_range_query(
        &engine->avl,
        min_gpa,
        max_gpa,
        locations,
        location_count,
        &location_count
    );

    if (avl_error != AVL_OK) {
        free(locations);
        return engine_from_avl_error(avl_error);
    }

    for (index = 0; index < location_count; index++) {
        StudentRecord *record;

        record = engine_resolve_location(engine, locations[index]);

        if (record == NULL || record->id == FRASE_TOMBSTONE_ID) {
            free(locations);
            return ENGINE_ERR_INVALID_STATE;
        }

        out_results[index].location = locations[index];
        out_results[index].record = record;
    }

    *out_count = location_count;

    free(locations);

    return ENGINE_OK;
}


/**
 * @brief Converts an `EngineError` enumeration value into a human-readable string.
 *
 * @param error The `EngineError` code to convert.
 * @return      A constant string literal describing the error.
 */
const char *engine_error_string(EngineError error)
{
    switch (error) {
        case ENGINE_OK:
            return "engine: ok";
        case ENGINE_ERR_NULL_ARGUMENT:
            return "engine: null argument";
        case ENGINE_ERR_INVALID_STATE:
            return "engine: invalid state";
        case ENGINE_ERR_INVALID_ID:
            return "engine: invalid id";
        case ENGINE_ERR_INVALID_GPA:
            return "engine: invalid gpa";
        case ENGINE_ERR_INVALID_NAME:
            return "engine: invalid name";
        case ENGINE_ERR_DUPLICATE_ID:
            return "engine: duplicate id";
        case ENGINE_ERR_NOT_FOUND:
            return "engine: record not found";
        case ENGINE_ERR_ALLOCATION_FAILED:
            return "engine: allocation failed";
        case ENGINE_ERR_OUTPUT_CAPACITY:
            return "engine: output capacity too small";
        case ENGINE_ERR_FILE_IO:
            return "engine: file I/O failure";
        case ENGINE_ERR_ARENA:
            return "engine: arena failure";
        case ENGINE_ERR_RADIX:
            return "engine: radix failure";
        case ENGINE_ERR_AVL:
            return "engine: AVL failure";
        case ENGINE_ERR_COMPACTION_FAILED:
            return "engine: compaction failed";
        default:
            return "engine: unknown error";
    }
}

static EngineError engine_require_booted(const Engine *engine)
{
    if (engine == NULL) {
        return ENGINE_ERR_NULL_ARGUMENT;
    }

    if (engine->state != ENGINE_STATE_BOOTED) {
        return ENGINE_ERR_INVALID_STATE;
    }

    return ENGINE_OK;
}


/**
 * @brief Stores the provided file path in the engine's `backing_path` buffer.
 * @param engine A pointer to the `Engine` instance.
 * @param path   The file path to store.
 * @return       `ENGINE_OK` on success, `ENGINE_ERR_INVALID_STATE` if path is too long.
 */
static EngineError engine_store_path(Engine *engine, const char *path)
{
    size_t length;

    if (engine == NULL || path == NULL) {
        return ENGINE_ERR_NULL_ARGUMENT;
    }

    length = strlen(path);

    if (length == 0 || length >= FRASE_ENGINE_PATH_SIZE) {
        return ENGINE_ERR_INVALID_STATE;
    }

    memset(engine->backing_path, 0, sizeof(engine->backing_path));
    memcpy(engine->backing_path, path, length);

    return ENGINE_OK;
}


/**
 * @brief Checks if the database file needs compaction and performs it if necessary.
 * @param path The file path to the database backing file.
 * @return     `ENGINE_OK` on success (compaction performed or not needed),
 *             `ENGINE_ERR_COMPACTION_FAILED` or other `EngineError` on failure.
 */
static EngineError engine_compact_if_needed(const char *path)
{
    FileStats stats;
    FileIoError file_error;
    double ratio;

    if (path == NULL) {
        return ENGINE_ERR_NULL_ARGUMENT;
    }

    file_error = file_io_read_stats(path, &stats);

    if (file_error != FILE_IO_OK) {
        return engine_from_file_error(file_error);
    }

    if (stats.total_used_slots == 0) {
        return ENGINE_OK;
    }

    ratio =
        (double)stats.total_tombstones /
        (double)stats.total_used_slots;

    if (ratio < FRASE_COMPACTION_THRESHOLD) {
        return ENGINE_OK;
    }

    file_error = file_io_compact(path);

    if (file_error != FILE_IO_OK) {
        return ENGINE_ERR_COMPACTION_FAILED;
    }

    return ENGINE_OK;
}


/**
 * @brief Initializes the engine's runtime components (arenas, radix, AVL).
 *
 * This includes initializing the memory arenas and the radix and AVL index structures.
 * @param engine A pointer to the `Engine` instance.
 * @return       `ENGINE_OK` on success, or an `EngineError` code on failure.
 */
static EngineError engine_initialize_runtime(Engine *engine)
{
    ArenaError arena_error;
    RadixError radix_error;
    AvlError avl_error;

    if (engine == NULL) {
        return ENGINE_ERR_NULL_ARGUMENT;
    }

    arena_error = arena_init(&engine->radix_arena, FRASE_ARENA_DEFAULT_PAGE_SIZE);

    if (arena_error != ARENA_OK) {
        return engine_from_arena_error(arena_error);
    }

    arena_error = arena_init(&engine->avl_arena, FRASE_ARENA_DEFAULT_PAGE_SIZE);

    if (arena_error != ARENA_OK) {
        return engine_from_arena_error(arena_error);
    }

    radix_error = radix_init(&engine->radix, &engine->radix_arena);

    if (radix_error != RADIX_OK) {
        return engine_from_radix_error(radix_error);
    }

    avl_error = avl_init(&engine->avl, &engine->avl_arena);

    if (avl_error != AVL_OK) {
        return engine_from_avl_error(avl_error);
    }

    return ENGINE_OK;
}


/**
 * @brief Hydrates the radix and AVL indexes from the loaded chunk data.
 * @param engine A pointer to the `Engine` instance.
 * @return       `ENGINE_OK` on success, or an `EngineError` code on failure.
 */
static EngineError engine_hydrate_indexes(Engine *engine)
{
    AVLEntry *entries;
    size_t active_count;
    EngineError engine_error;
    AvlError avl_error;

    if (engine == NULL) {
        return ENGINE_ERR_NULL_ARGUMENT;
    }

    if (engine->chunks.total_tombstones > engine->chunks.total_used_slots) {
        return ENGINE_ERR_INVALID_STATE;
    }

    active_count =
        engine->chunks.total_used_slots -
        engine->chunks.total_tombstones;

    if (active_count == 0) {
        return ENGINE_OK;
    }

    entries = malloc(active_count * sizeof(*entries));

    if (entries == NULL) {
        return ENGINE_ERR_ALLOCATION_FAILED;
    }

    engine_error = engine_build_avl_entries(engine, entries, active_count);

    if (engine_error != ENGINE_OK) {
        free(entries);
        return engine_error;
    }

    avl_error = avl_build_from_entries(&engine->avl, entries, active_count);

    free(entries);

    if (avl_error != AVL_OK) {
        return engine_from_avl_error(avl_error);
    }

    return ENGINE_OK;
}


/**
 * @brief Builds an array of `AVLEntry` structures from the engine's chunks.
 *
 * This function iterates through all records in the chunks and populates the `entries` array.
 * @param engine       A pointer to the `Engine` instance.
 * @param entries      A pointer to the `AVLEntry` array to populate.
 * @param expected_count The expected number of active records.
 * @return             `ENGINE_OK` on success, or an `EngineError` code on failure.
 */
static EngineError engine_build_avl_entries(Engine *engine, AVLEntry *entries, size_t expected_count)
{
    size_t entry_count;
    size_t chunk_index;

    if (engine == NULL || entries == NULL) {
        return ENGINE_ERR_NULL_ARGUMENT;
    }

    entry_count = 0;

    for (chunk_index = 0; chunk_index < engine->chunks.chunk_count; chunk_index++) {
        Chunk *chunk;
        size_t slot;

        chunk = &engine->chunks.chunks[chunk_index];

        if (chunk->records == NULL) {
            return ENGINE_ERR_INVALID_STATE;
        }

        if (chunk->used_slots > FRASE_CHUNK_RECORD_CAPACITY) {
            return ENGINE_ERR_INVALID_STATE;
        }

        for (slot = 0; slot < chunk->used_slots; slot++) {
            StudentRecord *record;
            RecordLocation location;
            RadixError radix_error;

            record = &chunk->records[slot];

            if (record->id == FRASE_TOMBSTONE_ID) {
                continue;
            }

            if (entry_count >= expected_count) {
                return ENGINE_ERR_INVALID_STATE;
            }

            if (engine_validate_record(record) != ENGINE_OK) {
                return ENGINE_ERR_INVALID_STATE;
            }

            location.chunk_id = (uint32_t)chunk_index;
            location.slot_index = (uint32_t)slot;

            radix_error = radix_insert(&engine->radix, record->id, location);

            if (radix_error != RADIX_OK) {
                return engine_from_radix_error(radix_error);
            }

            entries[entry_count].gpa = record->gpa;
            entries[entry_count].id = record->id;
            entries[entry_count].location = location;
            entry_count++;
        }
    }

    if (entry_count != expected_count) {
        return ENGINE_ERR_INVALID_STATE;
    }

    return ENGINE_OK;
}


/**
 * @brief Validates a student ID.
 * @param id The ID to validate.
 * @return   `ENGINE_OK` if valid, `ENGINE_ERR_INVALID_ID` otherwise.
 */
static EngineError engine_validate_id(uint32_t id)
{
    if (id == FRASE_TOMBSTONE_ID) {
        return ENGINE_ERR_INVALID_ID;
    }

    return ENGINE_OK;
}


/**
 * @brief Validates a GPA value.
 * @param gpa The GPA to validate.
 * @return    `ENGINE_OK` if valid, `ENGINE_ERR_INVALID_GPA` otherwise.
 */
static EngineError engine_validate_gpa(float gpa)
{
    if (gpa < ENGINE_MIN_GPA || gpa > ENGINE_MAX_GPA) {
        return ENGINE_ERR_INVALID_GPA;
    }

    return ENGINE_OK;
}


/**
 * @brief Validates a student name buffer.
 * @param name The name string to validate.
 * @return     `ENGINE_OK` if valid, `ENGINE_ERR_INVALID_NAME` otherwise.
 */
static EngineError engine_validate_name_buffer(const char *name)
{
    size_t length;

    if (name == NULL) {
        return ENGINE_ERR_NULL_ARGUMENT;
    }

    length = 0;

    while (length < FRASE_STUDENT_NAME_SIZE && name[length] != '\0') {
        length++;
    }

    if (length == 0 || length >= FRASE_STUDENT_NAME_SIZE) {
        return ENGINE_ERR_INVALID_NAME;
    }

    return ENGINE_OK;
}


/**
 * @brief Validates a single `StudentRecord`.
 * @param record A pointer to the `StudentRecord` to validate.
 * @return       `ENGINE_OK` if valid, or an `EngineError` code on failure.
 */
static EngineError engine_validate_record(const StudentRecord *record)
{
    EngineError engine_error;

    if (record == NULL) {
        return ENGINE_ERR_NULL_ARGUMENT;
    }

    engine_error = engine_validate_id(record->id);

    if (engine_error != ENGINE_OK) {
        return engine_error;
    }

    engine_error = engine_validate_gpa(record->gpa);

    if (engine_error != ENGINE_OK) {
        return engine_error;
    }

    return engine_validate_name_buffer(record->name);
}


/**
 * @brief Validates a batch of `StudentRecord`s, checking for duplicates and validity.
 * @param engine  A pointer to the `Engine` instance.
 * @param records A pointer to the array of `StudentRecord`s.
 * @param count   The number of records in the batch.
 * @return        `ENGINE_OK` if valid, or an `EngineError` code on failure.
 */
static EngineError engine_validate_batch(Engine *engine, const StudentRecord *records, size_t count)
{
    size_t i;

    if (engine == NULL || records == NULL) {
        return ENGINE_ERR_NULL_ARGUMENT;
    }

    for (i = 0; i < count; i++) {
        EngineError engine_error;
        RadixError radix_error;
        RecordLocation unused_location;
        size_t j;

        engine_error = engine_validate_record(&records[i]);

        if (engine_error != ENGINE_OK) {
            return engine_error;
        }

        for (j = i + 1; j < count; j++) {
            if (records[i].id == records[j].id) {
                return ENGINE_ERR_DUPLICATE_ID;
            }
        }

        radix_error = radix_find(&engine->radix, records[i].id, &unused_location);

        if (radix_error == RADIX_OK) {
            return ENGINE_ERR_DUPLICATE_ID;
        }

        if (radix_error != RADIX_ERR_NOT_FOUND) {
            return engine_from_radix_error(radix_error);
        }
    }

    return ENGINE_OK;
}


/**
 * @brief Resolves a `RecordLocation` to a direct pointer to the `StudentRecord`.
 * @param engine   A pointer to the `Engine` instance.
 * @param location The `RecordLocation` to resolve.
 * @return         A pointer to the `StudentRecord`, or `NULL` if the location is invalid.
 */
static StudentRecord *engine_resolve_location(Engine *engine, RecordLocation location)
{
    Chunk *chunk;

    if (engine == NULL) {
        return NULL;
    }

    if ((size_t)location.chunk_id >= engine->chunks.chunk_count) {
        return NULL;
    }

    chunk = &engine->chunks.chunks[location.chunk_id];

    if (chunk->records == NULL) {
        return NULL;
    }

    if ((size_t)location.slot_index >= chunk->used_slots) {
        return NULL;
    }

    if (location.slot_index >= FRASE_CHUNK_RECORD_CAPACITY) {
        return NULL;
    }

    return &chunk->records[location.slot_index];
}


/**
 * @brief Marks the chunk containing the given `RecordLocation` as dirty.
 * @param engine   A pointer to the `Engine` instance.
 * @param location The `RecordLocation` whose chunk should be marked dirty.
 */
static void engine_mark_location_dirty(Engine *engine, RecordLocation location)
{
    if (engine == NULL) {
        return;
    }

    if ((size_t)location.chunk_id >= engine->chunks.chunk_count) {
        return;
    }

    engine->chunks.chunks[location.chunk_id].dirty = true;
}


/**
 * @brief Takes a snapshot of the engine's chunk state before an append operation.
 * @param engine   A pointer to the `Engine` instance.
 * @param snapshot A pointer to an `EngineAppendSnapshot` to store the current state.
 */
static void engine_take_append_snapshot(const Engine *engine, EngineAppendSnapshot *snapshot)
{
    const ChunkManager *manager;
    const Chunk *tail;

    if (engine == NULL || snapshot == NULL) {
        return;
    }

    manager = &engine->chunks;

    memset(snapshot, 0, sizeof(*snapshot));

    snapshot->chunk_count = manager->chunk_count;
    snapshot->total_used_slots = manager->total_used_slots;
    snapshot->total_tombstones = manager->total_tombstones;
    snapshot->had_tail = manager->chunk_count > 0;

    if (snapshot->had_tail) {
        tail = &manager->chunks[manager->chunk_count - 1u];
        snapshot->tail_used_slots = tail->used_slots;
        snapshot->tail_tombstones = tail->tombstones;
        snapshot->tail_dirty = tail->dirty;
    }
}


/**
 * @brief Rolls back an append operation to a previously taken snapshot.
 * @param engine   A pointer to the `Engine` instance.
 * @param snapshot A pointer to the `EngineAppendSnapshot` representing the state to roll back to.
 */
static void engine_rollback_append(Engine *engine, const EngineAppendSnapshot *snapshot)
{
    ChunkManager *manager;
    size_t index;

    if (engine == NULL || snapshot == NULL) {
        return;
    }

    manager = &engine->chunks;

    for (index = snapshot->chunk_count; index < manager->chunk_count; index++) {
        free(manager->chunks[index].records);
        memset(&manager->chunks[index], 0, sizeof(manager->chunks[index]));
    }

    manager->chunk_count = snapshot->chunk_count;
    manager->total_used_slots = snapshot->total_used_slots;
    manager->total_tombstones = snapshot->total_tombstones;

    if (snapshot->had_tail && manager->chunk_count > 0) {
        Chunk *tail;

        tail = &manager->chunks[manager->chunk_count - 1u];
        tail->used_slots = snapshot->tail_used_slots;
        tail->tombstones = snapshot->tail_tombstones;
        tail->dirty = snapshot->tail_dirty;
    }
}


/**
 * @brief Ensures that the `ChunkManager`'s internal array of `Chunk`s has enough capacity.
 * @param manager           A pointer to the `ChunkManager`.
 * @param required_capacity The minimum capacity required.
 * @return                  `ENGINE_OK` on success, or `ENGINE_ERR_ALLOCATION_FAILED` if reallocation fails.
 */
static EngineError engine_ensure_chunk_array_capacity(ChunkManager *manager, size_t required_capacity)
{
    size_t new_capacity;
    Chunk *new_chunks;

    if (manager == NULL) {
        return ENGINE_ERR_NULL_ARGUMENT;
    }

    if (required_capacity <= manager->chunk_capacity) {
        return ENGINE_OK;
    }

    new_capacity = manager->chunk_capacity;

    if (new_capacity == 0) {
        new_capacity = ENGINE_INITIAL_CHUNK_ARRAY_CAPACITY;
    }

    while (new_capacity < required_capacity) {
        new_capacity *= 2u;
    }

    new_chunks = realloc(manager->chunks, new_capacity * sizeof(*new_chunks));

    if (new_chunks == NULL) {
        return ENGINE_ERR_ALLOCATION_FAILED;
    }

    memset(
        new_chunks + manager->chunk_capacity,
        0,
        (new_capacity - manager->chunk_capacity) * sizeof(*new_chunks)
    );

    manager->chunks = new_chunks;
    manager->chunk_capacity = new_capacity;

    return ENGINE_OK;
}


/**
 * @brief Creates and appends a new empty chunk to the `ChunkManager`.
 * @param manager A pointer to the `ChunkManager`.
 * @return        `ENGINE_OK` on success, or an `EngineError` code on failure.
 */
static EngineError engine_create_empty_tail_chunk(ChunkManager *manager)
{
    EngineError engine_error;
    Chunk *chunk;

    if (manager == NULL) {
        return ENGINE_ERR_NULL_ARGUMENT;
    }

    engine_error = engine_ensure_chunk_array_capacity(
        manager,
        manager->chunk_count + 1u
    );

    if (engine_error != ENGINE_OK) {
        return engine_error;
    }

    chunk = &manager->chunks[manager->chunk_count];
    memset(chunk, 0, sizeof(*chunk));

    chunk->records = calloc(FRASE_CHUNK_RECORD_CAPACITY, sizeof(StudentRecord));

    if (chunk->records == NULL) {
        return ENGINE_ERR_ALLOCATION_FAILED;
    }

    chunk->chunk_id = (uint32_t)manager->chunk_count;
    chunk->used_slots = 0;
    chunk->tombstones = 0;
    chunk->dirty = true;

    manager->chunk_count++;

    return ENGINE_OK;
}


/**
 * @brief Prepares the engine for inserting new records by ensuring a writable tail chunk exists.
 * @param engine A pointer to the `Engine` instance.
 * @return       `ENGINE_OK` on success, or an `EngineError` code on failure.
 */
static EngineError engine_prepare_insert_tail(Engine *engine)
{
    ChunkManager *manager;
    Chunk *tail;

    if (engine == NULL) {
        return ENGINE_ERR_NULL_ARGUMENT;
    }

    manager = &engine->chunks;

    if (manager->chunk_count == 0) {
        return engine_create_empty_tail_chunk(manager);
    }

    tail = &manager->chunks[manager->chunk_count - 1u];

    if (tail->used_slots >= FRASE_CHUNK_RECORD_CAPACITY) {
        return engine_create_empty_tail_chunk(manager);
    }

    return ENGINE_OK;
}


/**
 * @brief Appends a batch of `StudentRecord`s to the engine's chunks.
 * @param engine   A pointer to the `Engine` instance.
 * @param records  A pointer to the array of `StudentRecord`s to append.
 * @param count    The number of records in the batch.
 * @param out_span A pointer to a `RecordSpan` to store the location of the appended records.
 * @return         `ENGINE_OK` on success, or an `EngineError` code on failure.
 */
static EngineError engine_append_batch_to_chunks(Engine *engine, const StudentRecord *records, size_t count, RecordSpan *out_span)
{
    EngineError engine_error;
    size_t index;

    if (engine == NULL || records == NULL || out_span == NULL) {
        return ENGINE_ERR_NULL_ARGUMENT;
    }

    engine_error = engine_prepare_insert_tail(engine);

    if (engine_error != ENGINE_OK) {
        return engine_error;
    }

    out_span->start_chunk_id = (uint32_t)(engine->chunks.chunk_count - 1u);
    out_span->start_slot_index = (uint32_t)engine->chunks.chunks[engine->chunks.chunk_count - 1u].used_slots;
    out_span->record_count = count;

    for (index = 0; index < count; index++) {
        Chunk *tail;

        engine_error = engine_prepare_insert_tail(engine);

        if (engine_error != ENGINE_OK) {
            return engine_error;
        }

        tail = &engine->chunks.chunks[engine->chunks.chunk_count - 1u];

        tail->records[tail->used_slots] = records[index];
        tail->used_slots++;
        tail->dirty = true;

        engine->chunks.total_used_slots++;
    }

    return ENGINE_OK;
}


/**
 * @brief Calculates the `RecordLocation` for a specific offset within a `RecordSpan`.
 * @param span         A pointer to the `RecordSpan`.
 * @param offset       The zero-based offset within the span.
 * @param out_location A pointer to a `RecordLocation` to store the calculated location.
 */
static void engine_span_location(const RecordSpan *span, size_t offset, RecordLocation *out_location)
{
    size_t absolute_slot;

    if (span == NULL || out_location == NULL) {
        return;
    }

    absolute_slot = (size_t)span->start_slot_index + offset;

    out_location->chunk_id =
        span->start_chunk_id +
        (uint32_t)(absolute_slot / FRASE_CHUNK_RECORD_CAPACITY);

    out_location->slot_index =
        (uint32_t)(absolute_slot % FRASE_CHUNK_RECORD_CAPACITY);
}


/**
 * @brief Inserts all records within a `RecordSpan` into the radix and AVL indexes.
 * @param engine A pointer to the `Engine` instance.
 * @param span   A pointer to the `RecordSpan` containing the records to index.
 * @return       `ENGINE_OK` on success, or an `EngineError` code on failure.
 */
static EngineError engine_insert_span_into_indexes(Engine *engine, const RecordSpan *span)
{
    size_t indexed_count;
    size_t offset;

    if (engine == NULL || span == NULL) {
        return ENGINE_ERR_NULL_ARGUMENT;
    }

    indexed_count = 0;

    for (offset = 0; offset < span->record_count; offset++) {
        RecordLocation location;
        StudentRecord *record;
        RadixError radix_error;
        AvlError avl_error;
        AVLEntry entry;

        engine_span_location(span, offset, &location);
        record = engine_resolve_location(engine, location);

        if (record == NULL) {
            engine_rollback_indexed_span(engine, span, indexed_count);
            return ENGINE_ERR_INVALID_STATE;
        }

        radix_error = radix_insert(&engine->radix, record->id, location);

        if (radix_error != RADIX_OK) {
            engine_rollback_indexed_span(engine, span, indexed_count);
            return engine_from_radix_error(radix_error);
        }

        entry.gpa = record->gpa;
        entry.id = record->id;
        entry.location = location;

        avl_error = avl_insert(&engine->avl, entry);

        if (avl_error != AVL_OK) {
            (void)radix_delete(&engine->radix, record->id);
            engine_rollback_indexed_span(engine, span, indexed_count);
            return engine_from_avl_error(avl_error);
        }

        indexed_count++;
    }

    return ENGINE_OK;
}


/**
 * @brief Rolls back the indexing of records within a `RecordSpan` up to a certain count.
 * @param engine        A pointer to the `Engine` instance.
 * @param span          A pointer to the `RecordSpan`.
 * @param indexed_count The number of records from the start of the span that were indexed.
 */
static void engine_rollback_indexed_span(Engine *engine, const RecordSpan *span, size_t indexed_count)
{
    size_t offset;

    if (engine == NULL || span == NULL) {
        return;
    }

    for (offset = 0; offset < indexed_count; offset++) {
        RecordLocation location;
        StudentRecord *record;

        engine_span_location(span, offset, &location);
        record = engine_resolve_location(engine, location);

        if (record == NULL || record->id == FRASE_TOMBSTONE_ID) {
            continue;
        }

        (void)avl_delete(&engine->avl, record->gpa, record->id);
        (void)radix_delete(&engine->radix, record->id);
    }
}


/**
 * @brief Frees all dynamically allocated `StudentRecord` arrays within the `ChunkManager`.
 *
 * This function iterates through all chunks and frees their `records` arrays, then frees the `chunks` array itself.
 * @param manager A pointer to the `ChunkManager`.
 */
static void engine_free_chunks(ChunkManager *manager)
{
    size_t index;

    if (manager == NULL) {
        return;
    }

    for (index = 0; index < manager->chunk_count; index++) {
        free(manager->chunks[index].records);
        manager->chunks[index].records = NULL;
    }

    free(manager->chunks);
    memset(manager, 0, sizeof(*manager));
}


/**
 * @brief Ensures the engine is in the `ENGINE_STATE_BOOTED` state.
 * @param engine A pointer to the `Engine` instance.
 * @return       `ENGINE_OK` if booted, `ENGINE_ERR_INVALID_STATE` otherwise.
 */
static void engine_destroy_runtime(Engine *engine)
{
    if (engine == NULL) {
        return;
    }

    arena_destroy(&engine->radix_arena);
    arena_destroy(&engine->avl_arena);

    memset(&engine->radix, 0, sizeof(engine->radix));
    memset(&engine->avl, 0, sizeof(engine->avl));

    engine_free_chunks(&engine->chunks);
}


/**
 * @brief Converts a `FileIoError` into an `EngineError`.
 * @param error The `FileIoError` to convert.
 * @return      The corresponding `EngineError`.
 */
static EngineError engine_from_file_error(FileIoError error)
{
    if (error == FILE_IO_OK) {
        return ENGINE_OK;
    }

    if (error == FILE_IO_ERR_NULL_ARGUMENT) {
        return ENGINE_ERR_NULL_ARGUMENT;
    }

    if (error == FILE_IO_ERR_ALLOCATION_FAILED) {
        return ENGINE_ERR_ALLOCATION_FAILED;
    }

    return ENGINE_ERR_FILE_IO;
}


/**
 * @brief Converts an `ArenaError` into an `EngineError`.
 * @param error The `ArenaError` to convert.
 * @return      The corresponding `EngineError`.
 */
static EngineError engine_from_arena_error(ArenaError error)
{
    if (error == ARENA_OK) {
        return ENGINE_OK;
    }

    if (error == ARENA_ERR_NULL_ARGUMENT) {
        return ENGINE_ERR_NULL_ARGUMENT;
    }

    if (error == ARENA_ERR_ALLOCATION_FAILED) {
        return ENGINE_ERR_ALLOCATION_FAILED;
    }

    return ENGINE_ERR_ARENA;
}


/**
 * @brief Converts a `RadixError` into an `EngineError`.
 * @param error The `RadixError` to convert.
 * @return      The corresponding `EngineError`.
 */
static EngineError engine_from_radix_error(RadixError error)
{
    switch (error) {
        case RADIX_OK:
            return ENGINE_OK;
        case RADIX_ERR_NULL_ARGUMENT:
            return ENGINE_ERR_NULL_ARGUMENT;
        case RADIX_ERR_INVALID_ID:
            return ENGINE_ERR_INVALID_ID;
        case RADIX_ERR_ALLOCATION_FAILED:
            return ENGINE_ERR_ALLOCATION_FAILED;
        case RADIX_ERR_DUPLICATE_ID:
            return ENGINE_ERR_DUPLICATE_ID;
        case RADIX_ERR_NOT_FOUND:
            return ENGINE_ERR_NOT_FOUND;
        default:
            return ENGINE_ERR_RADIX;
    }
}


static EngineError engine_from_avl_error(AvlError error)
{
    switch (error) {
        case AVL_OK:
            return ENGINE_OK;
        case AVL_ERR_NULL_ARGUMENT:
            return ENGINE_ERR_NULL_ARGUMENT;
        case AVL_ERR_ALLOCATION_FAILED:
            return ENGINE_ERR_ALLOCATION_FAILED;
        case AVL_ERR_DUPLICATE_KEY:
            return ENGINE_ERR_DUPLICATE_ID;
        case AVL_ERR_NOT_FOUND:
            return ENGINE_ERR_NOT_FOUND;
        case AVL_ERR_INVALID_RANGE:
            return ENGINE_ERR_INVALID_GPA;
        case AVL_ERR_OUTPUT_CAPACITY:
            return ENGINE_ERR_OUTPUT_CAPACITY;
        default:
            return ENGINE_ERR_AVL;
    }
}