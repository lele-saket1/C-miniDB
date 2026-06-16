#ifndef FRASE_ENGINE_H
#define FRASE_ENGINE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "Chunk.h"
#include "arena.h"
#include "radix.h"
#include "AVL.h"
#include "frasetype.h"

/*
    FRASE Engine

    The engine is the coordinator layer.

    It owns:
        - the hydrated chunk system
        - the radix arena
        - the AVL arena
        - the radix index
        - the AVL index
        - the backing file path

    It does not parse shell commands.
    It does not directly interact with the user.
    It exposes clean operations that the shell/main layer can call.
*/

#define FRASE_ENGINE_PATH_SIZE 260u

/*
    Compaction threshold.

    If:

        total_tombstones / total_used_slots >= FRASE_COMPACTION_THRESHOLD

    then boot-time compaction may be triggered before RAM hydration.

    This is intentionally a simple fixed threshold for Version 1.
*/

#define FRASE_COMPACTION_THRESHOLD 0.30

/*
    EngineError

    Error enum used by the engine module.

    The engine wraps errors from file I/O, arena, radix, and AVL into
    engine-level failures.
*/

typedef enum EngineError {
    ENGINE_OK = 0,

    ENGINE_ERR_NULL_ARGUMENT,
    ENGINE_ERR_INVALID_STATE,
    ENGINE_ERR_INVALID_ID,
    ENGINE_ERR_INVALID_GPA,
    ENGINE_ERR_INVALID_NAME,
    ENGINE_ERR_DUPLICATE_ID,
    ENGINE_ERR_NOT_FOUND,
    ENGINE_ERR_ALLOCATION_FAILED,
    ENGINE_ERR_OUTPUT_CAPACITY,

    ENGINE_ERR_FILE_IO,
    ENGINE_ERR_ARENA,
    ENGINE_ERR_RADIX,
    ENGINE_ERR_AVL,
    ENGINE_ERR_COMPACTION_FAILED
} EngineError;

/*
    EngineState

    A small FSM-style state marker.

    CREATED:
        Struct exists but has not booted.

    BOOTED:
        Chunks, arenas, radix, and AVL are initialized.

    SHUTDOWN:
        Engine has been torn down.
*/

typedef enum EngineState {
    ENGINE_STATE_CREATED = 0,
    ENGINE_STATE_BOOTED,
    ENGINE_STATE_SHUTDOWN
} EngineState;

/*
    EngineUpdateField

    Used when the shell wants to update one field.

    ID is intentionally not included.
    Student IDs are immutable in Version 1.
*/

typedef enum EngineUpdateField {
    ENGINE_UPDATE_NAME = 0,
    ENGINE_UPDATE_GPA
} EngineUpdateField;

/*
    Engine

    Main engine object.

    chunks:
        Owns all chunk-resident StudentRecord arrays.

    radix_arena:
        Arena used by the radix index.

    avl_arena:
        Arena used by the AVL index.

    radix:
        Primary ID lookup index.

    avl:
        GPA range-query index.

    backing_path:
        File path used by file_io_load(), file_io_save(), and compaction.

    state:
        Tracks whether the engine is booted or shut down.
*/

typedef struct Engine {
    ChunkManager chunks;

    Arena radix_arena;
    Arena avl_arena;

    RadixIndex radix;
    AVLIndex   avl;

    char backing_path[FRASE_ENGINE_PATH_SIZE];

    EngineState state;
} Engine;

/*
    EngineSearchResult

    Returned by ID search.

    location:
        Virtual record location inside the chunk system.

    record:
        Direct pointer to the live StudentRecord.

    The pointer is convenient for display, but ownership remains with the
    chunk system. The caller must not free it.
*/

typedef struct EngineSearchResult {
    RecordLocation location;
    StudentRecord *record;
} EngineSearchResult;

/*
    engine_boot

    Boots the engine from a backing file.

    High-level behavior:
        - stores the file path
        - checks whether boot compaction is needed
        - loads chunks from disk
        - initializes arenas
        - initializes radix and AVL
        - hydrates radix from live chunk records
        - builds AVL from a temporary AVLEntry array

    After success, the engine enters ENGINE_STATE_BOOTED.
*/

EngineError engine_boot(Engine *engine, const char *path);

/*
    engine_save

    Saves dirty chunks to the backing file.

    This does not destroy runtime state.
    The engine remains usable after this function succeeds.
*/

/**
 * @brief Saves all dirty (modified) chunks to the backing file.
 *
 * This operation does not affect the engine's runtime state; the engine remains
 * fully functional after a successful save.
 * @param engine A pointer to the `Engine` instance.
 * @return       `ENGINE_OK` on success, or an `EngineError` code on failure.
 */
EngineError engine_save(Engine *engine);

/*
    engine_shutdown

    Tears down runtime memory.

    This destroys:
        - radix arena pages
        - AVL arena pages
        - chunk record arrays
        - chunk array

    After this call, the engine enters ENGINE_STATE_SHUTDOWN.
*/
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
EngineError engine_shutdown(Engine *engine, bool save_before_shutdown);

/*
    engine_insert_one
    Inserts one validated StudentRecord.
*/
/**
 * @brief Inserts a single `StudentRecord` into the database.
 *
 * This function performs necessary validations (e.g., unique ID, valid GPA)
 * and uses the batch insertion path internally.
 * @param engine A pointer to the `Engine` instance.
 * @param record The `StudentRecord` to insert.
 * @return       `ENGINE_OK` on success, or an `EngineError` code on failure.
 */
EngineError engine_insert_one(Engine *engine, StudentRecord record);

/*
    engine_insert_batch
    Inserts a batch of StudentRecord values.
*/
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
EngineError engine_insert_batch(
    Engine *engine,
    const StudentRecord *records,
    size_t count
);

/*
    engine_find_by_id
    Searches by immutable student ID.
*/
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
EngineError engine_find_by_id(
    Engine *engine,
    uint32_t id,
    EngineSearchResult *out_result
);

/*
    engine_delete_by_id
    Deletes a record by ID.
*/
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
EngineError engine_delete_by_id(Engine *engine, uint32_t id);

/*
    engine_update_name
    Updates only the student's name.
*/
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
EngineError engine_update_name(
    Engine *engine,
    uint32_t id,
    const char *new_name
);

/*
    engine_update_gpa

*/
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
EngineError engine_update_gpa(
    Engine *engine,
    uint32_t id,
    float new_gpa
);

/*
    engine_count_gpa_range
    First pass of GPA range query.
*/
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
size_t engine_count_gpa_range(
    const Engine *engine,
    float min_gpa,
    float max_gpa
);

/*
    engine_query_gpa_range
    Second pass of GPA range query.
*/
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
);

/*
    engine_error_string
    Converts EngineError values into readable strings.
*/
/**
 * @brief Converts an `EngineError` enumeration value into a human-readable string.
 *
 * @param error The `EngineError` code to convert.
 * @return      A constant string literal describing the error.
 */
const char *engine_error_string(EngineError error);

#endif