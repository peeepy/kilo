// In ui.c
#include <stdlib.h>
#include <string.h>
#include "kilo.h" // Access global E, die()
#include "ui.h"

// Function to initialize (called from initEditor)
void uiInitialize() {
    E.ui_components = NULL;
    E.ui_component_count = 0;
    E.ui_component_capacity = 0;
}

// Function to register a new component
void uiRegisterComponent(const char *name, UIPosition pos, int height, void (*draw_func)(struct abuf *), bool initially_enabled, int order) {
    // Resize array if needed
    if (E.ui_component_count >= E.ui_component_capacity) {
        int new_capacity = E.ui_component_capacity == 0 ? 4 : E.ui_component_capacity * 2;
        UIComponent *new_array = realloc(E.ui_components, sizeof(UIComponent) * new_capacity);
        if (!new_array) {
            die("Failed to allocate memory for UI components"); // Or handle more gracefully
        }
        E.ui_components = new_array;
        E.ui_component_capacity = new_capacity;
    }

    // Add the new component
    UIComponent *comp = &E.ui_components[E.ui_component_count++];
    comp->name = name; // Be careful with string lifetimes if names aren't literals
    comp->position = pos;
    comp->height = height;
    comp->draw_func = draw_func;
    comp->enabled = initially_enabled;
    comp->order = order;

    // You might want to sort the array by position and then order here,
    // or do it during rendering. Sorting here might be slightly more efficient.
}

// Function to free registry memory (called via atexit)
void uiFreeComponents() {
    free(E.ui_components);
    E.ui_components = NULL;
    E.ui_component_count = 0;
    E.ui_component_capacity = 0;
}

// Optional: Function to enable/disable components by name
void uiEnableComponent(const char *name, bool enable) {
    for (int i = 0; i < E.ui_component_count; i++) {
        // Use strcmp only if component name is guaranteed non-NULL
        if (E.ui_components[i].name && strcmp(E.ui_components[i].name, name) == 0) {
            E.ui_components[i].enabled = enable;
            return; // Found and updated
        }
    }
    debug_printf("Component not found to enable: %s", name);
}