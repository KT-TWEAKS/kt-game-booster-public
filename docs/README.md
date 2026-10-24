# KT Game Booster — Documentação

> Otimizador de jogos para Windows em modo usuário — engine C++ nativa,
> interface WPF, sem driver e sem dependências externas.

Esta pasta é pensada para leitura no **Obsidian** (links relativos funcionam
nativamente). Use o painel de arquivos para navegar ou abra os documentos
abaixo.

## Índice

| Documento | Conteúdo |
|---|---|
| [[GUIA-RAPIDO]] | Primeiros passos: compilar, rodar, CLI, GUI |
| [[ARQUITETURA]] | Módulos, pipeline do motor, ciclo de otimização |
| [[CONFIGURACAO]] | Referência completa do `gamemode.toml` + registro |
| [[API]] | API exportada (`KT_*`) |
| [[CHANGELOG]] | Mudanças da versão |
| [[TESTES]] | Como compilar e validar os dois alvos |
| [[SEGURANCA]] | Permissões, anticheat e restauração de estado |
| [[ROADMAP]] | O que vem depois da 1.0 |

## Visão geral rápida

- **`KTGameBooster.exe`** — release único: UI WPF e engine C++ (`ktengine.dll`)
  no mesmo executável, com `gamemode.toml`, perfis e ícones embutidos. Na
  primeira execução ele materializa esses arquivos ao lado do EXE.
- **Núcleo**: detecta o jogo → aplica o perfil (prioridade, timer, CPU Sets,
  EcoQoS de fundo) → acompanha por tick de 1 s → restaura tudo ao sair, com
  journal de snapshots cobrindo cada mudança.
- **Config**: `gamemode.toml` (ou defaults embutidos) + chaves da GUI em
  `HKCU\Software\KTGameBooster`.

## O que a 1.0 entrega

- Detecção com 260+ executáveis embutidos e confiança por caminho de loja.
- 61 títulos com preset automático (101 regras de EXE).
- Timer 0,5 ms, CPU Sets em P-cores, EcoQoS de fundo e planos de energia gaming.
- Bench de latência v2 com score composto e piores casos visíveis.
- Painel com abas Visão Geral / Processos / Otimizações / Jogos / Log.
- CLI completa no mesmo EXE e updater com SHA-256 (canal público anônimo).
