# Guia de Instalação, Execução e Comandos

Esta seção apresenta o roteiro para configuração do ambiente de desenvolvimento, gravação do firmware nas placas ESP32, monitoramento da comunicação serial e uso dos comandos interativos.

---

## 1. Configurando o Ambiente
*   Baixe e instale a **Arduino IDE** (ou utilize o VS Code com a extensão PlatformIO).
*   Na Arduino IDE, vá em `Sketch` -> `Incluir Biblioteca` -> `Gerenciar Bibliotecas`.
*   Pesquise e instale a biblioteca **RadioHead** (desenvolvida por Mike McCauley).

---

## 2. Gravando as Placas ESP32
*   Abra o arquivo `rf_tx_test.ino` na Arduino IDE. Escolha a porta COM correspondente ao seu **ESP32 Transmissor** e clique em Carregar (Upload).
*   Abra o arquivo `rf_rx_test.ino` na Arduino IDE. Escolha a porta COM correspondente ao seu **ESP32 Receptor** e clique em Carregar (Upload).

---

## 3. Monitorando a Comunicação
*   Abra dois monitores seriais diferentes com taxa de transmissão configurada para **`115200` bps**.
*   No monitor serial do Transmissor, você verá a mensagem enviada automaticamente (`contador=X`) ou poderá digitar qualquer frase no campo de texto e enviar pressionando `Enter`.

---

## 4. Comandos Disponíveis no Serial Monitor (Transmissor)

O código do transmissor implementa monitoramento ativo do Serial Monitor para comandos digitados em tempo de execução:

### A. Injeção e Simulação de Erros
*   **Comando `ERR ON`:** Ativa o injetor de erros. A cada 4 frames de dados (`dataFrameCounter % 4 == 0`), a primeira tentativa de envio do quadro é intencionalmente corrompida (`payload[4] ^= 0x5A` ou FCS corrompido), permitindo demonstrar a retransmissão automática após o timeout.
*   **Comando `ERR OFF`:** Desativa a injeção de erros.

### B. Controle do Contador Automático
*   **Comando `COUNT ON` (ou `CNT ON`):** Ativa o envio periódico e automático do contador a cada 3 segundos.
*   **Comando `COUNT OFF` (ou `CNT OFF`):** Desativa o envio periódico do contador, permitindo o monitoramento limpo apenas de mensagens enviadas manualmente pelo usuário.

---

## 5. Logs de Execução Demonstrativos

### Cenário 1: Envio Limpo (Sem Erros)
**Transmissor Serial:**
```text
TX: iniciando protocolo confiável
TX: pronto
TX: digite um texto e pressione Enter para enviar
TX: comandos disponiveis -> ERR ON / ERR OFF / COUNT ON / COUNT OFF
TX: iniciando envio confiável de: Hello World!
TX: enviou DATA seq=0 len=12 tentativa=1
TX: ACK confirmado para seq=0
TX: enviou END seq=1 len=0 tentativa=1
TX: ACK confirmado para seq=1
TX: mensagem concluída com sucesso
```

**Receptor Serial:**
```text
RX: iniciando protocolo confiável
RX: pronto
RX: DATA novo seq=0 len=12
RX: payload parcial = Hello World!
RX: ACK enviado para seq=0
RX: END novo seq=1
RX: ACK enviado para seq=1
RX: mensagem completa reconstruida:
Hello World!
```

### Cenário 2: Transmissão com Erros Injetados (`ERR ON`)
**Transmissor Serial:**
```text
TX: iniciando envio confiável de: Transmissao Confiável RF
TX: enviou DATA seq=0 len=23 tentativa=1
TX: ACK confirmado para seq=0
TX: erro proposital injetado neste DATA
TX: enviou DATA seq=1 len=2 tentativa=1
TX: timeout de ACK, retransmitindo...
TX: enviou DATA seq=1 len=2 tentativa=2
TX: ACK confirmado para seq=1
TX: enviou END seq=0 len=0 tentativa=1
TX: ACK confirmado para seq=0
TX: mensagem concluída com sucesso
```

**Receptor Serial:**
```text
RX: DATA novo seq=0 len=23
RX: payload parcial = Transmissao Confiável
RX: ACK enviado para seq=0
RX: quadro corrompido detectado -> descartado, sem ACK
RX: DATA novo seq=1 len=2
RX: payload parcial = RF
RX: ACK enviado para seq=1
RX: END novo seq=0
RX: ACK enviado para seq=0
RX: mensagem completa reconstruida:
Transmissao Confiável RF
```
