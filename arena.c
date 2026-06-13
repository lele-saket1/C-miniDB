#include "arena.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * Private helper functions for the arena allocator.
 * These functions handle internal page management and address alignment.
 */
static ArenaPage *arena_create_page(size_t page_size);

static uintptr_t arena_align_forward(uintptr_t address, size_t alignment);


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
ArenaError arena_init(Arena *arena, size_t page_size)
{
    if (arena == NULL) {
        return ARENA_ERR_NULL_ARGUMENT;
    }

    if (page_size == 0) {
        return ARENA_ERR_INVALID_PAGE_SIZE;
    }

    memset(arena, 0, sizeof(*arena));

    arena->first = NULL;
    arena->current = NULL;
    arena->page_size = page_size;

    return ARENA_OK;
}


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
void *arena_alloc(Arena *arena, size_t size, size_t alignment)
{
    ArenaPage *page;
    uintptr_t base_address;
    uintptr_t current_address;
    uintptr_t aligned_address;
    size_t padding;
    size_t new_used;

    if (arena == NULL) {
        return NULL;
    }

    if (size == 0) {
        return NULL;
    }

    if (arena->page_size == 0) {
        return NULL;
    }

    /*
     * Alignment must be a non-zero power of two.
     *
     * This check is performed directly here rather than in a separate helper
     * function due to its simplicity and direct relevance to the allocation
     * logic.
    */

    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        return NULL;
    }

    /*
     * Current design constraint (Version 1):
     * Each individual allocation must fit entirely within a single arena page.
     * Multi-page allocations are not supported by this allocator.
    */

    if (size > arena->page_size) {
        return NULL;
    }

    /*
     * Allocate the first page lazily if the arena is currently empty.
     * This avoids allocating memory until it's actually needed.
    */
    if (arena->current == NULL) {
        page = arena_create_page(arena->page_size);

        if (page == NULL) {
            return NULL;
        }

        arena->first = page;
        arena->current = page;
    }

    page = arena->current;

    base_address = (uintptr_t)page->memory;
    current_address = base_address + page->used;
    aligned_address = arena_align_forward(current_address, alignment);

    padding = (size_t)(aligned_address - current_address);

    if (page->used + padding <= arena->page_size) {
        new_used = page->used + padding + size;
    } else {
        new_used = arena->page_size + 1;
    }
    
    /*
     * If the current page cannot accommodate the allocation (either due to
     * insufficient space or alignment requirements), allocate a new page
     * and attempt the allocation on that fresh page.
    */
    if (new_used > arena->page_size) {
        ArenaPage *new_page = arena_create_page(arena->page_size);

        if (new_page == NULL) {
            return NULL;
        }

        page->next = new_page;
        arena->current = new_page;
        page = new_page;

        base_address = (uintptr_t)page->memory;
        current_address = base_address + page->used;
        aligned_address = arena_align_forward(current_address, alignment);

        padding = (size_t)(aligned_address - current_address);

        if (page->used + padding <= arena->page_size) {
            new_used = page->used + padding + size;
        } else {
            new_used = arena->page_size + 1;
        }

        if (new_used > arena->page_size) {
            return NULL;
        }
    }

    page->used = new_used;

    return (void *)aligned_address;
}


/*
 * arena_destroy
 *
 * Frees all memory pages owned by the arena, effectively releasing all
 * allocations made from it. This operation is efficient, with a complexity
 * proportional to the number of pages, not the number of individual allocations.
 *
 * @param arena A pointer to the `Arena` instance to destroy.
 * @return      void
*/
void arena_destroy(Arena *arena)
{
    ArenaPage *page;
    ArenaPage *next;

    if (arena == NULL) {
        return;
    }

    page = arena->first;

    while (page != NULL) {
        next = page->next;

        free(page->memory);
        page->memory = NULL;

        free(page);

        page = next;
    }

    arena->first = NULL;
    arena->current = NULL;
    arena->page_size = 0;
}


/*
 * arena_error_string
 *
 * Converts an `ArenaError` enumeration value into a human-readable string.
 * This is useful for debugging and error reporting.
 *
 * @param error The `ArenaError` code to convert.
 * @return      A constant string literal describing the error.
*/

const char *arena_error_string(ArenaError error)
{
    switch (error) {
        case ARENA_OK:
            return "arena: ok";

        case ARENA_ERR_NULL_ARGUMENT:
            return "arena: null argument";

        case ARENA_ERR_INVALID_PAGE_SIZE:
            return "arena: invalid page size";

        case ARENA_ERR_INVALID_ALLOCATION_SIZE:
            return "arena: invalid allocation size";

        case ARENA_ERR_INVALID_ALIGNMENT:
            return "arena: invalid alignment";

        case ARENA_ERR_ALLOCATION_FAILED:
            return "arena: allocation failed";

        default:
            return "arena: unknown error";
    }
}


/*
 * arena_create_page
 *
 * Allocates a new `ArenaPage` structure and its associated raw memory buffer.
 * This function is used internally by the arena allocator to expand its memory pool.
 *
 * @param page_size The size of the memory buffer to allocate for the page.
 * @return          A pointer to the newly created `ArenaPage` on success, or `NULL` on failure.
*/

static ArenaPage *arena_create_page(size_t page_size)
{
    ArenaPage *page;

    if (page_size == 0) {
        return NULL;
    }

    page = malloc(sizeof(*page));

    if (page == NULL) {
        return NULL;
    }

    page->memory = malloc(page_size);

    if (page->memory == NULL) {
        free(page);
        return NULL;
    }

    page->used = 0;
    page->next = NULL;

    return page;
}


/*
 * arena_align_forward
 *
 * Calculates the next memory address that satisfies the given alignment
 * requirement, rounding the provided `address` upwards.
 *
 * Precondition: The `alignment` parameter must be a power of two. This is
 * checked by `arena_alloc()` before calling this helper.
 *
 * @param address   The starting memory address.
 * @param alignment The desired byte alignment (must be a power of two).
 * @return          The aligned memory address.
*/

static uintptr_t arena_align_forward(uintptr_t address, size_t alignment)
{
    uintptr_t mask;

    mask = (uintptr_t)alignment - 1u;

    return (address + mask) & ~mask;
}