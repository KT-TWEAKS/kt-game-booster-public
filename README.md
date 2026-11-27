<div align="center">

<img src="assets/banner.png" alt="Banner oficial da KT TWEAKS" width="100%">

<h1>KT Game Booster</h1>

<p><strong>Perfis temporários de otimização para sessões de jogo no Windows.</strong></p>

<p><a href="https://github.com/KT-TWEAKS/kt-game-booster-public/releases/latest">Download</a> · <a href="docs/GUIA-RAPIDO.md">Guia rápido</a> · <a href="docs/CONFIGURACAO.md">Configuração</a> · <a href="https://github.com/KT-TWEAKS/kt-game-booster-public/issues">Reportar problema</a></p>

</div>

## Visão geral

O KT Game Booster detecta o jogo em execução, aplica um perfil e registra as mudanças em um journal para restaurá-las ao fim da sessão. Funciona em modo usuário, sem driver. Os perfis podem ajustar prioridade, timer, CPU Sets, EcoQoS de processos em segundo plano e plano de energia.

## Começar

1. Baixe a [release mais recente](https://github.com/KT-TWEAKS/kt-game-booster-public/releases/latest) e confira o arquivo `.sha256` publicado com o instalador.
2. Execute em Windows 10 (build 19045+) ou Windows 11 x64 com privilégios de administrador.
3. Abra um jogo compatível e acompanhe o estado no painel. Ao encerrar a sessão, o aplicativo restaura as mudanças registradas.

Para testar sem aplicar alterações, use `KTGameBooster.exe --dry-run`. Leia o [guia rápido](docs/GUIA-RAPIDO.md) antes do primeiro uso.

## Documentação

| Guia | Conteúdo |
| :-- | :-- |
| [Configuração](docs/CONFIGURACAO.md) | `gamemode.toml` e perfis |
| [Arquitetura](docs/ARQUITETURA.md) | Motor, interface e ciclo de execução |
| [Segurança](docs/SEGURANCA.md) | Permissões, anticheat e restauração |
| [Testes](docs/TESTES.md) | Compilação e validação |
| [Changelog](docs/CHANGELOG.md) | Histórico de versões |

## Desenvolver

O build requer VS Build Tools com MSVC x64, Windows SDK e .NET Framework 4.8.

```bat
build.bat
```

Consulte a [documentação técnica](docs/README.md) para os demais comandos e módulos.

## Licença

[MIT](LICENSE) · Um projeto da [KT TWEAKS](https://github.com/KT-TWEAKS).
