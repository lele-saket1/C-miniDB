#ifndef FRASE_TYPE_H
#define FRASE_TYPE_H

#include <stdint.h>
#include <stddef.h>

/*
 * RecordLocation
 *
 * Represents a virtual, logical address for a `StudentRecord` within the
 * database's chunk management system. This structure is not a direct memory
 * pointer but rather a symbolic reference.
 *
 * A `RecordLocation` uniquely identifies a record by specifying:
 *   - `chunk_id`   : The identifier of the `Chunk` that owns the record.
 *   - `slot_index` : The zero-based index of the record within that specific chunk.
 *
 * The database engine can resolve a `RecordLocation` into an actual
 * `StudentRecord` memory pointer using the `ChunkManager` as follows:
 *   `&manager->chunks[record_location.chunk_id].records[record_location.slot_index]`
 *
 * This abstraction allows indexes (e.g., AVL trees, radix tries) to store
 * lightweight `RecordLocation` values instead of directly owning or copying
 * `StudentRecord` objects, promoting efficient memory usage and data integrity.
*/
typedef struct RecordLocation {
    uint32_t chunk_id;   // The logical ID of the chunk containing the record.
    uint32_t slot_index; // The index of the record within its chunk.
} RecordLocation;

/*
 * RecordSpan
 *
 * Describes a contiguous range of newly inserted records within the database's
 * chunk system. This structure is particularly useful for batch insertion
 * operations.
 *
 * During runtime, a single batch insertion might span across multiple chunk
 * boundaries. However, because insertion is strictly append-only, these
 * newly added records still form one logical, consecutive region.
 *
 * The database engine can iterate over this `RecordSpan` to process each
 * newly seated record, for example, by inserting them into various indexes
 * such as radix tries or AVL trees.
*/
typedef struct RecordSpan {
    uint32_t start_chunk_id;   // The logical ID of the chunk where the span begins.
    uint32_t start_slot_index; // The index within `start_chunk_id` where the span begins.
    size_t   record_count;     // The total number of records included in this span.
} RecordSpan;

#endif