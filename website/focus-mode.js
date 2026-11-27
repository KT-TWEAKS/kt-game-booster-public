export function resolveFocusMode(value) {
  return value === 'true';
}

export function nextFocusMode(enabled) {
  return !enabled;
}

function syncButton(button, enabled) {
  button.setAttribute('aria-pressed', String(enabled));
  button.innerHTML = enabled
    ? '<i class="fas fa-eye" aria-hidden="true"></i> Modo foco ativo'
    : '<i class="fas fa-wand-magic-sparkles" aria-hidden="true"></i> Reduzir efeitos';
}

function setFocusMode(button, enabled) {
  document.body.classList.toggle('focus-mode', enabled);
  window.localStorage.setItem('kt-game-booster-focus-mode', String(enabled));
  syncButton(button, enabled);
}

if (typeof document !== 'undefined') {
  const button = document.querySelector('[data-focus-mode]');
  if (button) {
    const initial = resolveFocusMode(window.localStorage.getItem('kt-game-booster-focus-mode'));
    setFocusMode(button, initial);
    button.addEventListener('click', () => {
      setFocusMode(button, nextFocusMode(document.body.classList.contains('focus-mode')));
    });
  }
}
