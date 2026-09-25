# harness/ — ferramentas de teste

Infraestrutura do estágio S03. **Nada aqui é linkado ao binário `sleuth-par`.**

A restrição §C do spec mestre tira GIF e todo o subsistema `gdif_obj`/`GD` do
protótipo. Isso continua valendo: o núcleo lê texto. O que existe aqui são duas
coisas que ficam *ao lado* do protótipo — um conversor de dados que roda uma vez,
e a construção do binário de referência contra o qual a invariante V7 é medida.

A árvore `SLEUTH/` é somente-leitura (§R2). Tudo o que precisa ser compilado ou
corrigido acontece sobre uma cópia em `build/`, que é descartável.

## Alvos

```
make gif2asc     conversor GIF -> texto (linka só o gd.c do SLEUTH)
make data        gera ../data/demo200/*.txt
make baseline    constrói o `grow` de referência numa cópia de SLEUTH/
make scenario    gera o cenário de referência de V7
make verify      confere que o gif2asc reproduz os números de §D1 (VS8)
make smoke       roda o baseline 1990->1995 e mostra o avg.log
make clean       remove build/
make distclean   remove build/ e os dados gerados
```

Para levantar tudo do zero:

```bash
make -C harness gif2asc data baseline smoke
```

## Os arquivos

| arquivo | papel |
|---|---|
| `gif2asc.c` | réplica literal do laço de leitura de `gdif_obj.c:359-380`. Só lê; não transforma. |
| `mkdata.sh` | aplica as transformações que o SLEUTH faz *depois* de ler, e gera `../data/demo200/`. |
| `mkscenario.sh` | deriva o cenário de referência de `scenario.demo200_predict`, com cada ajuste comentado. |
| `grow-ref.sh` | executa o `grow` de referência com as cinco variáveis de ambiente de 2001 que ele exige. |
| `0001-date_str-overflow.patch` | o patch de um byte sem o qual o original aborta em toolchain moderno. |

## Por que o baseline tem de ser construído

O `grow` que vem na árvore **não serve**: é um ELF MIPS N32 de 2001 (e o
`grow.exe` é de 2005). E, construído cru com gcc atual, ele aborta — há um
estouro de pilha de um byte em `util_WriteZProbGrid` que o `_FORTIFY_SOURCE`
moderno detecta. O patch corrige.

Outros dois defeitos são contornados pelo arquivo de cenário, não por patch:
`fclose(NULL)` em `mem_Init` e um estouro de buffer em `landclass_LogIt`. Os
três estão documentados em §B1–§B3 de `../../specs/S03-harness.md`.

## O que é versionado e o que não é

`build/` é ignorado — é reconstruível com `make baseline`. Os dados em
`../data/demo200/*.txt` **são** versionados: assim rodar os testes não exige
o GD nem o conversor, e a entrada da comparação de V7 fica fixada como texto
diffável.
