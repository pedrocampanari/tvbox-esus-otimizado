#include "native_window.h"

#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

namespace kiosk {

unsigned long GetNativeX11WindowId(void *glfwWindowHandle) {
    if (!glfwWindowHandle) return 0;
    if (glfwGetPlatform() != GLFW_PLATFORM_X11) return 0;
    return static_cast<unsigned long>(glfwGetX11Window(static_cast<GLFWwindow *>(glfwWindowHandle)));
}

} // namespace kiosk
