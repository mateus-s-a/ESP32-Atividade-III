# Projeto Final: Transmissão de Dados Confiável via Rádio (433 MHz)

Este repositório contém a implementação do projeto final para a disciplina de **Transmissão e Comunicação de Dados** do Instituto Federal de Educação, Ciência e Tecnologia de Mato Grosso (IFMT).

O objetivo do projeto é estabelecer uma comunicação sem fio confiável e resiliente entre duas placas ESP32 utilizando módulos de radiofrequência (RF) de 433 MHz em nível de enlace de dados, aplicando conceitos teóricos de **estruturação de quadros**, **detecção de erros (FCS)** e **controle de fluxo/erro (Stop-and-Wait ARQ)**.

- **Mateus de Souza Arruda**
- **Matheus Henrique Moreira Louro**

---

## Menu de Documentação Detalhada

Para facilitar a leitura e organização do projeto, a documentação detalhada foi dividida em seções modulares localizadas na pasta [docs/](docs/):

1.  **[Arquitetura de Hardware e Conexões](docs/hardware.md)**
    *   Pinagem detalhada para interligação do ESP32 aos módulos RF 433 MHz e ao LCD I2C.
    *   Recomendações técnicas para melhoria de sinal e confecção de antenas.
2.  **[Estrutura do Quadro e Protocolo de Enlace](docs/protocolo.md)**
    *   Layout de bytes do frame (Magic Byte, Type, Seq, Len, Payload, FCS).
    *   Detalhamento do algoritmo **Stop-and-Wait ARQ** e diagramas de transição de estado.
3.  **[Detecção de Erros e Assinaturas (FCS)](docs/fcs.md)**
    *   Comparativo técnico entre as assinaturas digitais do **Checksum de 16 bits** e do **CRC-16-CCITT**.
    *   Implementações em C++ e instruções para alternar entre os modos de validação.
4.  **[Como o Projeto Funciona (Guia Didático)](docs/funcionamento.md)**
    *   Explicação didática do ciclo de vida da mensagem ("Hello World") com analogias ilustradas.
    *   Diagrama de sequência visual do fluxo de envio e confirmação.
5.  **[Guia de Uso, Comandos e Logs](docs/guia_uso.md)**
    *   Passo a passo para configurar a Arduino IDE, instalar bibliotecas e gravar os microcontroladores.
    *   Tabela de comandos do Serial Monitor (`ERR ON/OFF`, `COUNT ON/OFF`).
    *   Logs demonstrativos da simulação de ruídos e envio de texto limpo.
6.  **[Requisitos Originais do Projeto (Enunciado)](docs/Projeto%20Final%20-%20Comunica%C3%A7%C3%A3o%20via%20R%C3%A1dio.md)**
    *   Regras, critérios de pontuação e prazos estipulados pelo docente.
