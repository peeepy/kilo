#ifndef KILO_COMPONENTS_H_
#define KILO_COMPONENTS_H_

// Component types

typedef enum ComponentType {
    COMPONENT_TEXTAREA,   // Main text editing area
    COMPONENT_STATUSBAR,  // Status bar at bottom
    COMPONENT_TABLINE,    // Tab/buffer line at top
    COMPONENT_PANEL,      // Side panel (like directory tree)
    COMPONENT_MESSAGEBAR, // Message bar at bottom
    COMPONENT_FLOATING,   // Floating window/dialog
    COMPONENT_CUSTOM      // Custom component
} ComponentType;

// Component position (for fixed components)
typedef enum ComponentPosition {
    POSITION_TOP,
    POSITION_BOTTOM,
    POSITION_LEFT,
    POSITION_RIGHT,
    POSITION_CENTER,
    POSITION_FLOATING
} ComponentPosition;

// Structure to hold component layout information
typedef struct ComponentLayout {
    int x;                // X position (1-based)
    int y;                // Y position (1-based)
    int width;            // Width in columns
    int height;           // Height in rows
    bool visible;         // Whether component is visible
    ComponentPosition position; // Where component is positioned
    ComponentType type;   // Type of component
    char *name;           // Component name (for lookup)
    int z_index;          // Drawing order (higher = on top)
    int lua_callback_ref; // Reference to Lua callback function
} ComponentLayout;

// Structure to hold editor component system
typedef struct ComponentSystem {
    ComponentLayout *components;      // Array of registered components
    int component_count;              // Number of components
    int component_capacity;           // Capacity of components array
    int screen_width;                 // Current screen width
    int screen_height;                // Current screen height
    ComponentLayout *textarea;        // Quick reference to textarea component
    ComponentLayout *statusbar;       // Quick reference to statusbar component
    ComponentLayout *tabline;         // Quick reference to tabline component
    ComponentLayout *messagebar;      // Quick reference to messagebar component
    ComponentLayout *panel;           // Quick reference to panel component
} ComponentSystem;

// Global component system
ComponentSystem component_system;


//prototypes
void initComponentSystem();
void freeComponentSystem();
ComponentLayout* registerComponent(const char *name, ComponentType type, ComponentPosition position);
void registerDefaultComponents();
void calculateLayout();

void drawComponents(struct abuf *ab);
void drawTextareaComponent(struct abuf *ab, ComponentLayout *component);
void drawStatusbarComponent(struct abuf *ab, ComponentLayout *component);
void drawTablineComponent(struct abuf *ab, ComponentLayout *component);
void drawMessagebarComponent(struct abuf *ab, ComponentLayout *component);
void drawPanelComponent(struct abuf *ab, ComponentLayout *component);
void drawCustomComponent(struct abuf *ab, ComponentLayout *component);



#endif