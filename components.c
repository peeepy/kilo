#include "kilo.h"
#include "components.h"

/**
 * Improved Layout Implementation for Kilo Editor
 * 
 * This implementation provides a Neovim-like layout system where:
 * - C core determines the available space and manages component placement
 * - Lua defines content and styling for components
 * - Components can request size hints which C tries to accommodate
 * 
 * The implementation includes:
 * 1. Component registration system
 * 2. Layout calculation pipeline
 * 3. Drawing functions
 * 4. Lua API for component configuration
 */

// --- Component Structure and Registration ---

// Initialize the component system
void initComponentSystem() {
    // Allocate initial component array
    component_system.component_capacity = 10;
    component_system.components = malloc(sizeof(ComponentLayout) * component_system.component_capacity);
    component_system.component_count = 0;
    
    // Initialize references to NULL
    component_system.textarea = NULL;
    component_system.statusbar = NULL;
    component_system.tabline = NULL;
    component_system.messagebar = NULL;
    component_system.panel = NULL;
    
    // Get initial screen dimensions
    getWindowSize(&component_system.screen_height, &component_system.screen_width);
    
    // Register default components
    registerDefaultComponents();
}

// Clean up component system resources
void freeComponentSystem() {
    for (int i = 0; i < component_system.component_count; i++) {
        free(component_system.components[i].name);
    }
    free(component_system.components);
}

// Register a new component
ComponentLayout* registerComponent(const char *name, ComponentType type, ComponentPosition position) {
    // Ensure we have capacity
    if (component_system.component_count >= component_system.component_capacity) {
        component_system.component_capacity *= 2;
        ComponentLayout *new_components = realloc(component_system.components, 
            sizeof(ComponentLayout) * component_system.component_capacity);
        if (!new_components) {
            debug_printf("Failed to allocate memory for components");
            return NULL;
        }
        component_system.components = new_components;
    }
    
    // Create new component
    ComponentLayout *component = &component_system.components[component_system.component_count++];
    component->name = strdup(name);
    component->type = type;
    component->position = position;
    component->visible = true;
    component->lua_callback_ref = LUA_NOREF;
    component->z_index = 0;
    
    // Set default dimensions based on position
    switch (position) {
        case POSITION_TOP:
            component->x = 1;
            component->y = 1;
            component->width = component_system.screen_width;
            component->height = 1;
            break;
        case POSITION_BOTTOM:
            component->x = 1;
            component->y = component_system.screen_height;
            component->width = component_system.screen_width;
            component->height = 1;
            break;
        case POSITION_LEFT:
            component->x = 1;
            component->y = 2; // Assume below tabline
            component->width = 20;
            component->height = component_system.screen_height - 2;
            break;
        case POSITION_RIGHT:
            component->x = component_system.screen_width - 20 + 1;
            component->y = 2; // Assume below tabline
            component->width = 20;
            component->height = component_system.screen_height - 2;
            break;
        case POSITION_CENTER:
            component->x = 1;
            component->y = 2; // Assume below tabline
            component->width = component_system.screen_width;
            component->height = component_system.screen_height - 3; // Leave space for status and message
            break;
        case POSITION_FLOATING:
            // Floating windows will be positioned by their callbacks
            component->x = (component_system.screen_width - 40) / 2 + 1;
            component->y = (component_system.screen_height - 10) / 2 + 1;
            component->width = 40;
            component->height = 10;
            component->z_index = 10; // Floating windows on top by default
            break;
    }
    
    // Store quick references to standard components
    if (strcmp(name, "textarea") == 0) {
        component_system.textarea = component;
    } else if (strcmp(name, "statusbar") == 0) {
        component_system.statusbar = component;
    } else if (strcmp(name, "tabline") == 0) {
        component_system.tabline = component;
    } else if (strcmp(name, "messagebar") == 0) {
        component_system.messagebar = component;
    } else if (strcmp(name, "panel") == 0) {
        component_system.panel = component;
    }
    
    return component;
}

// Register default components
void registerDefaultComponents() {
    // Register standard components with default positions and dimensions
    registerComponent("tabline", COMPONENT_TABLINE, POSITION_TOP);
    registerComponent("statusbar", COMPONENT_STATUSBAR, POSITION_BOTTOM);
    registerComponent("messagebar", COMPONENT_MESSAGEBAR, POSITION_BOTTOM);
    registerComponent("textarea", COMPONENT_TEXTAREA, POSITION_CENTER);
    registerComponent("panel", COMPONENT_PANEL, POSITION_LEFT);
    
    // Hide panel by default
    if (component_system.panel) {
        component_system.panel->visible = false;
    }
    
    // Set z-indices for proper layering
    if (component_system.tabline) component_system.tabline->z_index = 1;
    if (component_system.statusbar) component_system.statusbar->z_index = 1;
    if (component_system.messagebar) component_system.messagebar->z_index = 2; // Above statusbar
    if (component_system.textarea) component_system.textarea->z_index = 0;
    if (component_system.panel) component_system.panel->z_index = 0;
}

// Find a component by name
ComponentLayout* findComponent(const char *name) {
    for (int i = 0; i < component_system.component_count; i++) {
        if (strcmp(component_system.components[i].name, name) == 0) {
            return &component_system.components[i];
        }
    }
    return NULL;
}

// --- Layout Calculation ---

// Calculate the layout of all components
void calculateLayout() {
    // Get current screen dimensions
    int old_width = component_system.screen_width;
    int old_height = component_system.screen_height;
    getWindowSize(&component_system.screen_height, &component_system.screen_width);
    
    // Check if screen dimensions have changed
    bool dimensions_changed = (old_width != component_system.screen_width || 
                              old_height != component_system.screen_height);
    
    // Sort components by z-index for layout calculation
    // (This is a simple bubble sort - could be quicksort for larger lists)
    for (int i = 0; i < component_system.component_count - 1; i++) {
        for (int j = 0; j < component_system.component_count - i - 1; j++) {
            if (component_system.components[j].z_index > component_system.components[j + 1].z_index) {
                // Swap components
                ComponentLayout temp = component_system.components[j];
                component_system.components[j] = component_system.components[j + 1];
                component_system.components[j + 1] = temp;
                
                // Update quick references if needed
                if (component_system.textarea == &component_system.components[j]) {
                    component_system.textarea = &component_system.components[j + 1];
                } else if (component_system.textarea == &component_system.components[j + 1]) {
                    component_system.textarea = &component_system.components[j];
                }
                // Similar updates for other quick references...
            }
        }
    }
    
    // Process fixed components first
    for (int i = 0; i < component_system.component_count; i++) {
        ComponentLayout *component = &component_system.components[i];
        
        // Skip invisible components
        if (!component->visible) continue;
        
        // Skip textarea (will be calculated last)
        if (component->type == COMPONENT_TEXTAREA) continue;
        
        // Skip floating components (managed by their callbacks)
        if (component->position == POSITION_FLOATING) continue;
        
        // Call Lua callback to get size hints if available
        int lua_height = -1, lua_width = -1;
        if (component->lua_callback_ref != LUA_NOREF) {
            lua_State *L = getLuaState();
            if (L) {
                lua_rawgeti(L, LUA_REGISTRYINDEX, component->lua_callback_ref);
                if (lua_pcall(L, 0, 2, 0) == LUA_OK) {
                    // Check options table for height/width hints
                    if (lua_istable(L, -1)) {
                        lua_getfield(L, -1, "height");
                        if (lua_isnumber(L, -1)) {
                            lua_height = lua_tointeger(L, -1);
                        }
                        lua_pop(L, 1);
                        
                        lua_getfield(L, -1, "width");
                        if (lua_isnumber(L, -1)) {
                            lua_width = lua_tointeger(L, -1);
                        }
                        lua_pop(L, 1);
                    }
                    lua_pop(L, 2); // Pop segments and options
                } else {
                    // Handle error
                    debug_printf("Error calling component Lua callback: %s", lua_tostring(L, -1));
                    lua_pop(L, 1); // Pop error message
                }
            }
        }
        
        // Update component dimensions based on screen size and hints
        switch (component->position) {
            case POSITION_TOP:
                component->x = 1;
                component->width = component_system.screen_width;
                // Apply height hint if provided
                if (lua_height > 0) component->height = lua_height;
                break;
                
            case POSITION_BOTTOM:
                component->x = 1;
                component->width = component_system.screen_width;
                // Apply height hint if provided
                if (lua_height > 0) component->height = lua_height;
                // Adjust y position based on height
                component->y = component_system.screen_height - component->height + 1;
                break;
                
            case POSITION_LEFT:
                component->y = 1;
                if (component_system.tabline && component_system.tabline->visible) {
                    component->y += component_system.tabline->height;
                }
                // Apply width hint if provided
                if (lua_width > 0) component->width = lua_width;
                // Calculate height based on available space
                component->height = component_system.screen_height - component->y + 1;
                if (component_system.statusbar && component_system.statusbar->visible) {
                    component->height -= component_system.statusbar->height;
                }
                if (component_system.messagebar && component_system.messagebar->visible) {
                    component->height -= component_system.messagebar->height;
                }
                break;
                
            case POSITION_RIGHT:
                component->y = 1;
                if (component_system.tabline && component_system.tabline->visible) {
                    component->y += component_system.tabline->height;
                }
                // Apply width hint if provided
                if (lua_width > 0) component->width = lua_width;
                // Adjust x position based on width
                component->x = component_system.screen_width - component->width + 1;
                // Calculate height based on available space
                component->height = component_system.screen_height - component->y + 1;
                if (component_system.statusbar && component_system.statusbar->visible) {
                    component->height -= component_system.statusbar->height;
                }
                if (component_system.messagebar && component_system.messagebar->visible) {
                    component->height -= component_system.messagebar->height;
                }
                break;
                
            default:
                // Other positions handled elsewhere
                break;
        }
        
        // Ensure dimensions are valid
        if (component->height < 1) component->height = 1;
        if (component->width < 1) component->width = 1;
    }
    
    // Finally calculate textarea dimensions based on other components
    if (component_system.textarea && component_system.textarea->visible) {
        // Start with full screen
        component_system.textarea->x = 1;
        component_system.textarea->y = 1;
        component_system.textarea->width = component_system.screen_width;
        component_system.textarea->height = component_system.screen_height;
        
        // Adjust for tabline
        if (component_system.tabline && component_system.tabline->visible) {
            component_system.textarea->y += component_system.tabline->height;
            component_system.textarea->height -= component_system.tabline->height;
        }
        
        // Adjust for statusbar and messagebar
        if (component_system.statusbar && component_system.statusbar->visible) {
            component_system.textarea->height -= component_system.statusbar->height;
        }
        if (component_system.messagebar && component_system.messagebar->visible) {
            component_system.textarea->height -= component_system.messagebar->height;
        }
        
        // Adjust for left panel
        if (component_system.panel && component_system.panel->visible && 
            component_system.panel->position == POSITION_LEFT) {
            component_system.textarea->x += component_system.panel->width;
            component_system.textarea->width -= component_system.panel->width;
        }
        
        // Adjust for right panel
        if (component_system.panel && component_system.panel->visible && 
            component_system.panel->position == POSITION_RIGHT) {
            component_system.textarea->width -= component_system.panel->width;
        }
        
        // Ensure dimensions are valid
        if (component_system.textarea->height < 1) component_system.textarea->height = 1;
        if (component_system.textarea->width < 1) component_system.textarea->width = 1;
        
        // Update editor global dimensions for text area
        E.content_start_row = component_system.textarea->y;
        E.content_start_col = component_system.textarea->x;
        E.screenrows = component_system.textarea->height;
        E.content_width = component_system.textarea->width;
    }
}

// --- Drawing Functions ---

// Draw all components
void drawComponents(struct abuf *ab) {
    // Sort components by z-index for drawing order
    // (This is a simple bubble sort - could be quicksort for larger lists)
    for (int i = 0; i < component_system.component_count - 1; i++) {
        for (int j = 0; j < component_system.component_count - i - 1; j++) {
            if (component_system.components[j].z_index > component_system.components[j + 1].z_index) {
                // Swap components
                ComponentLayout temp = component_system.components[j];
                component_system.components[j] = component_system.components[j + 1];
                component_system.components[j + 1] = temp;
                
                // Update quick references if needed
                if (component_system.textarea == &component_system.components[j]) {
                    component_system.textarea = &component_system.components[j + 1];
                } else if (component_system.textarea == &component_system.components[j + 1]) {
                    component_system.textarea = &component_system.components[j];
                }
                // Similar updates for other quick references...
            }
        }
    }
    
    // Draw components in order of z-index (lowest first)
    for (int i = 0; i < component_system.component_count; i++) {
        ComponentLayout *component = &component_system.components[i];
        if (!component->visible) continue;
        
        // Draw the component based on its type
        switch (component->type) {
            case COMPONENT_TEXTAREA:
                drawTextareaComponent(ab, component);
                break;
            case COMPONENT_STATUSBAR:
                drawStatusbarComponent(ab, component);
                break;
            case COMPONENT_TABLINE:
                drawTablineComponent(ab, component);
                break;
            case COMPONENT_MESSAGEBAR:
                drawMessagebarComponent(ab, component);
                break;
            case COMPONENT_PANEL:
                drawPanelComponent(ab, component);
                break;
            case COMPONENT_FLOATING:
            case COMPONENT_CUSTOM:
                drawCustomComponent(ab, component);
                break;
        }
    }
    
    // Draw overlays (these are managed separately)
    for (int i = 0; i < E.num_active_overlays; i++) {
        OverlayInstance *overlay = &E.active_overlays[i];
        if (overlay->draw_func) {
            overlay->draw_func(ab, overlay->state);
        }
    }
    
    // Draw debug overlay if active
    if (debug_overlay_active) {
        editorDrawDebugOverlay(ab);
    }
}

// Draw textarea component
void drawTextareaComponent(struct abuf *ab, ComponentLayout *component) {
    // Draw the text area using its calculated dimensions
    editorDrawRows(ab, component->x, component->y, component->width, component->height);
}

// Draw statusbar component
void drawStatusbarComponent(struct abuf *ab, ComponentLayout *component) {
    if (component->lua_callback_ref != LUA_NOREF) {
        // Position cursor at component start
        char pos_buf[32];
        snprintf(pos_buf, sizeof(pos_buf), "\x1b[%d;%dH", component->y, component->x);
        abAppend(ab, pos_buf, strlen(pos_buf));
        
        // Call Lua callback to get content
        lua_State *L = getLuaState();
        if (!L) {
            editorDrawDefaultStatusBar(ab);
            return;
        }
        
        lua_rawgeti(L, LUA_REGISTRYINDEX, component->lua_callback_ref);
        if (lua_pcall(L, 0, 2, 0) != LUA_OK) {
            const char *error_msg = lua_tostring(L, -1);
            debug_printf("Error calling statusbar Lua func: %s", error_msg);
            lua_pop(L, 1);
            editorDrawDefaultStatusBar(ab);
            return;
        }
        
        // Process segments and options
        if (!lua_istable(L, -2)) {
            debug_printf("Statusbar Lua function didn't return segments table");
            lua_pop(L, 2);
            editorDrawDefaultStatusBar(ab);
            return;
        }
        
        // Get default colors
        char *status_fg = E.theme.ui_status_fg;
        char *status_bg = E.theme.ui_status_bg;
        
        // Check options table for colors
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "fg");
            if (lua_isstring(L, -1)) {
                const char *n = lua_tostring(L, -1);
                char* t = getThemeColorByName(n);
                if(t) status_fg = t; else if(n[0] == '#') status_fg = (char*)n;
            }
            lua_pop(L, 1);
            
            lua_getfield(L, -1, "bg");
            if (lua_isstring(L, -1)) {
                const char *n = lua_tostring(L, -1);
                char* t = getThemeColorByName(n);
                if(t) status_bg = t; else if(n[0] == '#') status_bg = (char*)n;
            }
            lua_pop(L, 1);
        }
        
        // Clear background
        applyTrueColor(ab, status_fg, status_bg);
        abAppend(ab, "\x1b[K", 3);
        
        // Process segments
        lua_Integer segment_count = lua_rawlen(L, -2);
        int left_width = 0;
        int right_width = 0;
        
        // First pass: calculate widths for left and right segments
        for (int i = 1; i <= segment_count; i++) {
            lua_rawgeti(L, -2, i);
            if (!lua_istable(L, -1)) {
                lua_pop(L, 1);
                continue;
            }
            
            // Get alignment
            int alignment = 0; // Default left
            lua_getfield(L, -1, "align");
            if (lua_isstring(L, -1)) {
                if (strcmp(lua_tostring(L, -1), "right") == 0) alignment = 2;
            }
            lua_pop(L, 1);
            
            // Get text and calculate width
            lua_getfield(L, -1, "text");
            if (lua_isstring(L, -1)) {
                const char *text = lua_tostring(L, -1);
                int width = calculate_visible_length_ansi(text);
                
                if (alignment == 0) {
                    left_width += width;
                } else if (alignment == 2) {
                    right_width += width;
                }
            }
            lua_pop(L, 1);
            
            lua_pop(L, 1); // Pop segment table
        }
        
        // Second pass: render segments
        int current_x = component->x;
        
        // Render left segments
        for (int i = 1; i <= segment_count; i++) {
            lua_rawgeti(L, -2, i);
            if (!lua_istable(L, -1)) {
                lua_pop(L, 1);
                continue;
            }
            
            // Get alignment
            int alignment = 0; // Default left
            lua_getfield(L, -1, "align");
            if (lua_isstring(L, -1)) {
                if (strcmp(lua_tostring(L, -1), "right") == 0) alignment = 2;
            }
            lua_pop(L, 1);
            
            // Skip right-aligned segments for now
            if (alignment != 0) {
                lua_pop(L, 1);
                continue;
            }
            
            // Get text
            lua_getfield(L, -1, "text");
            if (!lua_isstring(L, -1)) {
                lua_pop(L, 2); // Pop text and segment table
                continue;
            }
            const char *text = lua_tostring(L, -1);
            int text_len = strlen(text);
            lua_pop(L, 1);
            
            // Get colors
            char *fg = status_fg;
            char *bg = status_bg;
            
            lua_getfield(L, -1, "fg");
            if (lua_isstring(L, -1)) {
                const char *n = lua_tostring(L, -1);
                char* t = getThemeColorByName(n);
                if(t) fg = t; else if(n[0] == '#') fg = (char*)n;
            }
            lua_pop(L, 1);
            
            lua_getfield(L, -1, "bg");
            if (lua_isstring(L, -1)) {
                const char *n = lua_tostring(L, -1);
                char* t = getThemeColorByName(n);
                if(t) bg = t; else if(n[0] == '#') bg = (char*)n;
            }
            lua_pop(L, 1);
            
            // Position cursor and draw segment
            char pos_buf[32];
            snprintf(pos_buf, sizeof(pos_buf), "\x1b[%d;%dH", component->y, current_x);
            abAppend(ab, pos_buf, strlen(pos_buf));
            
            // Apply colors and draw text
            applyTrueColor(ab, fg, bg);
            abAppend(ab, text, text_len);
            
            // Update current position
            current_x += calculate_visible_length_ansi(text);
            
            lua_pop(L, 1); // Pop segment table
        }
        
        // Calculate position for right-aligned segments
        if (right_width > 0) {
            int right_start = component->x + component->width - right_width;
            
            // Fill gap between left and right segments
            if (right_start > current_x) {
                char pos_buf[32];
                snprintf(pos_buf, sizeof(pos_buf), "\x1b[%d;%dH", component->y, current_x);
                abAppend(ab, pos_buf, strlen(pos_buf));
                applyTrueColor(ab, status_fg, status_bg);
                
                for (int i = 0; i < right_start - current_x; i++) {
                    abAppend(ab, " ", 1);
                }
            }
            
            // Render right segments
            current_x = right_start;
            for (int i = 1; i <= segment_count; i++) {
                lua_rawgeti(L, -2, i);
                if (!lua_istable(L, -1)) {
                    lua_pop(L, 1);
                    continue;
                }
                
                // Get alignment
                int alignment = 0; // Default left
                lua_getfield(L, -1, "align");
                if (lua_isstring(L, -1)) {
                    if (strcmp(lua_tostring(L, -1), "right") == 0) alignment = 2;
                }
                lua_pop(L, 1);
                
                // Skip left-aligned segments
                if (alignment != 2) {
                    lua_pop(L, 1);
                    continue;
                }
                
                // Get text
                lua_getfield(L, -1, "text");
                if (!lua_isstring(L, -1)) {
                    lua_pop(L, 2); // Pop text and segment table
                    continue;
                }
                const char *text = lua_tostring(L, -1);
                int text_len = strlen(text);
                lua_pop(L, 1);
                
                // Get colors
                char *fg = status_fg;
                char *bg = status_bg;
                
                lua_getfield(L, -1, "fg");
                if (lua_isstring(L, -1)) {
                    const char *n = lua_tostring(L, -1);
                    char* t = getThemeColorByName(n);
                    if(t) fg = t; else if(n[0] == '#') fg = (char*)n;
                }
                lua_pop(L, 1);
                
                lua_getfield(L, -1, "bg");
                if (lua_isstring(L, -1)) {
                    const char *n = lua_tostring(L, -1);
                    char* t = getThemeColorByName(n);
                    if(t) bg = t; else if(n[0] == '#') bg = (char*)n;
                }
                lua_pop(L, 1);
                
                // Position cursor and draw segment
                char pos_buf[32];
                snprintf(pos_buf, sizeof(pos_buf), "\x1b[%d;%dH", component->y, current_x);
                abAppend(ab, pos_buf, strlen(pos_buf));
                
                // Apply colors and draw text
                applyTrueColor(ab, fg, bg);
                abAppend(ab, text, text_len);
                
                // Update current position
                current_x += calculate_visible_length_ansi(text);
                
                lua_pop(L, 1); // Pop segment table
            }
        }
        
        // Reset colors
        applyThemeDefaultColor(ab);
        
        // Clean up Lua stack
        lua_pop(L, 2); // Pop segments and options tables
    } else {
        // No Lua callback, use default implementation
        editorDrawDefaultStatusBar(ab);
    }
}

// TODO: Implement
// Draw tabline component
void drawTablineComponent(struct abuf *ab, ComponentLayout *component) {
    if (component->lua_callback_ref != LUA_NOREF) {
        // Similar to statusbar, call Lua and process segments
        // ...
        // This would be similar to the statusbar implementation but for tabline
        // ...
    } else {
        // No Lua callback, use default implementation
        editorDrawDefaultTabline(ab);
    }
}

// Draw messagebar component
void drawMessagebarComponent(struct abuf *ab, ComponentLayout *component) {
    // Position cursor at component start
    char pos_buf[32];
    snprintf(pos_buf, sizeof(pos_buf), "\x1b[%d;%dH", component->y, component->x);
    abAppend(ab, pos_buf, strlen(pos_buf));
    
    // Use the existing messagebar implementation
    editorDrawMessageBar(ab);
}

// Draw panel component (e.g., directory tree)
void drawPanelComponent(struct abuf *ab, ComponentLayout *component) {
    if (component->lua_callback_ref != LUA_NOREF) {
        // Call Lua to get panel content
        editorDrawDirTreeFixed(ab, component->x, component->y, component->width, component->height);
    } else {
        // Fallback to default panel implementation if needed
        // ...
    }
}

// Draw custom component using Lua callback
void drawCustomComponent(struct abuf *ab, ComponentLayout *component) {
    if (component->lua_callback_ref == LUA_NOREF) return;
    
    // Call Lua callback to get content
    lua_State *L = getLuaState();
    if (!L) return;
    
    lua_rawgeti(L, LUA_REGISTRYINDEX, component->lua_callback_ref);
    if (lua_pcall(L, 0, 2, 0) != LUA_OK) {
        const char *error_msg = lua_tostring(L, -1);
        debug_printf("Error calling custom component Lua func: %s", error_msg);
        lua_pop(L, 1);
        return;
    }
    
    // Check for segments table
    if (!lua_istable(L, -2)) {
        debug_printf("Custom component Lua function didn't return segments table");
        lua_pop(L, lua_gettop(L));
        return;
    }
    
    // Process segments similarly to statusbar
    // ...
    
    // Clean up Lua stack
    lua_pop(L, 2); // Pop segments and options tables
}
