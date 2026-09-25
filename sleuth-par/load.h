/*
 * load.h — leitura das grades de entrada a partir de texto.
 *
 * Existe para que o prototipo possa ser alimentado com os dados reais do
 * demo200 sem que leitura de GIF entre no nucleo (SPEC.md §C). A conversao
 * GIF -> texto e feita uma vez, fora do binario, por harness/gif2asc +
 * harness/mkdata.sh; o resultado esta versionado em data/demo200/.
 *
 * Formato dos arquivos (o mesmo que --dump produz):
 *
 *     nrows ncols
 *     v v v ... v      <- ncols inteiros
 *     ...              <- nrows linhas
 *
 * Os quatro arquivos sao lidos de um diretorio so:
 *
 *     z.txt      estado urbano inicial, ja com PHASE0G nas celulas da semente
 *     roads.txt  malha viaria ja normalizada (igrid_NormalizeRoads)
 *     excld.txt  exclusao, crua
 *     slp.txt    declividade, crua
 *
 * Quem aplica as transformacoes e o mkdata.sh, nao este carregador: ele le o
 * que esta no arquivo e ponto. Ver specs/S03-harness.md §D3 para o
 * levantamento de quais transformacoes o original faz e onde.
 */
#ifndef LOAD_H
#define LOAD_H

/*
 * Le as quatro grades de DIR, tira as dimensoes do cabecalho e chama
 * grid_alloc(). Aborta com mensagem se algum arquivo faltar, se os
 * cabecalhos discordarem entre si ou se a contagem de valores nao fechar.
 *
 * Faz a unica alocacao do programa (dentro de grid_alloc) e nada mais —
 * invariante V9.
 */
void load_input_dir (const char *dir);

#endif /* LOAD_H */
