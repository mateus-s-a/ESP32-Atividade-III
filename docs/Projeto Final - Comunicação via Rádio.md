# Projeto Final da Disciplina Transmissão e Comunicação de Dados:
## _Transmissão de dados confiável via rádio_

### Introdução

O projeto refere-se a um sistema de comunicação em que os dados são transmitidos de um
microcontrolador a outro via uma interface sem fio. Neste projeto, os dois lados da
comunicação devem implementar os protocolos de comunicação necessários para que a troca
de informações ocorra eficientemente. A comunicação entre o emissor e o receptor deve
ocorrer exclusivamente por meio de módulos de radiofrequência. O objetivo do projeto é
possibilitar aos estudantes envolvidos praticar os conceitos estudados em sala de aula de
modo a fixar os conhecimentos envolvidos.

### Proposta
Desenvolver um sistema de comunicação sem fio entre duas placas ESP32 utilizando módulos
RF de 433 MHz. O sistema deverá permitir a transmissão confiável de mensagens de texto e
pequenas imagens.

### Materiais Utilizados
- 2 placas ESP32;
- 2 módulos RF 433 MHz;
- 1 display LCD conectado ao receptor;
- Cabos jumpers e protoboard.

### Comunicação Básica
Implemente uma comunicação simples entre os dois ESP32 utilizando transmissão RF bruta,
conforme ilustrado na Figura 1. A comunicação deve ocorrer exclusivamente pelo canal sem fio
RF de 433 MHz. O display LCD no receptor deverá mostrar o texto recebido do transmissor. A
estrutura de quadro a ser transmitida é mostrada na Figura 2.

Somente três tipos de quadros serão aceitos, conforme a seguir:
- DATA - Transmissão de dados;
- ACK - Confirmação de recebimento;
- END - Fim da transmissão.


### Detecção e Recuperação de Erro
Implemente um FCS (Frame Check Sequence) utilizando inicialmente o método do checksum.
O receptor deverá: i) verificar o FCS recebido; ii) descartar quadros corrompidos.

Para a recuperação do erro, implemente inicialmente o protocolo Stop-and-Wait ARQ, da
seguinte forma:
- O transmissor envia um quadro;
- Aguarda um ACK;
- Caso o tempo expire, o quadro deve ser retransmitido;
- Siga o procedimento mostrado na Figura 3.

### Simulação de Erros
Introduza erros propositalmente durante a transmissão e demonstre:
- Detecção de quadros corrompidos;
- Retransmissão automática;
- Recuperação correta dos dados.

### Módulos de Comunicação a Serem Usados
A Figura 4 apresenta possíveis módulos RF 433 MHz que podem ser utilizados.

### Itens a Serem Entregues
Os resultados do projetos deverão ser apresentados ao docente, e o seguintes itens deverão
ser entregues:
- Código-fonte do transmissor e receptor, inclusive disponibilizado no Github;
- Documento completo, descrevendo o sistema implementado;
- Descrição do cálculo do FCS;
- Logs do monitor serial demonstrando as transmissões;
- Demonstração do texto exibido no LCD;
- Demonstração da reconstrução da imagem.

### Critérios de Aceitação
- Distância: consegue se comunicar a mais de 1 metro de distância;
- Ruído: consegue transmitir a mensagem mesmo sob a presença de ruído;
- Comunicação entre dispositivos feita apenas via módulos RF (conforme Figura 1);
- Implementação dos algoritmos citados acima;
- O dado transmitido deve chegar em perfeito estado.


### Avaliação
- Design: (6 pts)
  - Algoritmo de detecção de erros: Checksum (2 pts) ou CRC (3 pts);
  - Algoritmo de controle de fluxo: Stop-and-Wait ARQ (1 pt) ou Go-Back-N ARQ (2 pts) ou Selective Repeat ARQ (3 pts).
- Relatório: (4 pts)
  - Reprodutibilidade: permite construir um sistema semelhante apenas com base no conteúdo do relatório entregue: (2 pts)
  - Clareza: a linguagem de escrita é clara, objetiva e descreve bem o projeto: (2
pts)

### Importante:
- O projeto deverá ser demonstrado presencialmente em sala de aula. A pontuação acima
dependerá da qualidade da demonstração;
- A identificação de plágio, cópia parcial ou total, ou uso indevido de ferramentas de IA
generativa, como ChatGPT e similares, implicará na anulação integral do projeto;
- Os critérios de avaliação considerarão, principalmente, o nível de detalhamento, a
qualidade técnica da documentação apresentada, a organização do projeto e a
consistência dos circuitos desenvolvidos.

Data de entrega: 16/06/2026.
