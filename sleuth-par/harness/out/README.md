# `harness/out/` — saídas de sonda que não se reconstroem

O `.gitignore` do projeto exclui o que se refaz em segundos (`graphify-out/`,
`data/demo2000/`, `build/`). **Isto é o contrário disso.** Uma saída de sonda é
a observação de uma máquina num estado que não volta: a sessão 12 mediu com dez
containers de pé, depois com eles recém-nascidos, depois com eles mortos, e os
três estados só coexistem aqui.

A regra do §9 do HANDOFF é "não se versiona o que se reconstrói". A sonda é
versionada porque é o instrumento; **a saída é versionada porque é o dado**, e
rodar a sonda de novo amanhã produz outro dado, não o mesmo.

## O que tem aqui

| arquivo | estágio | ambiente medido |
|---|---|---|
| `S08-T0-com-carga.txt` | T0 | 10 containers no ar + Kaspersky, SQL, Discord, Chrome, Riot |
| `S08-T4b-containers-frescos.txt` | T4b, 1ª tentativa | Windows limpo, mas os containers **voltaram** pelo systemd após um restart do WSL — ver armadilha 8 |
| `S08-T4b-sem-carga.txt` | T4b, 2ª tentativa | zero containers, Windows limpo. O braço válido |
| `S08-threads-6-8-12.txt` | diagnóstico de §D6 | o mesmo ambiente limpo, escada 6/8/12 |

Cada arquivo traz no cabeçalho o SHA do `HEAD`, a confirmação de que a sonda
estava **não modificada**, e o censo do ambiente **antes e depois** da medida.
O censo depois existe porque a 1ª tentativa da T4b provou que ele é necessário:
sem ele, uma medida contaminada é indistinguível de uma limpa.

## Como reler

Nenhum número daqui vale isolado. A leitura é sempre **entre arquivos**, com o
censo ao lado — e a comparação legítima é entre os dois que compartilham o
mesmo boot do WSL (`containers-frescos` e `sem-carga`), porque o par com a T0
atravessa um restart.
