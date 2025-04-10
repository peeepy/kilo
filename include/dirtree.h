#ifndef DIRTREE_H // Include guards are good practice
#define DIRTREE_H

#include <stdbool.h> // For bool
#include <stddef.h>  // For size_t
#include <stdio.h>   // Included by dirent.h usually, but good to be explicit if needed elsewhere
#include <dirent.h>  // For DIR* (needs _DEFAULT_SOURCE or similar)

typedef enum {
    NODE_FILE,
    NODE_DIR
} NodeType;

typedef struct DirTreeNode {
    char *name;
    char *full_path; // Full path (for operations)
    NodeType type;
    struct DirTreeNode *parent;

    // for dirs only
    int fd;
    DIR *dirstream; // Type DIR comes from <dirent.h>
    size_t num_children;
    size_t num_capacity;
    struct DirTreeNode **children;
    bool is_expanded;

} DirTreeNode;

// Prototypes
DirTreeNode* createTreeNode(const char* path, DirTreeNode* parent, NodeType type);
int expandDirNode(DirTreeNode *node);
int freeTreeNode(DirTreeNode *node);
DirTreeNode* findOrCreateDirTreeNode(const char *full_path);

#endif // DIRTREE_H