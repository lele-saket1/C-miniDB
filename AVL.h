#ifndef FRASE_AVL_H
#define FRASE_AVL_H

#include <stdint.h>
#include <stddef.h>

#include "arena.h"
#include "frasetype.h"

/*
 * FRASE AVL Index Module
 *
 * This module provides an in-memory AVL tree implementation to serve as
 * a secondary index for student records, primarily ordered by GPA.
 *
 * Key characteristics:
 *   - **Ordering**: Records are ordered by a composite key:
 *     1.  Primary key: `gpa` (ascending)
 *     2.  Tie-breaker key: `id` (ascending)
 *     The `id` tie-breaker is crucial because multiple students can have
 *     the same GPA, but student IDs are unique.
 *   - **Data Storage**: The AVL index does not store full `StudentRecord`
 *     objects. Instead, it stores copies of the key fields (`gpa`, `id`)
 *     and a `RecordLocation` pointer back to the actual record in the
 *     chunk system.
 *   - **Ownership**: The AVL index does not own the `StudentRecord` objects;
 *     their lifecycle is managed by the chunk system.
 *   - **Transience**: This is an in-memory index, rebuilt on application
 *     startup and discarded on shutdown.
*/

typedef enum AvlError {
    AVL_OK = 0,

    AVL_ERR_NULL_ARGUMENT,
    AVL_ERR_ALLOCATION_FAILED,
    AVL_ERR_DUPLICATE_KEY,
    AVL_ERR_NOT_FOUND,
    AVL_ERR_INVALID_RANGE,
    AVL_ERR_OUTPUT_CAPACITY
} AvlError;

/*
 * AVLEntry
 *
 * A temporary structure used to describe a record for indexing purposes.
 * The database engine can construct an array of `AVLEntry` values, for example,
 * when scanning chunks to build the index during initialization.
 *
 * Usage patterns:
 *   - **Boot-time**: The engine collects live records into an `AVLEntry` array,
 *     sorts this array by (gpa, id), and then the AVL module builds a balanced
 *     tree efficiently from the sorted data.
 *   - **Runtime Insertion**: For newly inserted records, the engine creates
 *     a single `AVLEntry`, which the AVL module then inserts using standard
 *     AVL insertion and rotation algorithms.
*/
typedef struct AVLEntry {
    float          gpa;      // The student's GPA, serving as the primary ordering key.
    uint32_t       id;       // The student's unique ID, serving as the tie-breaker key.
    RecordLocation location; // The logical location of the actual StudentRecord in the chunk system.
} AVLEntry;

/*
 * AVLNode
 *
 * Represents a single node within the AVL tree.
 *
 * Members:
 *   gpa      : The GPA value for this node, part of the ordering key.
 *   id       : The student ID for this node, part of the ordering key.
 *   location : A `RecordLocation` pointing back to the actual `StudentRecord`
 *              in the chunk system.
 *   height   : The height of the subtree rooted at this node. This is
 *              dynamically maintained by AVL insertion/deletion logic to
 *              ensure balance.
 *   left     : Pointer to the left child node.
 *   right    : Pointer to the right child node.
*/
typedef struct AVLNode {
    float           gpa;      // GPA of the student record.
    uint32_t        id;       // ID of the student record.
    RecordLocation  location; // Logical location of the record in the chunk system.

    int             height;   // Height of the subtree rooted at this node.

    struct AVLNode *left;     // Pointer to the left child node.
    struct AVLNode *right;    // Pointer to the right child node.
} AVLNode;

/*
 * AVLIndex
 *
 * Represents the entire AVL tree index structure.
 *
 * Members:
 *   root       : A pointer to the root `AVLNode` of the AVL tree. All operations
 *                (insertion, deletion, search) begin from this node.
 *   arena      : A pointer to the `Arena` allocator instance used for allocating
 *                `AVLNode` objects. The AVL module uses this arena but does not
 *                manage its lifecycle (initialization or destruction). The database
 *                engine is responsible for the arena's lifetime.
 *   node_count : The number of currently "live" (non-deleted) nodes in the AVL tree.
 *                Note that this count reflects logical nodes, not necessarily the
 *                number of allocated `AVLNode` structures in the arena, as deleted
 *                nodes remain in arena memory until the arena is torn down.
*/
typedef struct AVLIndex {
    AVLNode *root;      // The root node of the AVL tree.
    Arena   *arena;     // The arena allocator used for AVLNode objects.
    size_t   node_count; // Number of active (non-deleted) nodes in the tree.
} AVLIndex;

/*
    avl_init

    Initializes an empty AVL index.

 * Initializes an empty `AVLIndex` structure.
 * The provided `arena` must be initialized by the caller before this function is invoked.
 *
 * @param index A pointer to the `AVLIndex` structure to initialize.
 * @param arena A pointer to the `Arena` instance to be used for node allocations.
 * @return      `AVL_OK` on successful initialization, or an `AvlError` code on failure.
*/
AvlError avl_init(AVLIndex *index, Arena *arena);

/*
    avl_entry_compare

    Comparator for qsort().

    Orders AVLEntry values by:

        gpa ascending
        id ascending if GPA is equal

 * Compares two `AVLEntry` structures based on the AVL tree's ordering rules.
 * This function is exposed to allow the database engine to sort temporary
 * arrays (e.g., during boot-time index construction) consistently with the
 * AVL tree's internal ordering.
 *
 * @param left  Pointer to the first `AVLEntry` for comparison.
 * @param right Pointer to the second `AVLEntry` for comparison.
*/


AvlError avl_build_from_entries(
    AVLIndex *index,
    const AVLEntry *entries,
    size_t count
);

/*
 * avl_insert
 *
 * Inserts a single `AVLEntry` into the AVL tree. This function is designed
 * for runtime insertions, typically after the database engine has appended
 * a new record into the chunk system.
 *
 * The insertion process involves standard AVL tree insertion logic, including
 * rotations to maintain the tree's balance.
 *
 * @param index A pointer to the `AVLIndex` to insert into.
 * @param entry The `AVLEntry` containing the key and location to insert.
 * @return      `AVL_OK` on success, `AVL_ERR_DUPLICATE_KEY` if the key already
 *              exists, or an `AvlError` code on failure (e.g., allocation).
*/

AvlError avl_insert(AVLIndex *index, AVLEntry entry);

/*
    avl_delete

    Deletes one key from the AVL tree.

    The key is:

        gpa + id

    This is used for:
        normal delete
        GPA update, where old key is deleted and new key is inserted

 * Deletes a specific key (composed of `gpa` and `id`) from the AVL tree.
 * This function is used for both logical record deletion and for updating
 * a record's GPA (by deleting the old key and inserting a new one).
 *
 * Note: This operation marks the node as logically deleted but does not
 * free the underlying `AVLNode` memory from the arena. The memory remains
 * allocated until the entire arena is destroyed during application teardown.
 *
 * @param index A pointer to the `AVLIndex` from which to delete.
 * @param gpa   The GPA component of the key to delete.
 * @param id    The ID component of the key to delete.
 * @return      `AVL_OK` on success, `AVL_ERR_NOT_FOUND` if the key is not
 *              present, or an `AvlError` code on failure.
*/

AvlError avl_delete(AVLIndex *index, float gpa, uint32_t id);

/*
    avl_count_range

    Counts how many records fall inside a GPA range.

    This is the first pass of the range-query strategy.
 * Counts the number of records whose GPA falls within a specified inclusive range
 * (`min_gpa <= gpa <= max_gpa`). This function serves as the first pass in a
 * two-step range query strategy.
 *
 * @param index   A pointer to the `AVLIndex` to query.
 * @param min_gpa The lower bound (inclusive) of the GPA range.
 * @param max_gpa The upper bound (inclusive) of the GPA range.

    The engine can use the returned count to allocate an exact-size
    RecordLocation array before calling avl_range_query().
*/

size_t avl_count_range(
    const AVLIndex *index,
    float min_gpa,
    float max_gpa
);

/*
    avl_range_query

 * Fills a caller-provided array with `RecordLocation` values for all records
 * whose GPA falls within the specified inclusive range (`min_gpa <= gpa <= max_gpa`).
 * This is the second pass of the range query strategy, following `avl_count_range`.
 *
 * The results are populated in ascending order of (GPA, ID).
 *
 * @param index         A pointer to the `AVLIndex` to query.
 * @param min_gpa       The lower bound (inclusive) of the GPA range.
 * @param max_gpa       The upper bound (inclusive) of the GPA range.
 * @param out_locations A pointer to a caller-owned array where the matching
 *                      `RecordLocation` values will be stored.
 * @param out_capacity  The maximum number of `RecordLocation` slots available
 *                      in `out_locations`.
 * @param out_count     A pointer to a `size_t` variable that will receive the
 *                      actual number of `RecordLocation` values written to
 *                      `out_locations`.

    If the output array is too small, the function returns
    AVL_ERR_OUTPUT_CAPACITY.
*/

AvlError avl_range_query(
    const AVLIndex *index,
    float min_gpa,
    float max_gpa,
    RecordLocation *out_locations,
    size_t out_capacity,
    size_t *out_count
);

/*
 * avl_error_string
 *
 * Converts an `AvlError` enumeration value into a human-readable string.
 * This function is primarily intended for logging, debugging, or displaying
 * error messages.
 
 * @param error The `AvlError` code to convert.
 * @return      A constant string literal describing the error.
*/

const char *avl_error_string(AvlError error);

#endif