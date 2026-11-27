# GUIA-RÁPIDO — como usar

## Compilar

Requisitos: MSVC x64 (VS Build Tools 18) e o SDK do Windows.

```bat
build.bat
```

Saída: `KTGameBooster.exe` na raiz do projeto (UI + engine em um EXE único).

## Primeira execução

1. Copie `gamemode.toml` para a mesma pasta do executável (opcional — sem o
   arquivo, usa-se a configuração embutida).
2. Rode **como Administrador** (a GUI tem manifesto `requireAdministrator`).
3. Abra o jogo. O KT Game Booster detecta sozinho e aplica as otimizações.
4. Ao fechar o jogo (ou o otimizador), tudo é restaurado.

## CLI

```bat
KTGameBooster.exe                     REM roda e mostra a tabela de processos
KTGameBooster.exe --daemon            REM sem a tabela periódica
KTGameBooster.exe --dry-run           REM mostra o que faria, sem aplicar
KTGameBooster.exe --list-threads      REM lista threads do jogo detectado
KTGameBooster.exe --list-games        REM lista os jogos conhecidos
KTGameBooster.exe --tournament        REM perfil torneio na sessao
KTGameBooster.exe --no-power          REM não toca em plano/boost/parking
KTGameBooster.exe --no-background     REM desliga toda limitação de fundo
KTGameBooster.exe --no-eco-qos        REM desliga EcoQoS
KTGameBooster.exe --no-io-priority    REM desliga mudanças de prioridade de I/O
KTGameBooster.exe --no-thread-power   REM desliga throttling de energia
```

`Ctrl+C` (ou `Ctrl+Break`) restaura tudo e sai limpo.

## GUI

- **Abas** (left rail): **Visão Geral** (resumo), **Processos** (tabela
  full-height), **Otimizações** (switches com mais espaço + seletor de perfil),
  **Jogos** (lista full-height), **Log** (full-height). Clique no rail para trocar.
- **Cards**: alvo, processos limitados, threads suspensas, eventos ETW, RAM.
- **Tabela**: processos ordenados por CPU; a linha do jogo fica destacada e
  mostra um **mini-gráfico (sparkline)** de CPU na coluna CPU%.
- **Otimizações**: switches aplicados em tempo real; seletor de **perfil**
  (setas `◄`/`►`) alterna `gamemode.toml` ↔ `profiles/*.toml`.
- **Testar jogo**: com o jogo aberto, use o botão no painel para forçá-lo como
  alvo no próximo ciclo de detecção.
- **Log**: rolável; o botão `Filtro: ...` alterna TODOS / GAME / CORE / POWER /
  CONFIG / GUI.
- **Footer**: relatório de economia — sessões, tempo em jogo, uptime e execuções.
- **Atalhos**: `Espaço` pausa/retoma; `Ctrl+Alt+G` liga/desliga o **Modo
  Tournament**; `T` (ou o botão lua/sol no header) alterna tema claro/escuro.
- **Tray**: ícone com menu (Abrir / Pausar / Restaurar / Limpar RAM / Sair) e
  balões quando um jogo é detectado ou encerrado.
- **Intervalo**: ajusta `poll_interval_ms` (250 ms – 10 s).

### Flags da GUI

```bat
KTGameBooster.exe --start-minimized   REM inicia oculta (só tray)
```

### DPI

A GUI é *Per-Monitor v2 aware* e escala a janela conforme o DPI do monitor
primário. Em monitores com escala diferente, basta abrir no monitor desejado.

## Jogos novos

1. Abra a GUI → "Adicionar jogo..." → escolha o `.exe` do jogo.
2. Ou edite `[[process]]` / a lista `kKnownGames` (ver [[CONFIGURACAO]]).

## Solução de problemas

| Sintoma | Causa provável |
|---|---|
| "ETW unavailable, falling back to polling" | Sem sessão de kernel logger neste Windows; o polling (20 ciclos) cobre. |
| Working-set lock falhou | Falta privilégio / trava de memória não suportada; ignora. |
| Processos de fundo não limitados | `background_limiter = false` (é o master switch). |
| C-States não aplicam | `PowerWriteACValueIndex` precisa de plano de energia editável. |
