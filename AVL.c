#include "AVL.h"

#include <stdlib.h>
#include <string.h>

/*
 * Private helper functions for the AVL tree module.
 * These functions encapsulate the internal logic for node creation,
 * comparison, height management, rotations, rebalancing, and recursive
 * tree operations like insertion, deletion, and range queries.
 */
/**
 * @brief Allocates a new `AVLNode` from the provided `Arena` and initializes it.
 * @param arena A pointer to the `Arena` from which to allocate the node.
 * @param entry The `AVLEntry` containing the data to initialize the new node.
 * @return      A pointer to the newly allocated and initialized `AVLNode`
 *              on success, or `NULL` if allocation fails or `arena` is `NULL`.
 */
static AVLNode *avl_create_node(Arena *arena, AVLEntry entry);

/**
 * @brief Compares two key pairs (`gpa`, `id`) according to the AVL tree's ordering rules.
 * @param left_gpa  The GPA component of the first key.
 * @param left_id   The ID component of the first key.
 * @param right_gpa The GPA component of the second key.
 * @param right_id  The ID component of the second key.
 * @return          A negative integer if `left` < `right`, zero if `left` == `right`,
 *                  or a positive integer if `left` > `right`.
 */
static int avl_compare_key(
    float left_gpa,
    uint32_t left_id,
    float right_gpa,
    uint32_t right_id
);

/**
 * @brief A `qsort` compatible comparison function for `AVLEntry` structures.
 * @param left  Pointer to the first `AVLEntry` for comparison.
 * @param right Pointer to the second `AVLEntry` for comparison.
 * @return      An integer indicating the relative order of the two entries.
 */
static int avl_compare_entry(const void *left, const void *right);

/**
 * @brief Returns the height of the subtree rooted at the given node.
 * @param node A pointer to the `AVLNode`.
 * @return     The height of the node, or 0 if `node` is `NULL`.
 */
static int avl_height(const AVLNode *node);

/**
 * @brief Returns the maximum of two integer values.
 * @param a The first integer.
 * @param b The second integer.
 * @return  The larger of `a` and `b`.
 */
static int avl_max_int(int a, int b);

/**
 * @brief Recalculates and updates the height of the given node.
 * @param node A pointer to the `AVLNode` whose height needs to be updated.
 */
static void avl_update_height(AVLNode *node);

/**
 * @brief Calculates the balance factor of a node.
 * @param node A pointer to the `AVLNode`.
 * @return     The balance factor of the node, or 0 if `node` is `NULL`.
 */
static int avl_balance_factor(const AVLNode *node);

/**
 * @brief Performs a left rotation around the given node.
 * @param node The node to rotate around (the root of the unbalanced subtree).
 * @return     The new root of the rotated subtree.
 */
static AVLNode *avl_rotate_left(AVLNode *node);

/**
 * @brief Performs a right rotation around the given node.
 * @param node The node to rotate around (the root of the unbalanced subtree).
 * @return     The new root of the rotated subtree.
 */
static AVLNode *avl_rotate_right(AVLNode *node);

/**
 * @brief Rebalances the subtree rooted at the given node if it becomes unbalanced.
 * @param node A pointer to the `AVLNode` to rebalance.
 * @return     The new root of the (potentially rebalanced) subtree.
 */
static AVLNode *avl_rebalance(AVLNode *node);

/**
 * @brief Recursively inserts a new `AVLEntry` into the AVL tree.
 * @param index A pointer to the `AVLIndex` (needed for its arena).
 * @param root  The current root of the subtree being considered for insertion.
 * @param entry The `AVLEntry` to insert.
 * @param error A pointer to an `AvlError` variable to report any errors.
 * @return      The new root of the subtree after insertion and potential rebalancing.
 */
static AVLNode *avl_insert_node(
    AVLIndex *index,
    AVLNode *root,
    AVLEntry entry,
    AvlError *error
);

/**
 * @brief Finds the node with the minimum key value (leftmost node) in a subtree.
 * @param root The root of the subtree to search.
 * @return     A pointer to the node with the minimum key, or `NULL` if the
 *             subtree is empty.
 */
static AVLNode *avl_find_min(AVLNode *root);

/**
 * @brief Recursively deletes a node with the specified `gpa` and `id` from the AVL tree.
 * @param root  The current root of the subtree being considered for deletion.
 * @param gpa   The GPA component of the key to delete.
 * @param id    The ID component of the key to delete.
 * @param error A pointer to an `AvlError` variable to report any errors.
 * @return      The new root of the subtree after deletion and potential rebalancing.
 */
static AVLNode *avl_delete_node(
    AVLNode *root,
    float gpa,
    uint32_t id,
    AvlError *error
);

/**
 * @brief Recursively builds a height-balanced AVL tree from a sorted array of `AVLEntry` values.
 * @param index   A pointer to the `AVLIndex` (needed for its arena).
 * @param entries A pointer to the sorted array of `AVLEntry` values.
 * @param start   The starting index (inclusive) of the current range.
 * @param end     The ending index (exclusive) of the current range.
 * @param error   A pointer to an `AvlError` variable to report any errors.
 * @return        The root of the newly constructed balanced subtree.
 */
static AVLNode *avl_build_balanced_from_entries(
    AVLIndex *index,
    const AVLEntry *entries,
    size_t start,
    size_t end,
    AvlError *error
);

/**
 * @brief Recursively counts the number of nodes within a subtree whose GPA falls
 *        within the specified inclusive range.
 * @param node    The current root of the subtree to search.
 * @param min_gpa The lower bound (inclusive) of the GPA range.
 * @param max_gpa The upper bound (inclusive) of the GPA range.
 * @return        The count of matching nodes in the subtree.
 */
static size_t avl_count_range_node(
    const AVLNode *node,
    float min_gpa,
    float max_gpa
);

/**
 * @brief Recursively traverses the AVL tree to find `RecordLocation` values for nodes
 *        whose GPA falls within the specified inclusive range.
 * @param node          The current root of the subtree to search.
 * @param min_gpa       The lower bound (inclusive) of the GPA range.
 * @param max_gpa       The upper bound (inclusive) of the GPA range.
 * @param out_locations A pointer to a caller-owned array where the matching
 *                      `RecordLocation` values will be stored.
 * @param out_capacity  The maximum number of `RecordLocation` slots available
 *                      in `out_locations`.
 * @param out_count     A pointer to a `size_t` variable that tracks the
 *                      current number of `RecordLocation` values written.
 * @return              `AVL_OK` on success, or `AVL_ERR_OUTPUT_CAPACITY` if the
 *                      output array is too small.
 */
static AvlError avl_range_query_node(
    const AVLNode *node,
    float min_gpa,
    float max_gpa,
    RecordLocation *out_locations,
    size_t out_capacity,
    size_t *out_count
);


/*
    avl_init

    Initializes an empty AVL index.

    The engine owns the arena and passes it here.
    AVL uses the arena for AVLNode allocation, but does not destroy it.
*/

AvlError avl_init(AVLIndex *index, Arena *arena)
{
    if (index == NULL || arena == NULL) {
        return AVL_ERR_NULL_ARGUMENT;
    }

    memset(index, 0, sizeof(*index));

    index->root = NULL;
    index->arena = arena;
    index->node_count = 0;

    return AVL_OK;
}


/*
    avl_build_from_entries

    Boot-time AVL construction.

    The engine gives AVL a temporary AVLEntry array.

    AVL sorts it in-place by:

        gpa ascending
        id ascending if GPA is equal

    Then AVL builds a balanced tree from the sorted array in O(n).

    This function is intended for boot hydration, not runtime insertion.
*/

AvlError avl_build_from_entries(
    AVLIndex *index,
    AVLEntry *entries,
    size_t count
)
{
    AvlError error;
    size_t i;

    if (index == NULL || index->arena == NULL) {
        return AVL_ERR_NULL_ARGUMENT;
    }

    if (count > 0 && entries == NULL) {
        return AVL_ERR_NULL_ARGUMENT;
    }

    /*
     * Rebuilding into a non-empty index would leak logical tree state.
     * This function is designed for initial boot-time construction before
     * runtime operations begin.
    */

    if (index->root != NULL || index->node_count != 0) {
        return AVL_ERR_DUPLICATE_KEY;
    }

    if (count == 0) {
        index->root = NULL;
        index->node_count = 0;
        return AVL_OK;
    }

    qsort(entries, count, sizeof(AVLEntry), avl_compare_entry);

    /*
     * Check for duplicate keys after sorting.
     * In a typical FRASE usage, duplicate IDs should be prevented by
     * higher-level validation (e.g., radix index). This check provides
     * an additional safeguard for AVL tree integrity.
    */

    for (i = 1; i < count; i++) {
        if (avl_compare_key(
                entries[i - 1].gpa,
                entries[i - 1].id,
                entries[i].gpa,
                entries[i].id
            ) == 0) {
            return AVL_ERR_DUPLICATE_KEY;
        }
    }

    error = AVL_OK;

    index->root = avl_build_balanced_from_entries(
        index,
        entries,
        0,
        count,
        &error
    );

    if (error != AVL_OK) {
        index->root = NULL;
        index->node_count = 0;
        return error;
    }

    index->node_count = count;

    return AVL_OK;
}


/*
    avl_insert

    Runtime insertion.

    This uses normal AVL insertion and rotations.

    Boot-time bulk construction should use avl_build_from_entries().
*/

AvlError avl_insert(AVLIndex *index, AVLEntry entry)
{
    AvlError error;
    AVLNode *new_root;

    if (index == NULL || index->arena == NULL) {
        return AVL_ERR_NULL_ARGUMENT;
    }

    error = AVL_OK;

    new_root = avl_insert_node(index, index->root, entry, &error);

    if (error != AVL_OK) {
        return error;
    }

    index->root = new_root;
    index->node_count++;

    return AVL_OK;
}


/*
    avl_delete

    Deletes one AVL key:

        gpa + id

    This is used for:
        - normal delete
        - GPA update, where old key is deleted and new key is inserted

    No arena memory is freed.
    Removed nodes become unreachable arena memory until teardown.
*/

AvlError avl_delete(AVLIndex *index, float gpa, uint32_t id)
{
    AvlError error;
    AVLNode *new_root;

    if (index == NULL) {
        return AVL_ERR_NULL_ARGUMENT;
    }

    error = AVL_OK;

    new_root = avl_delete_node(index->root, gpa, id, &error);

    if (error != AVL_OK) {
        return error;
    }

    index->root = new_root;

    if (index->node_count > 0) {
        index->node_count--;
    }

    return AVL_OK;
}


/*
    avl_count_range

    First pass of range query.

    Counts how many AVL nodes have GPA inside:

        min_gpa <= gpa <= max_gpa

    The engine can allocate exactly this many RecordLocation slots before
    calling avl_range_query().
*/

size_t avl_count_range(
    const AVLIndex *index,
    float min_gpa,
    float max_gpa
)
{
    if (index == NULL) {
        return 0;
    }

    if (min_gpa > max_gpa) {
        return 0;
    }

    return avl_count_range_node(index->root, min_gpa, max_gpa);
}


/*
    avl_range_query

    Second pass of range query.

    Fills a caller-owned RecordLocation array.

    Results are produced in ascending AVL order:
        GPA ascending, then ID ascending.
*/

AvlError avl_range_query(
    const AVLIndex *index,
    float min_gpa,
    float max_gpa,
    RecordLocation *out_locations,
    size_t out_capacity,
    size_t *out_count
)
{
    if (index == NULL || out_count == NULL) {
        return AVL_ERR_NULL_ARGUMENT;
    }

    if (out_capacity > 0 && out_locations == NULL) {
        return AVL_ERR_NULL_ARGUMENT;
    }

    if (min_gpa > max_gpa) {
        return AVL_ERR_INVALID_RANGE;
    }

    *out_count = 0;

    return avl_range_query_node(
        index->root,
        min_gpa,
        max_gpa,
        out_locations,
        out_capacity,
        out_count
    );
}


/*
    avl_error_string

    Converts AvlError values into readable strings.
*/

const char *avl_error_string(AvlError error)
{
    switch (error) {
        case AVL_OK:
            return "avl: ok";

        case AVL_ERR_NULL_ARGUMENT:
            return "avl: null argument";

        case AVL_ERR_ALLOCATION_FAILED:
            return "avl: allocation failed";

        case AVL_ERR_DUPLICATE_KEY:
            return "avl: duplicate key";

        case AVL_ERR_NOT_FOUND:
            return "avl: key not found";

        case AVL_ERR_INVALID_RANGE:
            return "avl: invalid range";

        case AVL_ERR_OUTPUT_CAPACITY:
            return "avl: output capacity too small";

        default:
            return "avl: unknown error";
    }
}


/*
    avl_create_node

    Allocates one AVLNode from the arena and initializes it.

    No _Alignof is used.

    sizeof(void *) is used as the alignment because AVLNode contains pointers,
    and pointer alignment is sufficient for this project.
*/

static AVLNode *avl_create_node(Arena *arena, AVLEntry entry)
{
    AVLNode *node;

    if (arena == NULL) {
        return NULL;
    }

    node = arena_alloc(arena, sizeof(AVLNode), sizeof(void *));

    if (node == NULL) {
        return NULL;
    }

    node->gpa = entry.gpa;
    node->id = entry.id;
    node->location = entry.location;

    node->height = 1;
    node->left = NULL;
    node->right = NULL;

    return node;
}


/*
    avl_compare_key

    The AVL ordering rule.

    Primary key:
        GPA ascending

    Tie-breaker:
        ID ascending

    Returns:
        negative if left < right
        zero     if left == right
        positive if left > right
*/

static int avl_compare_key(
    float left_gpa,
    uint32_t left_id,
    float right_gpa,
    uint32_t right_id
)
{
    if (left_gpa < right_gpa) {
        return -1;
    }

    if (left_gpa > right_gpa) {
        return 1;
    }

    if (left_id < right_id) {
        return -1;
    }

    if (left_id > right_id) {
        return 1;
    }

    return 0;
}


/*
    avl_compare_entry

    qsort comparator for AVLEntry arrays.

    This is private because sorting is an AVL concern, not an engine concern.
*/

static int avl_compare_entry(const void *left, const void *right)
{
    const AVLEntry *left_entry;
    const AVLEntry *right_entry;

    left_entry = (const AVLEntry *)left;
    right_entry = (const AVLEntry *)right;

    return avl_compare_key(
        left_entry->gpa,
        left_entry->id,
        right_entry->gpa,
        right_entry->id
    );
}


/*
 * avl_height
 *
 * Returns the height of the subtree rooted at the given node.
 * If the node is `NULL`, its height is considered 0.
 * @param node A pointer to the `AVLNode`.
 * @return     The height of the node, or 0 if `node` is `NULL`.
 */
static int avl_height(const AVLNode *node)
{
    if (node == NULL) {
        return 0;
    }

    return node->height;
}


static int avl_max_int(int a, int b)
{
    if (a > b) {
        return a;
    }

    return b;
}


static void avl_update_height(AVLNode *node)
{
    if (node == NULL) {
        return;
    }

    node->height = 1 + avl_max_int(
        avl_height(node->left),
        avl_height(node->right)
    );
}


static int avl_balance_factor(const AVLNode *node)
{
    if (node == NULL) {
        return 0;
    }

    return avl_height(node->left) - avl_height(node->right);
}


/*
    avl_rotate_right

          y                 x
         / \               / \
        x   C     ->      A   y
       / \                   / \
      A   B                 B   C
*/

static AVLNode *avl_rotate_right(AVLNode *y)
{
    AVLNode *x;
    AVLNode *b;

    if (y == NULL || y->left == NULL) {
        return y;
    }

    x = y->left;
    b = x->right;

    x->right = y;
    y->left = b;

    avl_update_height(y);
    avl_update_height(x);

    return x;
}


/*
    avl_rotate_left

        x                   y
       / \                 / \
      A   y      ->       x   C
         / \             / \
        B   C           A   B
*/

static AVLNode *avl_rotate_left(AVLNode *x)
{
    AVLNode *y;
    AVLNode *b;

    if (x == NULL || x->right == NULL) {
        return x;
    }

    y = x->right;
    b = y->left;

    y->left = x;
    x->right = b;

    avl_update_height(x);
    avl_update_height(y);

    return y;
}


/*
    avl_rebalance

    Updates node height and applies AVL rotations if needed.
*/

static AVLNode *avl_rebalance(AVLNode *node)
{
    int balance;

    if (node == NULL) {
        return NULL;
    }

    avl_update_height(node);

    balance = avl_balance_factor(node);

    /*
     * Left-heavy subtree:
     *   - If the left child is right-heavy, perform a left rotation on the left child first (LR case).
     *   - Then, perform a right rotation on the current node.
     */
    if (balance > 1) {
        if (avl_balance_factor(node->left) < 0) {
            node->left = avl_rotate_left(node->left);
        }

        return avl_rotate_right(node);
    }

    /*
     * Right-heavy subtree:
     *   - If the right child is left-heavy, perform a right rotation on the right child first (RL case).
     *   - Then, perform a left rotation on the current node.
     */
    if (balance < -1) {
        if (avl_balance_factor(node->right) > 0) {
            node->right = avl_rotate_right(node->right);
        }

        return avl_rotate_left(node);
    }

    return node;
}


/*
    avl_insert_node

    Recursive AVL insertion.

    Returns the new subtree root after insertion/rebalancing.
*/

static AVLNode *avl_insert_node(
    AVLIndex *index,
    AVLNode *root,
    AVLEntry entry,
    AvlError *error
)
{
    int cmp;

    if (error == NULL || *error != AVL_OK) {
        return root;
    }

    if (root == NULL) {
        AVLNode *node = avl_create_node(index->arena, entry);

        if (node == NULL) {
            *error = AVL_ERR_ALLOCATION_FAILED;
            return NULL;
        }

        return node;
    }

    cmp = avl_compare_key(entry.gpa, entry.id, root->gpa, root->id);

    if (cmp < 0) {
        root->left = avl_insert_node(index, root->left, entry, error);
    } else if (cmp > 0) {
        root->right = avl_insert_node(index, root->right, entry, error);
    } else {
        *error = AVL_ERR_DUPLICATE_KEY;
        return root;
    }

    if (*error != AVL_OK) {
        return root;
    }

    return avl_rebalance(root);
}


/*
    avl_find_min

    Finds the smallest node in a subtree.
*/

static AVLNode *avl_find_min(AVLNode *root)
{
    if (root == NULL) {
        return NULL;
    }

    while (root->left != NULL) {
        root = root->left;
    }

    return root;
}


/*
    avl_delete_node

    Recursive AVL deletion.

    This removes a key from the tree structurally but does not free arena
    memory. Detached nodes remain allocated until arena teardown.

    For a node with two children, the inorder successor is copied into the
    current node, then the successor is deleted from the right subtree.
*/

static AVLNode *avl_delete_node(
    AVLNode *root,
    float gpa,
    uint32_t id,
    AvlError *error
)
{
    int cmp;

    if (error == NULL || *error != AVL_OK) {
        return root;
    }

    if (root == NULL) {
        *error = AVL_ERR_NOT_FOUND;
        return NULL;
    }

    cmp = avl_compare_key(gpa, id, root->gpa, root->id);

    if (cmp < 0) {
        root->left = avl_delete_node(root->left, gpa, id, error);
    } else if (cmp > 0) {
        root->right = avl_delete_node(root->right, gpa, id, error);
    } else {
        /*
            Found the node to remove.
        */

        if (root->left == NULL && root->right == NULL) {
            return NULL;
        }

        if (root->left == NULL) {
            return root->right;
        }

        if (root->right == NULL) {
            return root->left;
        }

        /*
         * Two-child case:
         *   Find the in-order successor (minimum node in the right subtree),
         *   copy its data to the current node, and then recursively delete the successor.
         */
        {
            AVLNode *successor = avl_find_min(root->right);
            AvlError delete_successor_error = AVL_OK;

            root->gpa = successor->gpa;
            root->id = successor->id;
            root->location = successor->location;

            root->right = avl_delete_node(
                root->right,
                successor->gpa,
                successor->id,
                &delete_successor_error
            );

            if (delete_successor_error != AVL_OK) {
                *error = delete_successor_error;
                return root;
            }
        }
    }

    if (*error != AVL_OK) {
        return root;
    }

    return avl_rebalance(root);
}


/*
    avl_build_balanced_from_entries

    Builds a balanced binary search tree from a sorted AVLEntry range.

    The range is [start, end).

    Because the input array is sorted by the AVL key, choosing the midpoint
    as root creates a height-balanced tree.

    Heights are set during construction.
*/

static AVLNode *avl_build_balanced_from_entries(
    AVLIndex *index,
    const AVLEntry *entries,
    size_t start,
    size_t end,
    AvlError *error
)
{
    size_t mid;
    AVLNode *node;

    if (error == NULL || *error != AVL_OK) {
        return NULL;
    }

    if (start >= end) {
        return NULL;
    }

    mid = start + ((end - start) / 2);

    node = avl_create_node(index->arena, entries[mid]);

    if (node == NULL) {
        *error = AVL_ERR_ALLOCATION_FAILED;
        return NULL;
    }

    node->left = avl_build_balanced_from_entries(
        index,
        entries,
        start,
        mid,
        error
    );

    if (*error != AVL_OK) {
        return node;
    }

    node->right = avl_build_balanced_from_entries(
        index,
        entries,
        mid + 1,
        end,
        error
    );

    if (*error != AVL_OK) {
        return node;
    }

    avl_update_height(node);

    return node;
}


/*
    avl_count_range_node

    Counts matching nodes in a GPA range.

    Since AVL ordering is by (gpa, id), GPA comparisons allow us to skip
    entire left or right subtrees in obvious cases.
*/

static size_t avl_count_range_node(
    const AVLNode *node,
    float min_gpa,
    float max_gpa
)
{
    size_t count;

    if (node == NULL) {
        return 0;
    }

    /*
     * If the current node's GPA is below the minimum range, then all nodes
     * in its left subtree will also be below the minimum. Therefore, only
     * the right subtree needs to be checked.
     */
    if (node->gpa < min_gpa) {
        return avl_count_range_node(node->right, min_gpa, max_gpa);
    }

    /*
     * If the current node's GPA is above the maximum range, then all nodes
     * in its right subtree will also be above the maximum. Therefore, only
     * the left subtree needs to be checked.
     */
    if (node->gpa > max_gpa) {
        return avl_count_range_node(node->left, min_gpa, max_gpa);
    }

    count = 1;

    count += avl_count_range_node(node->left, min_gpa, max_gpa);
    count += avl_count_range_node(node->right, min_gpa, max_gpa);

    return count;
}


/*
    avl_range_query_node

    Fills caller-owned output array with matching RecordLocations.

    Traversal is inorder, so output is sorted by:

        GPA ascending
        ID ascending
*/

static AvlError avl_range_query_node(
    const AVLNode *node,
    float min_gpa,
    float max_gpa,
    RecordLocation *out_locations,
    size_t out_capacity,
    size_t *out_count
)
{
    AvlError error;

    if (node == NULL) {
        return AVL_OK;
    }

    /*
     * If the current node's GPA is greater than or equal to `min_gpa`,
     * then its left subtree might contain records within the range,
     * so we traverse left.
     */
    if (node->gpa >= min_gpa) {
        error = avl_range_query_node(
            node->left,
            min_gpa,
            max_gpa,
            out_locations,
            out_capacity,
            out_count
        );

        if (error != AVL_OK) {
            return error;
        }
    }

    if (node->gpa >= min_gpa && node->gpa <= max_gpa) {
        if (*out_count >= out_capacity) {
            return AVL_ERR_OUTPUT_CAPACITY;
        }

        out_locations[*out_count] = node->location;
        (*out_count)++;
    }

    /*
     * If the current node's GPA is less than or equal to `max_gpa`,
     * then its right subtree might contain records within the range,
     * so we traverse right.
     */
    if (node->gpa <= max_gpa) {
        error = avl_range_query_node(
            node->right,
            min_gpa,
            max_gpa,
            out_locations,
            out_capacity,
            out_count
        );

        if (error != AVL_OK) {
            return error;
        }
    }

    return AVL_OK;
}