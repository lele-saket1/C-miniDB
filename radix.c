#include "radix.h"

#include <string.h>

#include "Chunk.h"

/*
 * Private helper functions for the radix index module.
 * These functions handle internal node creation and ID nibble extraction.
 */
static RadixNode *radix_create_node(Arena *arena);

static uint32_t radix_get_nibble(uint32_t id, uint32_t depth);


/*
 * radix_init
 *
 * Initializes a `RadixIndex` structure and allocates its root node from the
 * provided arena. The arena must be initialized prior to calling this function.
 *
 * The `RadixIndex` is zero-initialized, and its `root` node is immediately
 * allocated using the provided `arena`. The `arena` pointer is stored for
 * subsequent node allocations.
 *
 * @param index A pointer to the `RadixIndex` structure to initialize.
 * @param arena A pointer to the `Arena` instance to use for node allocations.
 *              The database engine is responsible for the arena's lifetime.
 * @return      `RADIX_OK` on successful initialization, or an `RadixError` code on failure.
*/
RadixError radix_init(RadixIndex *index, Arena *arena)
{
    RadixNode *root;

    if (index == NULL || arena == NULL) {
        return RADIX_ERR_NULL_ARGUMENT;
    }

    memset(index, 0, sizeof(*index));

    root = radix_create_node(arena);

    if (root == NULL) {
        return RADIX_ERR_ALLOCATION_FAILED;
    }

    index->root = root;
    index->arena = arena;

    return RADIX_OK;
}


/*
 * radix_insert
 *
 * Inserts a mapping from a `uint32_t` student ID to its `RecordLocation`
 * into the radix index. The ID is processed as a sequence of eight
 * hexadecimal nibbles, from most significant to least significant.
 *
 * The insertion process involves traversing the trie based on the ID's nibbles.
 * If a required child node does not exist along the path, it is allocated
 * from the `RadixIndex`'s arena.
 *
 * If the node corresponding to the final nibble of the ID is already marked
 * as `is_occupied`, it indicates that the ID already exists in the index,
 * and `RADIX_ERR_DUPLICATE_ID` is returned. Otherwise, the `RecordLocation`
 * is stored, and the node is marked as `is_occupied`.
 *
 * Example ID traversal for `0x1234ABCD`:
 *   Root -> Child for '1' -> Child for '2' -> ... -> Child for 'D'
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
)
{
    RadixNode *node;
    uint32_t depth;

    if (index == NULL || index->root == NULL || index->arena == NULL) {
        return RADIX_ERR_NULL_ARGUMENT;
    }

    if (id == FRASE_TOMBSTONE_ID) {
        return RADIX_ERR_INVALID_ID;
    }

    node = index->root;

    for (depth = 0; depth < RADIX_ID_NIBBLE_COUNT; depth++) {
        uint32_t nibble = radix_get_nibble(id, depth);

        if (node->children[nibble] == NULL) {
            RadixNode *child = radix_create_node(index->arena);

            if (child == NULL) {
                return RADIX_ERR_ALLOCATION_FAILED;
            }

            node->children[nibble] = child;
        }

        node = node->children[nibble];
    }

    if (node->is_occupied) {
        return RADIX_ERR_DUPLICATE_ID;
    }

    node->location = location;
    node->is_occupied = true;

    return RADIX_OK;
}


/*
 * radix_find
 *
 * Searches for a specific `uint32_t` student ID within the radix index.
 * The search traverses the trie based on the ID's hexadecimal nibbles.
 *
 * On successful lookup:
 *   - The `RecordLocation` associated with the ID is copied into `out_location`.
 *   - `RADIX_OK` is returned.
 *
 * On failure (e.g., ID not found or invalid arguments):
 *   - `RADIX_ERR_NOT_FOUND` or another appropriate `RadixError` is returned.
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
)
{
    const RadixNode *node;
    uint32_t depth;

    if (index == NULL || index->root == NULL || out_location == NULL) {
        return RADIX_ERR_NULL_ARGUMENT;
    }

    if (id == FRASE_TOMBSTONE_ID) {
        return RADIX_ERR_INVALID_ID;
    }

    node = index->root;

    for (depth = 0; depth < RADIX_ID_NIBBLE_COUNT; depth++) {
        uint32_t nibble = radix_get_nibble(id, depth);

        if (node->children[nibble] == NULL) {
            return RADIX_ERR_NOT_FOUND;
        }

        node = node->children[nibble];
    }

    if (!node->is_occupied) {
        return RADIX_ERR_NOT_FOUND;
    }

    *out_location = node->location;

    return RADIX_OK;
}


/*
 * radix_delete
 *
 * Removes a `uint32_t` student ID from the radix index. This operation
 * does not deallocate any `RadixNode`s. Instead, it marks the node
 * corresponding to the final nibble of the ID as `!is_occupied`.
 * The `location` field of the node is also zeroed out for safety.
 *
 * The memory for the nodes remains allocated within the arena until
 * the arena itself is destroyed during engine teardown. This approach
 * avoids complex node deallocation logic in a bump allocator context.
 *
 * @param index A pointer to the `RadixIndex` from which to delete the ID.
 * @param id    The `uint32_t` student ID to remove.
 * @return      `RADIX_OK` on success, `RADIX_ERR_NOT_FOUND` if the ID was
 *              not present, `RADIX_ERR_INVALID_ID` if the ID is reserved,
 *              or `RADIX_ERR_NULL_ARGUMENT` if inputs are invalid.
*/
RadixError radix_delete(RadixIndex *index, uint32_t id)
{
    RadixNode *node;
    uint32_t depth;

    if (index == NULL || index->root == NULL) {
        return RADIX_ERR_NULL_ARGUMENT;
    }

    if (id == FRASE_TOMBSTONE_ID) {
        return RADIX_ERR_INVALID_ID;
    }

    node = index->root;

    for (depth = 0; depth < RADIX_ID_NIBBLE_COUNT; depth++) {
        uint32_t nibble = radix_get_nibble(id, depth);

        if (node->children[nibble] == NULL) {
            return RADIX_ERR_NOT_FOUND;
        }

        node = node->children[nibble];
    }

    if (!node->is_occupied) {
        return RADIX_ERR_NOT_FOUND;
    }

    node->is_occupied = false;
    memset(&node->location, 0, sizeof(node->location));

    return RADIX_OK;
}


/*
 * radix_error_string
 *
 * Converts a `RadixError` enumeration value into a human-readable string.
 * This function is useful for debugging and error reporting, providing
 * descriptive messages for each error code.
 *
 * @param error The `RadixError` code to convert.
 * @return      A constant string literal describing the error.
*/

const char *radix_error_string(RadixError error)
{
    switch (error) {
        case RADIX_OK:
            return "radix: ok";

        case RADIX_ERR_NULL_ARGUMENT:
            return "radix: null argument";

        case RADIX_ERR_INVALID_ID:
            return "radix: invalid id";

        case RADIX_ERR_ALLOCATION_FAILED:
            return "radix: allocation failed";

        case RADIX_ERR_DUPLICATE_ID:
            return "radix: duplicate id";

        case RADIX_ERR_NOT_FOUND:
            return "radix: id not found";

        default:
            return "radix: unknown error";
    }
}


/*
 * radix_create_node
 *
 * Allocates a new `RadixNode` from the provided `Arena` and
 * zero-initializes its memory.
 *
 * The alignment for the allocation is set to `sizeof(void *)`. This is
 * considered sufficient because `RadixNode` primarily consists of pointers
 * (the `children` array) and a boolean, and pointer alignment typically
 * satisfies the requirements for these types on most platforms.
 *
 * @param arena A pointer to the `Arena` from which to allocate the node.
 * @return      A pointer to the newly allocated and zero-initialized `RadixNode`
 *              on success, or `NULL` if allocation fails or `arena` is `NULL`.
*/
static RadixNode *radix_create_node(Arena *arena)
{
    RadixNode *node;

    if (arena == NULL) {
        return NULL;
    }

    node = arena_alloc(arena, sizeof(RadixNode), sizeof(void *));

    if (node == NULL) {
        return NULL;
    }

    memset(node, 0, sizeof(*node));

    return node;
}


/*
 * radix_get_nibble
 *
 * Extracts a single hexadecimal nibble (4 bits) from a `uint32_t` ID
 * at a specified `depth`. The `depth` parameter determines which nibble
 * is extracted, starting from the most significant.
 *
 * @param id    The `uint32_t` student ID from which to extract the nibble.
 * @param depth The zero-based depth of the nibble to extract.
 *              - `depth = 0` extracts the most significant nibble (bits 31-28).
 *              - `depth = 7` extracts the least significant nibble (bits 3-0).
 * @return      The extracted nibble as a `uint32_t` value (0-15).
*/
static uint32_t radix_get_nibble(uint32_t id, uint32_t depth)
{
    uint32_t shift;

    shift = (RADIX_ID_NIBBLE_COUNT - 1u - depth) * 4u;

    return (id >> shift) & 0xFu;
}