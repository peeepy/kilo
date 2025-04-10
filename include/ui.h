#ifndef UI_H
#define UI_H

#include <stdbool.h>

// Forward declare struct abuf or include its header if needed by draw_func typedef
struct abuf;

typedef enum { UI_POS_TOP, UI_POS_BOTTOM, UI_POS_RIGHT, UI_POS_LEFT, UI_POS_FLOAT } UIPosition;

typedef struct {
    const char *name;
    UIPosition position;
    int height;
    void (*draw_func)(struct abuf *);
    bool enabled;
    int order;
} UIComponent;

// Function Prototypes
void uiInitialize();
void uiRegisterComponent(const char *name, UIPosition pos, int height, void (*draw_func)(struct abuf *), bool initially_enabled, int order);
void uiFreeComponents();
// void uiEnableComponent(const char *name, bool enable); // Optional

#endif // UI_H