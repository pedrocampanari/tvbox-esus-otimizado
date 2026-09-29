#ifndef TVBOX_REMOTE_CONTROL_H
#define TVBOX_REMOTE_CONTROL_H

// Le VOL+/VOL-/MUTE do controle remoto IR do hardware (receptor
// gpio_ir_recv, keymap carregado via udev — ver
// docs/memory/known-issues.md item -6) direto de /dev/input, pra poder
// desenhar um indicador de volume no mesmo estilo visual do resto do
// kiosk. O botao POWER continua fora daqui: um desligamento real nao
// precisa de feedback na tela, e ja funciona via triggerhappy (processo
// de sistema, independente deste binario).
//
// Nao inclui raylib.h nem <X11/Xlib.h> de proposito, mesma regra de
// isolamento de headers do include/player.h (ver
// docs/memory/architecture.md, secao "Restricao de headers") — aqui nao
// ha colisao de tipos entre eles, mas manter o padrao evita qualquer
// acoplamento desnecessario com um modulo que so faz leitura de evdev.
namespace kiosk {

enum class RemoteButton {
    kNone,
    kVolumeUp,
    kVolumeDown,
    kMute,
};

class RemoteControl {
public:
    RemoteControl();
    ~RemoteControl();

    RemoteControl(const RemoteControl &) = delete;
    RemoteControl &operator=(const RemoteControl &) = delete;

    // Chamar uma vez por frame; nao bloqueia. Devolve o botao PRESSIONADO
    // neste ciclo (kNone se nao chegou nenhum evento novo). So dispara no
    // toque inicial — repeticao automatica do controle sendo segurado e
    // solturas sao ignoradas, mesmo criterio do trigger do triggerhappy
    // usado pro POWER.
    RemoteButton PollButtonPress();

private:
    int fd_ = -1;
};

// Consulta volume (0-100) e estado de mudo do primeiro mixer ALSA
// disponivel (via `amixer`, sem linkar libasound — mesmo principio de
// "processo pontual em vez de lib" usado pra curl/mpv/yt-dlp, ver
// docs/memory/architecture.md). Retorna false se nao houver nenhum
// mixer controlavel por software (comum em saida HDMI pura, onde a
// propria TV controla o volume).
bool QueryVolumeState(int &outPercent, bool &outMuted);

// Ajusta o volume do primeiro mixer disponivel. `deltaPercent` positivo
// ou negativo (ex.: +5/-5); ignorado se `toggleMute` for true. Devolve o
// novo estado via outPercent/outMuted. Mesmas condicoes de falha de
// QueryVolumeState.
bool AdjustVolume(int deltaPercent, bool toggleMute, int &outPercent, bool &outMuted);

} // namespace kiosk

#endif // TVBOX_REMOTE_CONTROL_H
