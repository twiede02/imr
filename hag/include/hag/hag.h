#ifndef HAG_H
#define HAG_H

#include "vulkan/vulkan.h"
#include "GLFW/glfw3.h"

namespace hag {
    struct Window;

    Window* create_window();
    void destroy_window(Window*);
}

#endif
