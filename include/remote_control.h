#ifndef TVBOX_REMOTE_CONTROL_H
#define TVBOX_REMOTE_CONTROL_H

// Le o controle remoto IR do hardware (receptor gpio_ir_recv, keymap
// carregado via udev a partir de config/rc-rk322x-tvbox.toml — ver
// docs/memory/known-issues.md item -6) direto de /dev/input, pra poder
// desenhar o feedback (volume, "segure para...") no mesmo estilo visual
// do resto do kiosk.
//
// POWER e lido aqui SO pra mostrar a contagem regressiva na tela: quem
// reinicia de verdade e o triggerhappy (processo de sistema,
// independente deste binario — tem que funcionar mesmo com o app
// travado). Ver install.sh, "POWER segurado".
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
    kPower,
    kHome,
    kKey1,
    kKey2,
    kKey3,
    // Navegacao do menu "Configuracao da BTV" (include/setup_menu.h).
    kUp,
    kDown,
    kLeft,
    kRight,
    kOk,
    kBack,
};

// Resultado de um Poll(), seguindo a secao 7 do manual da BTV11
// (docs/files/). VOL+/VOL- viram `tap` no toque e na repeticao (botao
// segurado = volume andando); MUTE/HOME/1/2 viram `tap` so na descida
// (segurar nao repete a acao). Setas repetem como o volume; OK/VOLTAR
// so na descida. 3 e POWER sao "botoes de segurar": a acao
// depende de quanto tempo ficaram pressionados, entao aparecem em
// `holding` enquanto estao apertados e em `released` na soltura.
struct RemoteInput {
    RemoteButton tap = RemoteButton::kNone;
    RemoteButton holding = RemoteButton::kNone;
    double heldSeconds = 0.0;
    RemoteButton released = RemoteButton::kNone;
    double releasedAfterSeconds = 0.0;
};

class RemoteControl {
public:
    RemoteControl();
    ~RemoteControl();

    RemoteControl(const RemoteControl &) = delete;
    RemoteControl &operator=(const RemoteControl &) = delete;

    // true se o receptor foi encontrado e aberto com sucesso. Usado so
    // pra logar um aviso quando nao encontrado (ver
    // apps/tvbox_esus_app.cpp) — PollButtonPress() ja falha em
    // silencio/seguro (devolve kNone) mesmo sem chamar isto.
    bool IsAvailable() const { return fd_ >= 0; }

    // Chamar uma vez por frame; nao bloqueia. `nowSeconds` e o relogio
    // monotonico do chamador (GetTime() do Raylib), usado pra medir
    // quanto tempo um botao de segurar ficou pressionado — a precisao e
    // a do frame (100ms a 10fps), mais que suficiente pra limiares de
    // segundos. Se vieram varios toques num unico frame, o ultimo
    // "vence" (efeito esperado pelo usuario).
    RemoteInput Poll(double nowSeconds);

private:
    int fd_ = -1;
    RemoteButton holdButton_ = RemoteButton::kNone;
    double holdStart_ = 0.0;
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
