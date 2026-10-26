# MiniHV - escopo 09

Aluno: David dos Santos Oliveira (matrícula 2611820)

Hypervisor simulado em C11. Cria VMs que rodam programas MV-ISA, escalona as vCPUs, gerencia a memória e atende E/S, tudo em ticks discretos.

## Parâmetros do escopo 09

| Item | Valor |
|---|---|
| Escalonador | CREDIT, q=5 (referência: FCFS) |
| pCPUs / máx. VMs | 4 / 6 |
| Memória | CONT, BEST_FIT, 4096 palavras |
| Disco | C_LOOK (200 cilindros, cabeça em 50, 25 cil/tick) |
| Log | JSONL (`out/minihv.jsonl`) |
| Recurso extra | GANTT |

Esses valores são os padrões do programa. Se uma chave não aparece no `.conf`, vale o valor acima.

## Como compilar

Precisa de `gcc` (ou `clang`) e `make`. Testei no Ubuntu 24.04 com gcc 13 e clang 18.

```bash
make
```

Compila com `-std=c11 -Wall -Wextra -Werror -pedantic` sem nenhum aviso. Só usa a biblioteca padrão.

## Como rodar

```bash
./minihv -c conf/hv.conf
```

Outras formas:

```bash
./minihv -c conf/hv.conf -i
```

```bash
./minihv -c conf/hv.conf -p FCFS
```

O `-i` abre o modo interativo (prompt `minihv>`, digite `help`). O `-p` troca a política só nessa execução, para comparar com a de referência.

Alvos do Makefile:

- `make run` roda `conf/hv.conf` (ou outro com `make run CONF=conf/ta04.conf`)
- `make test` compila e roda os testes de `tests/`
- `make valgrind` roda todos os `conf/*.conf` e todos os testes no valgrind
- `make clean` apaga `build/`, `out/` e o binário

O log e o `gantt.csv` vão para `out/`. O `make` cria essa pasta; se rodar o binário na mão, ela tem que existir.

## Organização

```
src/
  main.c
  hv/      fachada, laço de ticks, Observer (events.h), comandos e relatório (hv_cmd.c)
  cli/     modo interativo
  vm/      VCB e máquina de estados
  cpu/     interpretador, troca de contexto, loader MV-ISA
  sched/   interface Strategy + FCFS + CREDIT
  mem/     partições contíguas + BEST_FIT
  io/      CONSOLE, DISK, NET e o C_LOOK
  ext/     GANTT
  config/  leitura e validação do hv.conf
  log/     log (TXT/CSV/JSONL) e montagem do relatório final
  util/    fila, lista, LCG e códigos de status
```

Os módulos de baixo não incluem `hv.h`. Eles recebem um `EventSink` (ponteiro de função + contexto) e emitem eventos por ele; o `hv` repassa para o log e para o GANTT.

## Cenários de teste

| Arquivo | O que mostra |
|---|---|
| `conf/hv.conf` | configuração principal, 7 VMs misturando tudo |
| `conf/ta01.conf` | contador.mv imprime 5..1 e termina com HALT |
| `conf/ta02.conf` | 4 VMs CPU-bound com chegadas diferentes (host do escopo) |
| `conf/ta02_1cpu.conf` | as mesmas 4 VMs numa pCPU só, para conferir o CREDIT à mão |
| `conf/ta03.conf` | CPU, CONSOLE, DISK e NET juntos |
| `conf/ta04.conf` | memória pedida maior que a física: espera em NEW e compactação |
| `conf/ta05.conf` | VM com PROTECTION_FAULT, as outras terminam normal |
| `conf/ta06_programa.conf` e `conf/erros/` | programa com erro de sintaxe e configuração inválida |
| `conf/ta08.conf` | cenário do GANTT |
| `conf/perf.conf` | 8 VMs por 10.000 ticks (RNF-05) |

O TA-07 (determinismo) está em `tests/test_hv.c`: roda `conf/hv.conf` duas vezes e compara a saída e o log byte a byte. O TA-08 está em `tests/test_ext.c`: reconstrói a ocupação das pCPUs só com os eventos do log e compara com o `gantt.csv`, tick a tick.

Programas em `progs/`: `cpu_bound.mv` (CPU), `io_pesado.mv` (muita E/S), `memoria.mv` (percorre a memória), `protecao.mv` (erro de proteção), `gantt.mv` (rajadas para o Gantt), e mais `contador.mv`, `cpu_curto.mv`, `disco.mv`, `loop.mv` e `erro_sintaxe.mv`.

## Conta à mão do TA-02 com 1 pCPU

VMs a, b, c, d com 23 ticks de CPU cada, chegando em 0, 2, 4 e 6, prioridades 0, 3, 6 e 9 (pesos 10, 7, 4 e 1). Quantum 5, saldo inicial 5. Na recarga entram 30 créditos (30 × 1 pCPU): a ganha 13, b 9, c 5 e d 1.

| tick | despacha | por quê |
|---|---|---|
| 0 | a | única pronta |
| 5 | b | a gastou o quantum, b é a primeira UNDER |
| 10 | c | idem |
| 15 | d | idem |
| 20 | a | todas com saldo 0 (OVER), pega a primeira da fila |
| 25 | b | idem |
| 30 | c | recarga: a=8, b=4, c=5, d=1; c é a primeira UNDER da fila |
| 35 | d | d=1 |
| 40 | a | a=8 |
| 45 | b | b=4 |
| 50 | a | c=0 e d=-4 são OVER, a=3 ainda é UNDER |
| 55 | c | ninguém UNDER, c é a primeira |
| 60 | b | recarga: a=11, b=8, c=0, d=-3 |
| 65 | a | roda 3 ticks e termina em 67 |
| 68 | b | termina em 70 |
| 71 | d | ninguém UNDER |
| 76 | c | |
| 81 | d | |
| 86 | c | termina em 88 |
| 89 | d | termina em 91 |

O `tests/test_sched.c` confere essa sequência exata a partir dos eventos DISPATCH.

## Decisões onde o enunciado deixava dúvida

- A recarga do CREDIT acontece no começo do passo 3 dos ticks 30, 60, 90... e vale para as VMs admitidas que ainda não terminaram (READY, RUNNING, BLOCKED e PAUSED). VM em NEW ainda não tem conta.
- O saldo pode ficar negativo à vontade; só o máximo (60) é limitado.
- Uma VM UNDER que fica pronta não tira uma OVER da CPU. Preempção só no fim do quantum.
- Ao bloquear, a VM perde o resto do quantum e volta para o fim da fila quando acorda.
- Conta como troca de contexto quando entra numa pCPU uma VM diferente da última que esteve nela. Se a mesma VM é preemptada e despachada de novo na mesma pCPU, não conta.
- Na compactação as VMs não mudam de estado, só ficam sem executar pelos ticks da compactação (1 por partição que mudou de lugar). Os dispositivos continuam andando. Partição que já está no lugar não conta.
- A admissão segue a ordem de chegada. Se a primeira VM em NEW não cabe na memória, as que chegaram depois esperam atrás dela.
- Uma requisição que começa no tick s com latência L acorda a VM no passo 2 do tick s+L. Um OUT executado no tick t imprime no tick t+3.
- `fim` no relatório é o tick em que a VM executou o HALT (ou foi encerrada). As médias de turnaround e espera usam só as VMs que terminaram; a de resposta, só as que chegaram a ser despachadas.
- No modelo CONT não existe page fault, então a coluna `faults` aparece como `-`.
- O `gantt.csv` usa o nome da VM e `-` para pCPU ociosa. No diagrama em texto as VMs aparecem pelo id (0-9, depois A-Z) e tem uma legenda no final.

## Limitações conhecidas

- Só estão implementadas as políticas CREDIT e FCFS, o modelo CONT com BEST_FIT e os discos C_LOOK e FCFS. Outro valor nessas chaves dá erro de configuração, com arquivo e linha.
- O binário não cria a pasta `out/`; quem cria é o `make`.
- Nome de VM tem até 15 caracteres e o arquivo aceita até 64 VMs.
- No Gantt, ids acima de 35 aparecem como `#`.
- Rodando dentro do WSL numa pasta do Windows (`/mnt/c/...`) a gravação do log fica bem mais lenta (uns 2 s no `perf.conf`). Numa pasta do Linux o mesmo cenário leva uns 0,05 s.
