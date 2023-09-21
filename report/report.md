# PAGINADOR DE MEMÓRIA - RELATÓRIO

## 1. Termo de compromisso

Os membros do grupo afirmam que todo o código desenvolvido para este
trabalho é de autoria própria.  Exceto pelo material listado no item
3 deste relatório, os membros do grupo afirmam não ter copiado
material da Internet nem ter obtido código de terceiros.

## 2. Membros do grupo e alocação de esforço

Preencha as linhas abaixo com o nome e o e-mail dos integrantes do
grupo.  Substitua marcadores `XX` pela contribuição de cada membro
do grupo no desenvolvimento do trabalho (os valores devem somar
100%).

  * João Marcos <jmlg@ufmg.br> 34%
  * Pierre Sousa <pierrevictor@ufmg.br> 33%
  * Mariana Leite <marianaleite@ufmg.br> 33%

## 3. Referências bibliográficas

- https://man7.org/linux/man-pages/man3/getcontext.3.html
- https://man7.org/linux/man-pages/man3/makecontext.3.html
- https://man7.org/linux/man-pages/man2/timer_create.2.html
- https://man7.org/linux/man-pages/man7/signal.7.html
- https://man7.org/linux/man-pages/man2/sigaction.2.html
- https://www.ibm.com/docs/en/i/7.3?topic=ssw_ibm_i_73/apis/sigpmsk.html


## 4. Estruturas de dados

### 1. Descreva e justifique as estruturas de dados utilizadas para gerência das threads de espaço do usuário (partes 1, 2 e 5).

Foram utilizadas duas filas encadeadas, cujo item é um `dccthread_t`, uma para a "fila de prontos" e outra para a "fila de espera". A primeira enfileira as threads que estão prontas para serem executadas, já a segunda armazena as thread que estão esperando, seja outra thread terminar ou um tempo passar (sleep).

A ideia por trás de utilizar essas duas estruturas está pela simplicidade e alinhamento com o que foi visto dentro da sala de aula.

### 2. Descreva o mecanismo utilizado para sincronizar chamadas de dccthread_yield e disparos do temporizador (parte 4).

A fim de sincronizar as chamadas de `dccthread_yield` (ou demais funções) e eventuais disparos do temporizador, o sinal enviado pelo temporizador foi mascarado ao executar qualquer uma das funções (incluindo a `dccthread_yield`), de modo a evitar um corrupção da estrutura de gerência das threads.

        