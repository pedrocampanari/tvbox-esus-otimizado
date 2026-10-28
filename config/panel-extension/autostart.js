// O PEC mostra "Iniciar painel de chamadas" toda vez que o painel abre:
// o clique só toca o <audio> mudo para liberar o som das chamadas e
// troca para o painel. Com --autoplay-policy=no-user-gesture-required
// (exec.sh) um clique de script libera o som igual ao clique real.
// Checagem a cada 1 s em vez de MutationObserver: o painel ao vivo muda
// o DOM o tempo todo, e varrer os botões 1x/s é barato no RK3229.
const LABEL = 'Iniciar painel de chamadas';

setInterval(() => {
  for (const button of document.querySelectorAll('button')) {
    if (button.textContent.trim() === LABEL) {
      button.click();
      return;
    }
  }
}, 1000);
