#ifndef FRASE_RADIX_H
#define FRASE_RADIX_H

#include <stdint.h>
#include <stdbool.h>

#include "arena.h"
#include "frasetype.h"

/*
 * FRASE Radix Index Module
 *
 * This module implements the primary index for student IDs using a radix trie
 * (also known as a patricia trie or radix tree). Its primary function is to
 * provide an efficient mapping from a `uint32_t` student ID to a `RecordLocation`.
 *
 * Key characteristics:
 *   - **Mapping**: `uint32_t student_id` -> `RecordLocation`
 *   - **Transience**: The radix index is a transient, in-memory data structure.
 *     It is not persisted to disk directly.
 *   - **Lifecycle**:
 *     1.  **Boot**: Rebuilt from the chunk-resident records loaded from disk.
 *     2.  **Runtime**: Updated dynamically during insert and delete operations.
 *     3.  **Teardown**: Discarded when the application exits.
 *   - **Ownership**: The radix index does not own `StudentRecord` objects;
 *     these are managed by the chunk system. The index merely stores references
 *     (`RecordLocation`) to them.
*/

#define RADIX_CHILD_COUNT     16u // Number of possible children for each radix node (0-F for hexadecimal nibbles).
#define RADIX_ID_NIBBLE_COUNT 8u  // Number of hexadecimal nibbles in a uint32_t ID (32 bits / 4 bits per nibble).

/*
 * RadixError
 *
 * Enumerates possible error conditions encountered by the radix index module.
*/
typedef enum RadixError {
    RADIX_OK = 0,               // No error, operation completed successfully.

    RADIX_ERR_NULL_ARGUMENT,    // A required argument was NULL.
    RADIX_ERR_INVALID_ID,       // The provided ID is invalid (e.g., FRASE_TOMBSTONE_ID).
    RADIX_ERR_ALLOCATION_FAILED,// Memory allocation for a new radix node failed.
    RADIX_ERR_DUPLICATE_ID,     // An attempt was made to insert an ID that already exists.
    RADIX_ERR_NOT_FOUND         // The specified ID was not found in the index.
} RadixError;

/*
 * RadixNode
 *
 * Represents a single node within the 16-way radix trie. Each node can have
 * up to `RADIX_CHILD_COUNT` (16) children, corresponding to hexadecimal nibbles.
 *
 * Members:
 *   children    : An array of pointers to child `RadixNode`s. Each index
 *                 (0-15) corresponds to a hexadecimal nibble. `NULL` indicates
 *                 no child for that nibble.
 *   is_occupied : A boolean flag indicating whether this specific node represents
 *                 the end of a complete, stored student ID.
 *                 - Some nodes serve only as intermediate path nodes.
 *                 - When an ID is deleted, its corresponding node is marked
 *                   as `!is_occupied`, but the node itself (and its path)
 *                   remains in the arena until teardown.
 *   location    : The `RecordLocation` associated with the student ID that
 *                 terminates at this node. This field is only meaningful
 *                 when `is_occupied` is `true`.
*/
typedef struct RadixNode {
    struct RadixNode *children[RADIX_CHILD_COUNT]; // Pointers to child nodes, indexed by nibble value.
    bool              is_occupied;                  // True if this node marks the end of a valid ID.
    RecordLocation    location;                     // The RecordLocation for the ID ending at this node.
} RadixNode;

/*
 * RadixIndex
 *
 * Represents the entire radix trie index structure.
 *
 * Members:
 *   root  : A pointer to the root `RadixNode` of the trie. All ID lookups
 *           and insertions begin from this node.
 *   arena : A pointer to the `Arena` allocator instance used for allocating
 *           `RadixNode` objects. The radix module uses this arena but does
 *           not manage its lifecycle (initialization or destruction).
 *           The database engine is responsible for the arena's lifetime.
*/
typedef struct RadixIndex {
    RadixNode *root;  // The root node of the radix trie.
    Arena     *arena; // The arena allocator used for RadixNode objects.
} RadixIndex;

/*
    radix_init

    Initializes the radix index and allocates the root node from the arena.

 * Initializes a `RadixIndex` structure and allocates its root node from the
 * provided arena. The arena must be initialized prior to calling this function.
 *
 * @param index A pointer to the `RadixIndex` structure to initialize.
 * @param arena A pointer to the `Arena` instance to use for node allocations.
 * @return      `RADIX_OK` on success, or an `RadixError` code on failure.
*/
RadixError radix_init(RadixIndex *index, Arena *arena);

/*
    radix_insert

    Inserts a mapping:

        id -> location

    The ID is walked as eight hexadecimal nibbles.

 * Inserts a mapping from a `uint32_t` student ID to its `RecordLocation`
 * into the radix index. The ID is processed as a sequence of eight
 * hexadecimal nibbles.
 *
 * @param index    A pointer to the `RadixIndex` to insert into.
 * @param id       The `uint32_t` student ID to insert.
 * @param location The `RecordLocation` associated with the ID.
 * @return         `RADIX_OK` on success, `RADIX_ERR_DUPLICATE_ID` if the ID already exists, or an error code on failure.
*/

RadixError radix_insert(
    RadixIndex *index,
    uint32_t id,
    RecordLocation location
);

/*
    radix_find

    Searches for an ID.

 * Searches for a specific `uint32_t` student ID within the radix index.
 *
 * @param index        A pointer to the `RadixIndex` to search.
 * @param id           The `uint32_t` student ID to find.
 * @param out_location A pointer to a `RecordLocation` where the found
 *                     location will be copied on success.
 * @return             `RADIX_OK` on success, `RADIX_ERR_NOT_FOUND` if the ID
 *                     is not present, or an error code on failure.
*/

RadixError radix_find(
    const RadixIndex *index,
    uint32_t id,
    RecordLocation *out_location
);

/*
    radix_delete

    Removes an ID from the radix index.

 * Removes a `uint32_t` student ID from the radix index. This operation
 * does not deallocate any `RadixNode`s. Instead, it marks the node
 * corresponding to the final nibble of the ID as `!is_occupied`.
 * The memory for the nodes remains allocated within the arena until
 * the arena itself is destroyed.
 *
 * @param index A pointer to the `RadixIndex` from which to delete the ID.
 * @param id    The `uint32_t` student ID to remove.
 * @return      `RADIX_OK` on success, `RADIX_ERR_NOT_FOUND` if the ID was
 *              not present, or an error code on failure.
*/

RadixError radix_delete(RadixIndex *index, uint32_t id);

/*
    radix_error_string

 * Converts a `RadixError` enumeration value into a human-readable string.
 * This function is useful for debugging and error reporting.
 *
 * @param error The `RadixError` code to convert.
 * @return      A constant string literal describing the error.
*/

const char *radix_error_string(RadixError error);

#endif