# ROADMAP

> Legenda: `[x]` feito · `[ ]` pendente.

## 1.0 (feito)

- [x] **Fastpath ETW real** — sessão com GUID próprio, eventos Process/Thread,
      PID/TID extraídos do payload e filtro pelo PID do jogo. O polling de 20
      ciclos permanece como fallback.
- [x] **Detecção por caminho da imagem** — UWP/MS Store/Game Pass/launchers
      (Steam, Epic, Riot, Battle.net, Ubisoft, EA, GOG, WindowsApps) quando o
      exe é genérico.
- [x] **Presets por título** — 61 títulos (101 regras de EXE) com perfil
      automático antes da trava de anticheat.
- [x] **Bench de latência v2** — score composto, timer real via ntdll,
      P95 de scheduler e leitura 4KB sem cache.
- [x] **Updater com SHA-256** — canal público, download anônimo via REST.
- [x] **Painel em modo jogo** — 20 fps de teto, sem sombras, bandeja opcional.
- [x] **CLI completa** — `--daemon`, `--dry-run`, `--list-games`,
      `--tournament` e flags por lever.

## Próximo

- [ ] **Modo rede / sync de perfil** para times competitivos (hash de
      integridade).
- [ ] **Instalador MSI** — auto-start como serviço, ícones, assinatura EV.
- [ ] **Build Arm64** para Windows on ARM (nativo).

## Longo prazo

- [ ] **GUI em Direct2D/Composition** — transições, blur, DPI dinâmico entre
      monitores.
- [ ] **Profile sharing via nuvem** — sincronizar perfis entre máquinas via
      Microsoft Account (com hash de integridade).
- [ ] **Parse de manifests** Steam/Epic/EA App — bibliotecas completas como
      fonte de detecção.
