#include "kilo.h"
#include <dirent.h>
#include <string.h>
#include "dirtree.h"

DirTreeNode* createTreeNode(const char* path, DirTreeNode* parent, NodeType type) {
// parent is passed to provide bidirecational relationship to nodes.
    
    // This allocates enough bytes of memory to store the singular node - allocates the size of the node.
    DirTreeNode *node = malloc(sizeof(DirTreeNode));
    if (!node) {
        debug_printf("[src/dirtree.c] Directory tree node failed to allocate memory.");
        return NULL; // returns NULL if memory couldn't be allocated.
    }

    // Initialise common fields for files and dirs.
    node->name = strdup(basename(path));
    node->full_path = strdup(path);
    node->type = type;
    node->parent = parent;

    node->num_capacity = 0;
    node->num_children = 0;
    node->children = NULL;
    node->is_expanded = false;

    if (type == NODE_DIR) {
        // Initialising dir-only fields
        DIR* dirstream = opendir(path);
        if (dirstream == NULL) {
            node->dirstream = NULL;
            debug_printf("[src/dirtree.c] Failed to get dirstream from %s.", path);
            // free(node->dirstream); // Cannot free; it's NULL
            // closedir(dirsteam); cannot close; it's NULL
            free(node->name); // free name allocated by strdup 
            free(node); // free node allocated by malloc
            return NULL;
        }

        node->dirstream = dirstream;

        int filedesc = dirfd(node->dirstream);
        if (filedesc == -1) { // dirfd returns -1 on failure, not 0 or NULL
            node->fd = -1;
            debug_printf("[src/dirtree.c] Failed to get file descriptor from dirstream.");
            // free(node->fd); // Cannot free node->fd. It's an int not allocated memory.
            // Clean up resources; close stream & FD & 
            // free name and node by strdup & malloc respectively
            closedir(node->dirstream);
            free(node->name);
            free(node);
            return NULL;
        }
        // assign fd after successful check
        node->fd = filedesc;

    } else {
        
        // Don't need these for files.
        node->dirstream = NULL;
        node->fd = -1;
    }
    
    return node;

}

int expandDirNode(DirTreeNode *node) {
    if (!node || node->type != NODE_DIR || !node->dirstream) return -1;

    struct dirent* entry;
    char path_buf[PATH_MAX];

    rewinddir(node->dirstream);
    while ((entry = readdir(node->dirstream)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        // Construct child path based on parent's full_path
        size_t parent_path_len = strlen(node->full_path);
        size_t d_name_len = strlen(entry->d_name);

            if (parent_path_len + 1 + d_name_len + 1 <= PATH_MAX) {
                // Copy parent path first
                strcpy(path_buf, node->full_path);
                // Append separator and child name safely
                snprintf(path_buf + parent_path_len, // Start writing after parent path
                        PATH_MAX - parent_path_len, // Remaining space
                        "/%s", entry->d_name); // Format


                // Now path_buf holds the full path to the child entry
                NodeType type = (entry->d_type == DT_DIR ? NODE_DIR : NODE_FILE); // May want to use lstat instead of DT_DIR?
                DirTreeNode* child = createTreeNode(path_buf, node, type);

                if (child) {
                    if (node->num_children >= node->num_capacity) {
                        size_t new_capacity = node->num_capacity == 0 ? 8 : node->num_capacity * 2;
                        // Use temporary pointer for realloc result
                        DirTreeNode **temp_children = realloc(node->children, new_capacity * sizeof(DirTreeNode*));

                        if (!temp_children) {
                            perror("realloc failed");
                            if (child) freeTreeNode(child);
                            return -1;
                        }
                        node->children = temp_children;
                        node->num_capacity = new_capacity;
                    }
                    node->children[node->num_children++] = child;
                }
                // else: createTreeNode failed, error handled within that function (returned NULL)
                // Might want to log this failure here too.
            } else {
                 // Use fprintf for standard error stream
                 fprintf(stderr, "Error: Constructed path exceeds PATH_MAX for %s/%s\n",
                         path_buf, entry->d_name);
                 // Continue to next entry if path construction fails
                 continue;
            }
    } // end while loop

    node->is_expanded = true;

    return 0; // Success
}


// Frees tree node's memory from heap.
int freeTreeNode(DirTreeNode *node) {

    if (!node) return -1;

    // recursively free all of the node's children
    if (node->type == NODE_DIR) {
        for (size_t child = 0; child < node->num_children; child++) {
            freeTreeNode(node->children[child]);
        }

        // Need to free the children array itself after allocating it with realloc.
        // The above frees everything else except the children array... until this line
        free(node->children);
        if (node->dirstream) {
            // closes both dirstream & file descriptor
            closedir(node->dirstream);
        }

    }

    free(node->name); // free name allocated by strdup
    // free(node->type); // Do not free; it's an enum, part of the struct itself
    // free(node->parent); // Do not free; it's a pointer that will be freed by this func. May cause double-free error
    free(node->full_path);
    free(node); // free node allocated by malloc

    return 0;
}

DirTreeNode* findOrCreateDirTreeNode(const char *full_path) {
    // 1. Handle Root Case (as before)
    if (strcmp(full_path, E.project_root) == 0) {
        // Ensure E.panel_root_node exists (create if needed)
        if (!E.panel_root_node) {
            E.panel_root_node = createTreeNode(E.project_root, NULL, NODE_DIR);
        }
        return E.panel_root_node; // TODO: Add error check for creation
    }

    // 2. Start Traversal from Root
    DirTreeNode *current_node = E.panel_root_node;
    if (!current_node) return NULL; // Cannot proceed without a root

    // 3. Tokenize/Parse the Path
    // Make a mutable copy of full_path because strtok modifies it
    char *path_copy = strdup(full_path);
    if (!path_copy) return NULL; // Allocation failed

    // Skip leading '/' if path starts relative to root (adjust based on E.project_root structure)
    char *token = strtok(path_copy, "/"); // Adjust delimiter if needed (e.g., Windows \)

    while (token != NULL && current_node != NULL) {
        // 4. Ensure Current Node is an Expanded Directory
        if (current_node->type != NODE_DIR) {
            current_node = NULL; // Cannot traverse into a file node
            break;
        }
        if (!current_node->is_expanded) {
            if (expandDirNode(current_node) != 0) { // Try to expand
                current_node = NULL; // Expansion failed
                break;
            }
            // Consider sorting children after expansion?
            //  if (current_node->num_children > 0) {
            //      qsort(current_node->children, current_node->num_children, sizeof(DirTreeNode*), compareDirTreeNodes);
            //  }
        }

        // 5. Search Children for the Next Path Component ('token')
        DirTreeNode *found_child = NULL;
        for (size_t i = 0; i < current_node->num_children; i++) {
            if (strcmp(current_node->children[i]->name, token) == 0) {
                found_child = current_node->children[i];
                break;
            }
        }

        // 6. Handle Found/Not Found
        if (found_child) {
            current_node = found_child; // Descend into the found child
        } else {
            // Node for 'token' doesn't exist in the tree under current_node
            // Option A: Fail - return NULL (simplest)
            current_node = NULL;
            break;
            // Option B: Create it - More complex, requires stat(path_so_far + "/" + token)
            //           to determine type (file/dir) before calling createTreeNode,
            //           then add it to current_node->children (realloc needed).
            //           Let's stick with Option A for now.
        }

        // 7. Get Next Path Component
        token = strtok(NULL, "/");
    }

    free(path_copy); // Free the mutable copy
    return current_node; // Returns the found node, or NULL if not found/error
}
