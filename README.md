# FRASE

**FRASE** is a fixed-schema, file-backed student records storage engine written in C. It is designed around explicit memory ownership, predictable disk layout, runtime indexing, and simple low-level storage mechanisms.

The project stores student records containing an immutable student ID, GPA, and name. Records are persisted to disk in fixed-size chunks and hydrated into RAM during boot. Runtime indexes are rebuilt on startup instead of being persisted, keeping the on-disk format simple and focused only on durable record storage.

## Overview

FRASE is not a SQL database and does not attempt to be a general-purpose database engine. It is a compact storage engine built to demonstrate core systems concepts:

* fixed-size binary records
* chunk-backed storage
* tombstone-based deletion
* dirty chunk persistence
* boot-time compaction
* runtime-only indexes
* arena-backed index allocation
* separation between disk layout, memory layout, indexes, and engine coordination

The engine is built around a simple lifecycle:

```text
disk file
→ boot
→ optional compaction
→ RAM chunk hydration
→ index rebuild
→ runtime operations
→ save / save_exit
```

## Data Model

Each student record is exactly 64 bytes.

```c
typedef struct StudentRecord {
    uint32_t id;
    float    gpa;
    char     name[56];
} StudentRecord;
```

The `id` field is immutable once inserted. The GPA and name fields may be updated through engine operations.

A deleted record is represented by a tombstone. FRASE uses a reserved ID value as the tombstone marker:

```c
FRASE_TOMBSTONE_ID
```

This avoids maintaining a separate deletion bitmap in Version 1.

## Storage Layout

FRASE stores records in a custom binary file format.

The file layout is:

```text
Superblock
ChunkHeader 0
ChunkPayload 0
ChunkHeader 1
ChunkPayload 1
...
```

The superblock stores global file metadata:

* magic number
* version
* total chunk count
* total used slots
* total tombstones

Each chunk stores up to 1024 fixed-size student records. A chunk payload is therefore:

```text
1024 records × 64 bytes = 64 KiB
```

The chunk header stores per-chunk metadata:

* chunk ID
* used slots
* tombstone count

All chunks except possibly the tail chunk are considered full. A full chunk may contain both active records and tombstones. New runtime insertions append only to the tail chunk, overflowing into newly allocated chunks when needed.

## Runtime Architecture

At boot, FRASE loads the durable file into a RAM-resident `ChunkManager`. The chunk manager owns the active chunk array and the record arrays inside each chunk.

FRASE then rebuilds its runtime indexes from the hydrated chunk data.

The two runtime indexes are:

1. **Radix trie for ID lookup**
2. **AVL tree for GPA range queries**

These indexes are not persisted to disk. They are rebuilt every time the engine boots.

This keeps the durable file format simple and avoids the complexity of keeping disk-resident index structures consistent with record updates.

## Radix Index

The radix index maps:

```text
student ID → RecordLocation
```

A `RecordLocation` is a virtual pointer into the chunk system:

```c
typedef struct RecordLocation {
    uint32_t chunk_id;
    uint32_t slot_index;
} RecordLocation;
```

The radix trie is keyed by 32-bit student IDs using hexadecimal nibbles. It provides efficient lookup, insertion, duplicate detection, and deletion by ID.

The radix index does not own student records. It only stores locations.

## AVL Index

The AVL index supports GPA range queries.

It stores keys in the form:

```text
(gpa, id)
```

The GPA is the primary ordering key. The student ID acts as a tie-breaker, allowing multiple students to have the same GPA while keeping every AVL key unique.

Each AVL node stores:

* GPA
* ID
* record location
* height
* left/right child pointers

During boot, the engine builds a temporary flat `AVLEntry` array from live records, sorts it, and constructs a balanced AVL tree. During runtime, individual inserts, deletes, and GPA updates are applied directly to the AVL tree.

## Memory Management

FRASE uses separate arena allocators for radix and AVL nodes.

```text
radix_arena → radix trie nodes
avl_arena   → AVL tree nodes
```

Each arena grows page-by-page. Individual index nodes are not freed during runtime. Deleted index entries are detached or marked inactive, and the arena memory is reclaimed when the engine shuts down.

This keeps allocation simple and avoids complex ownership logic inside the index structures.

## Engine Layer

The engine is the coordinator of the system. It owns:

* the chunk manager
* the radix arena
* the AVL arena
* the radix index
* the AVL index
* the backing file path
* the engine state

The engine exposes operations such as:

* boot
* save
* save-exit shutdown
* insert one record
* insert a batch of records
* find by ID
* delete by ID
* update name
* update GPA
* query GPA range

The shell or main layer does not call the radix, AVL, arena, or file I/O modules directly. It interacts with the engine, and the engine coordinates the lower-level modules.

## Insertions

Runtime insertions append records to the tail chunk starting at the current `used_slots` index.

For batch insertion, FRASE validates the input batch, appends the records into chunks, records the contiguous inserted span, and then walks only that span to update the radix and AVL indexes.

The inserted span is represented as:

```c
typedef struct RecordSpan {
    uint32_t start_chunk_id;
    uint32_t start_slot_index;
    size_t   record_count;
} RecordSpan;
```

This allows the engine to avoid rescanning the entire chunk system after every batch insertion.

Batch insertion is treated as an all-or-nothing operation at the engine level. If index insertion fails partway through, the engine rolls back the visible insertion state.

## Deletion

Deletion is logical, not physical.

The engine first finds the record through the radix index, resolves the virtual location into the chunk system, removes the corresponding AVL key, removes the radix entry, and then marks the chunk record as a tombstone.

The frozen deletion order is:

```text
radix_find(id)
resolve RecordLocation
store old GPA and ID
avl_delete(old_gpa, id)
radix_delete(id)
mark record as tombstone
update tombstone counters
mark chunk dirty
```

The record remains physically present in the file until compaction.

## Updates

Student IDs are immutable.

Name updates affect only the chunk-resident record and do not touch either index.

GPA updates affect both the chunk record and the AVL index. The engine removes the old AVL key and inserts the new one using the same record location.

The frozen GPA update order is:

```text
radix_find(id)
resolve RecordLocation
store old GPA and ID
avl_delete(old_gpa, id)
avl_insert(new_gpa, id, same location)
update chunk GPA
mark chunk dirty
```

If the new AVL insertion fails after deleting the old key, the engine attempts to restore the old AVL entry before returning failure.

## Compaction

FRASE uses boot-time compaction to remove tombstones from the durable file.

Before loading the full file into RAM, the engine reads file statistics and checks the tombstone ratio:

```text
total_tombstones / total_used_slots
```

If the ratio crosses the configured threshold, the file I/O layer rewrites the backing file into a compacted temporary file containing only live records.

After compaction:

* total tombstones are zero
* total used slots equal the number of active records
* all chunks except possibly the tail are full
* the tail chunk may be partially filled
* runtime indexes are rebuilt from the compacted layout

Compaction is performed before radix and AVL indexes exist, so no runtime record locations need to be updated during compaction.

## Persistence

FRASE uses dirty chunk persistence. Chunks modified during runtime are marked dirty. On save, the file I/O layer rewrites dirty chunks and updates the superblock.

Ordinary save keeps the engine running.

Save-exit saves the file and then tears down runtime memory:

```text
save dirty chunks
destroy radix arena
destroy AVL arena
free chunk record arrays
free chunk array
mark engine shutdown
```

## Key Engineering Decisions

FRASE intentionally uses a simple, explicit architecture.

Major decisions include:

* fixed-size records instead of variable-length records
* chunked storage instead of per-record disk writes
* runtime-only indexes instead of persisted indexes
* tombstones instead of immediate physical deletion
* boot-time compaction instead of continuous reuse of deleted slots
* virtual record locations instead of raw persisted pointers
* arena allocation for index nodes
* dirty chunk writes instead of full-file rewrite on every save
* engine-owned lifecycle coordination

These choices keep the system understandable while still demonstrating important storage-engine mechanisms.

## Current Implementation Notes

The core storage engine modules are implemented or designed around the following components:

```text
chunk layout
file I/O
boot compaction
arena allocation
radix index
AVL index
engine coordination
```

The remaining immediate work is:

```text
shell command module
test cases
```

The shell module will provide a command interface for operations such as insert, batch insert, find, delete, update, range query, save, and save-exit.

## Planned Shell Commands

The intended Version 1 shell surface is:

```text
help
insert <id> <gpa> <name>
insert_batch <file_path>
find <id>
delete <id>
update_name <id> <new_name>
update_gpa <id> <new_gpa>
range <min_gpa> <max_gpa>
save
save_exit
```

There is no plain `exit` command. Exiting should go through `save_exit` so the engine has a clean persistence and teardown path.

## License

No license has been selected yet.
