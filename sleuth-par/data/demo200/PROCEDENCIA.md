# demo200 — grades em texto

Geradas por `harness/mkdata.sh` a partir de `SLEUTH/Input/demo200/`.
**Não editar à mão**: rode `make -C harness data`.

Formato: primeira linha `nrows ncols`, depois `nrows` linhas de
`ncols` inteiros separados por espaço. É o mesmo formato que o
`--dump` do `sleuth-par` produz.

| arquivo | origem | transformação |
|---|---|---|
| `z.txt` | `demo200.urban.1990.gif` | `PHASE0G` (=3) onde > 0 — `util_condition_gif(GT, 0)` |
| `roads.txt` | `demo200.roads.1990.gif` | `igrid_NormalizeRoads`: `(long)(100.0*v/max)`, `norm_factor = 1.0` |
| `excld.txt` | `demo200.excluded.gif` | crua |
| `slp.txt` | `demo200.slope.gif` | crua |

Uma grade de cada porque é o que o original carrega em modo `predict`:
apenas as urbanas com ano >= `PREDICTION_START_DATE` (`igrid_obj.c:1237`),
e exatamente uma de estrada por conta do defeito de §B4.

## Conferência

    1089caf6bcc5f0d604385b101c03ff9694fe61c776a6aad8d882d271d85c12f5  z.txt
    b6653dc37b9933b2f3b41c592a18fcc45cd443507b034b65fc639aff02fb101e  roads.txt
    4d93fc0086a77eeb1a5740780be5f9d622ede77158252f1c789cf4791e98854a  excld.txt
    57911b30f873836a96da6c8e7dc6ab1b6119fd65dfcbcc3fd2347c148a31943e  slp.txt

A procedência é identificada por soma de verificação, não por data de
geração: assim rodar `make -C harness data` sobre as mesmas entradas não
suja a árvore de trabalho, e um diff neste arquivo significa que os **dados**
mudaram.
