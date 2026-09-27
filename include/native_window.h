#ifndef TVBOX_NATIVE_WINDOW_H
#define TVBOX_NATIVE_WINDOW_H

// Isolado num header/módulo próprio pelo mesmo motivo de
// include/video_config.h: `glfw3native.h` (com GLFW_EXPOSE_NATIVE_X11)
// inclui <X11/Xlib.h> internamente, que colide com o `Font` do
// raylib.h na mesma translation unit. Quem chama isso
// (apps/tvbox_esus_app.cpp) inclui raylib.h; a implementação
// (src/native_window.cpp) não pode.
namespace kiosk {

// Extrai o ID de janela X11 nativa a partir do handle que
// `GetWindowHandle()` do raylib retorna no backend desktop (que é, na
// prática, um `GLFWwindow*`). Retorna 0 se não for possível (ex.: o
// GLFW estiver rodando com backend Wayland nativo em vez de X11/
// XWayland — nesse caso não existe uma janela X11 pra extrair).
unsigned long GetNativeX11WindowId(void *glfwWindowHandle);

} // namespace kiosk

#endif // TVBOX_NATIVE_WINDOW_H
