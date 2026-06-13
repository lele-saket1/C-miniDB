#ifndef FRASE_ARENA_H
#define FRASE_ARENA_H

#include <stddef.h>

/*
 * FRASE Arena Allocator
 *
 * This module implements a simple, page-backed bump allocator. It is designed
 * for efficient allocation of transient, short-lived objects that are typically
 * freed all at once.
 *
 * Key characteristics:
 *   - **Purpose**: Primarily intended for runtime-only data structures like
 *     AVL tree nodes, radix trie nodes, or other index components.
 *   - **Ownership**: It does *not* manage `StudentRecord` objects directly;
 *     `StudentRecord` objects reside within `Chunk` structures.
 *   - **No Individual Free**: Memory allocated from an arena cannot be freed
 *     individually. All allocations within an arena are released simultaneously
 *     when `arena_destroy()` is called.
 *   - **Lifecycle**: This allocation model aligns with FRASE's typical runtime
 *     workflow:
 *       1. Load disk records into RAM chunks.
 *       2. Build transient in-memory indexes (e.g., AVL, radix) using the arena.
 *       3. Perform operations.
 *       4. Teardown/destroy the arena, releasing all index memory at once.
*/

#define FRASE_ARENA_DEFAULT_PAGE_SIZE 8192u // The default size for each memory page allocated by the arena (8 KiB).

/*
 * ArenaError
 *
 * Enumerates possible error conditions that can occur within the arena allocator module.
*/
typedef enum ArenaError {
    ARENA_OK = 0,                   // No error, operation completed successfully.

    ARENA_ERR_NULL_ARGUMENT,        // A required argument was NULL.
    ARENA_ERR_INVALID_PAGE_SIZE,    // The provided page size is invalid (e.g., zero).
    ARENA_ERR_INVALID_ALLOCATION_SIZE,// The requested allocation size is invalid (e.g., too large for a single page).
    ARENA_ERR_INVALID_ALIGNMENT,    // The requested alignment is invalid (e.g., zero or not a power of two).
    ARENA_ERR_ALLOCATION_FAILED     // Memory allocation (e.g., for a new page) failed.
} ArenaError;

/*
 * ArenaPage
 *
 * Represents a single memory page within an arena. Pages are linked together
 * to form the arena's memory pool.
 *
 * Members:
 *   memory : A pointer to the raw, dynamically allocated memory block for this page.
 *   used   : The number of bytes currently consumed (allocated) within this page.
 *   next   : A pointer to the next `ArenaPage` in the linked list, or `NULL` if this is the last page.
*/
typedef struct ArenaPage {
    unsigned char    *memory; // Pointer to the raw memory buffer for this page.
    size_t            used;   // The amount of memory (in bytes) currently used within this page.
    struct ArenaPage *next;   // Pointer to the next page in the arena's linked list.
} ArenaPage;

/*
 * Arena
 *
 * Represents an instance of the arena allocator. Each `Arena` manages its
 * own set of memory pages.
 *
 * Note: Different data structures (e.g., AVL trees, radix tries) should
 * typically use separate `Arena` objects to manage their memory independently.
 *
 * Members:
 *   first     : A pointer to the first `ArenaPage` in the arena's linked list.
 *   current   : A pointer to the `ArenaPage` currently being used for new allocations.
 *   page_size : The fixed size (in bytes) of each memory page allocated by this arena.
*/
typedef struct Arena {
    ArenaPage *first;    // Pointer to the first page in the arena's page list.
    ArenaPage *current;  // Pointer to the current page from which allocations are made.
    size_t     page_size; // The fixed size of each memory page managed by this arena.
} Arena;

/*
 * arena_init
 *
 * Initializes an `Arena` structure. This function sets up the arena's internal
 * state but does not allocate any memory pages immediately. The first page
 * is allocated lazily upon the first call to `arena_alloc()`.
 *
 * @param arena     A pointer to the `Arena` structure to initialize.
 * @param page_size The desired size for each memory page the arena will allocate.
 * @return          `ARENA_OK` on successful initialization, or an `ArenaError` code on failure.
*/
ArenaError arena_init(Arena *arena, size_t page_size);

/*
 * arena_alloc
 *
 * Allocates a block of memory from the arena. The allocation is performed
 * from the current page if sufficient space is available. If not, a new page
 * is allocated and linked into the arena, and the allocation is retried on
 * the new page.
 *
 * Important: Memory allocated via `arena_alloc` cannot be freed individually.
 * All allocations are released when `arena_destroy()` is called.
 *
 * @param arena     A pointer to the `Arena` instance from which to allocate.
 * @param size      The number of bytes to allocate.
 * @param alignment The required byte alignment for the returned memory address.
 *                  Must be a power of two.
 * @return          A pointer to the allocated memory block on success, or `NULL` on failure.
*/
void *arena_alloc(Arena *arena, size_t size, size_t alignment);

/*
 * arena_destroy
 *
 * Frees all memory pages owned by the arena, effectively releasing all
 * allocations made from it. This operation is efficient, with a complexity
 * proportional to the number of pages, not the number of individual allocations.
 *
 * @param arena A pointer to the `Arena` instance to destroy.
*/
void arena_destroy(Arena *arena);

/*
 * arena_error_string
 *
 * Converts an `ArenaError` enumeration value into a human-readable string.
 * This is useful for debugging and error reporting.
 *
 * @param error The `ArenaError` code to convert.
 * @return      A constant string literal describing the error.
*/
const char *arena_error_string(ArenaError error);

#endif