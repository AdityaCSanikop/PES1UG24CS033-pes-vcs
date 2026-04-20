// tree.c — Tree object serialization and construction
//
// PROVIDED functions: get_file_mode, tree_parse, tree_serialize
// TODO functions:     tree_from_index
//
// Binary tree format (per entry, concatenated with no separators):
//   "<mode-as-ascii-octal> <name>\0<32-byte-binary-hash>"
//
// Example single entry (conceptual):
//   "100644 hello.txt\0" followed by 32 raw bytes of SHA-256

#include "tree.h"
#include "index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

// Forward declarations (implemented in object.c)
int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);

// ─── Mode Constants ─────────────────────────────────────────────────────────

#define MODE_FILE      0100644
#define MODE_EXEC      0100755
#define MODE_DIR       0040000

// ─── PROVIDED ───────────────────────────────────────────────────────────────

// Determine the object mode for a filesystem path.
uint32_t get_file_mode(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) return 0;

    if (S_ISDIR(st.st_mode))  return MODE_DIR;
    if (st.st_mode & S_IXUSR) return MODE_EXEC;
    return MODE_FILE;
}

// Parse binary tree data into a Tree struct safely.
// Returns 0 on success, -1 on parse error.
int tree_parse(const void *data, size_t len, Tree *tree_out) {
    tree_out->count = 0;
    const uint8_t *ptr = (const uint8_t *)data;
    const uint8_t *end = ptr + len;

    while (ptr < end && tree_out->count < MAX_TREE_ENTRIES) {
        TreeEntry *entry = &tree_out->entries[tree_out->count];

        // 1. Safely find the space character for the mode
        const uint8_t *space = memchr(ptr, ' ', end - ptr);
        if (!space) return -1; // Malformed data

        // Parse mode into an isolated buffer
        char mode_str[16] = {0};
        size_t mode_len = space - ptr;
        if (mode_len >= sizeof(mode_str)) return -1;
        memcpy(mode_str, ptr, mode_len);
        entry->mode = strtol(mode_str, NULL, 8);

        ptr = space + 1; // Skip space

        // 2. Safely find the null terminator for the name
        const uint8_t *null_byte = memchr(ptr, '\0', end - ptr);
        if (!null_byte) return -1; // Malformed data

        size_t name_len = null_byte - ptr;
        if (name_len >= sizeof(entry->name)) return -1;
        memcpy(entry->name, ptr, name_len);
        entry->name[name_len] = '\0'; // Ensure null-terminated

        ptr = null_byte + 1; // Skip null byte

        // 3. Read the 32-byte binary hash
        if (ptr + HASH_SIZE > end) return -1; 
        memcpy(entry->hash.hash, ptr, HASH_SIZE);
        ptr += HASH_SIZE;

        tree_out->count++;
    }
    return 0;
}

// Helper for qsort to ensure consistent tree hashing
static int compare_tree_entries(const void *a, const void *b) {
    return strcmp(((const TreeEntry *)a)->name, ((const TreeEntry *)b)->name);
}

// Serialize a Tree struct into binary format for storage.
// Caller must free(*data_out).
// Returns 0 on success, -1 on error.
int tree_serialize(const Tree *tree, void **data_out, size_t *len_out) {
    // Estimate max size: (6 bytes mode + 1 byte space + 256 bytes name + 1 byte null + 32 bytes hash) per entry
    size_t max_size = tree->count * 296; 
    uint8_t *buffer = malloc(max_size);
    if (!buffer) return -1;

    // Create a mutable copy to sort entries (Git requirement)
    Tree sorted_tree = *tree;
    qsort(sorted_tree.entries, sorted_tree.count, sizeof(TreeEntry), compare_tree_entries);

    size_t offset = 0;
    for (int i = 0; i < sorted_tree.count; i++) {
        const TreeEntry *entry = &sorted_tree.entries[i];
        
        // Write mode and name (%o writes octal correctly for Git standards)
        int written = sprintf((char *)buffer + offset, "%o %s", entry->mode, entry->name);
        offset += written + 1; // +1 to step over the null terminator written by sprintf
        
        // Write binary hash
        memcpy(buffer + offset, entry->hash.hash, HASH_SIZE);
        offset += HASH_SIZE;
    }

    *data_out = buffer;
    *len_out = offset;
    return 0;
}

// ─── TODO: Implement these ──────────────────────────────────────────────────

// Recursive helper to build tree levels from index entries.
// 
// Handles nested paths by grouping entries at each depth level.
// For entries like "src/main.c", at depth 0 creates "src" directory entry,
// then recursively processes entries under "src" at depth 1.
//
// Parameters:
//   entries:   array of index entries to process
//   count:     number of entries to consider
//   depth:     current directory depth (0 = root level)
//   id_out:    output parameter for this subtree's object ID
//
// Returns 0 on success, -1 on error.
static int write_tree_level(const IndexEntry *entries, int count, int depth, ObjectID *id_out) {
    Tree tree;
    tree.count = 0;
    
    int i = 0;
    while (i < count && tree.count < MAX_TREE_ENTRIES) {
        const IndexEntry *entry = &entries[i];
        
        // Extract the path component at this depth level
        // For "src/main.c" at depth 0, extract "src"
        // For "src/main.c" at depth 1, extract "main.c"
        const char *path = entry->path;
        const char *component_start = path;
        const char *next_slash = NULL;
        int current_depth = 0;
        
        // Find the component at the desired depth
        for (const char *p = path; *p; p++) {
            if (*p == '/') {
                if (current_depth == depth) {
                    next_slash = p;
                    break;
                }
                current_depth++;
                component_start = p + 1;
            }
        }
        
        // Extract component name length
        size_t component_len;
        if (next_slash) {
            component_len = next_slash - component_start;
        } else {
            component_len = strlen(component_start);
        }
        
        // Create tree entry
        TreeEntry *tree_entry = &tree.entries[tree.count];
        if (component_len >= sizeof(tree_entry->name)) {
            return -1;  // Component name too long
        }
        
        strncpy(tree_entry->name, component_start, component_len);
        tree_entry->name[component_len] = '\0';
        
        if (next_slash) {
            // This is a directory - recursively build subtree
            tree_entry->mode = MODE_DIR;
            
            // Collect all entries belonging to this directory
            int subentries_count = 0;
            int j = i;
            while (j < count && subentries_count < MAX_TREE_ENTRIES) {
                const IndexEntry *sub_entry = &entries[j];
                const char *sub_path = sub_entry->path;
                const char *sub_component_start = sub_path;
                const char *sub_next_slash = NULL;
                int sub_depth = 0;
                
                // Extract component at this depth
                for (const char *p = sub_path; *p; p++) {
                    if (*p == '/') {
                        if (sub_depth == depth) {
                            sub_next_slash = p;
                            break;
                        }
                        sub_depth++;
                        sub_component_start = p + 1;
                    }
                }
                
                size_t sub_component_len;
                if (sub_next_slash) {
                    sub_component_len = sub_next_slash - sub_component_start;
                } else {
                    sub_component_len = strlen(sub_component_start);
                }
                
                // Check if component matches current directory
                if (sub_component_len == component_len &&
                    strncmp(sub_component_start, component_start, component_len) == 0) {
                    subentries_count++;
                    j++;
                } else {
                    break;
                }
            }
            
            // Recursively build subtree for this directory
            if (write_tree_level(&entries[i], subentries_count, depth + 1, &tree_entry->hash) != 0) {
                return -1;
            }
            
            i = j;
        } else {
            // This is a file - use entry's mode and hash
            tree_entry->mode = entry->mode;
            tree_entry->hash = entry->hash;
            i++;
        }
        
        tree.count++;
    }
    
    // Serialize and write this tree level to object store
    void *data = NULL;
    size_t len = 0;
    if (tree_serialize(&tree, &data, &len) != 0) {
        return -1;
    }
    
    if (object_write(OBJ_TREE, data, len, id_out) != 0) {
        free(data);
        return -1;
    }
    
    free(data);
    return 0;
}

// Build a tree hierarchy from the current index and write all tree
// objects to the object store.
//
// HINTS - Useful functions and concepts for this phase:
//   - index_load      : load the staged files into memory
//   - strchr          : find the first '/' in a path to separate directories from files
//   - strncmp         : compare prefixes to group files belonging to the same subdirectory
//   - Recursion       : you will likely want to create a recursive helper function 
//                       (e.g., `write_tree_level(entries, count, depth)`) to handle nested dirs.
//   - tree_serialize  : convert your populated Tree struct into a binary buffer
//   - object_write    : save that binary buffer to the store as OBJ_TREE
//
// Returns 0 on success, -1 on error.
int tree_from_index(ObjectID *id_out) {
    Index index;
    
    // Load the index from disk
    if (index_load(&index) != 0) {
        return -1;
    }
    
    // Handle empty index case
    if (index.count == 0) {
        Tree empty_tree;
        empty_tree.count = 0;
        
        void *data = NULL;
        size_t len = 0;
        if (tree_serialize(&empty_tree, &data, &len) != 0) {
            return -1;
        }
        
        if (object_write(OBJ_TREE, data, len, id_out) != 0) {
            free(data);
            return -1;
        }
        
        free(data);
        return 0;
    }
    
    // Build tree hierarchy recursively starting at root level (depth 0)
    return write_tree_level(index.entries, index.count, 0, id_out);
}