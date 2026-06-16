# Como o Projeto Funciona: Passo a Passo

Este documento apresenta a descrição detalhada e o fluxo de execução lógica do protocolo de comunicação de dados sem fio estabelecido entre o transmissor e o receptor, utilizando uma abordagem estruturada passo a passo sob o modelo **Selective Repeat ARQ**.

---

## A Analogia com o Sistema Postal

Para elucidar os princípios de transmissão por radiofrequência (RF) e o controle de fluxo projetado, pode-se traçar um paralelo com um serviço postal de entrega com janela deslizante:
*   **Fragmentação de Dados:** Um documento extenso (mensagem de texto) é subdividido em páginas menores para caber em envelopes com capacidade restrita a 24 caracteres (limite definido pelo `MAX_PAYLOAD`).
*   **Janela Deslizante de Transmissão:** O remetente pode colocar na caixa de correio até 4 envelopes consecutivos (tamanho da janela $W = 4$) de uma única vez, sem a necessidade de esperar que o destinatário confirme o primeiro para selar os demais.
*   **Confirmação Seletiva (ACK Individual):** O destinatário envia um cartão postal confirmando individualmente o recebimento de cada página específica (ex: "Recebi o envelope 2").
*   **Retransmissão Seletiva:** Se o envelope 1 for extraviado mas os envelopes 0, 2 e 3 forem entregues com sucesso, o remetente receberá confirmações para os envelopes 0, 2 e 3. Ele identificará que apenas o envelope 1 falhou e retransmitirá **exclusivamente o envelope 1**, otimizando a eficiência e o uso do canal.
*   **Reorganização no Destinatário:** O destinatário armazena os envelopes recebidos fora de ordem e os posiciona na ordem correta das páginas correspondentes, fazendo a leitura do documento apenas quando todas as páginas estiverem presentes.

---

## 1. Parâmetros Operacionais (Constantes)

Antes da compilação, o sistema configura as seguintes diretivas globais que determinam o comportamento operacional dos dispositivos:

*   **`RF_BITRATE 500`:** Define a taxa de transmissão física do sinal de rádio para 500 bits por segundo. Uma taxa baixa é adotada para minimizar a taxa de erro de bits sob condições severas de ruído ambiental.
*   **`WINDOW_SIZE 4`:** Tamanho da janela deslizante que estipula o número máximo de pacotes que o transmissor pode enviar consecutivamente sem receber confirmações intermediárias.
*   **`USE_CRC16`:** Seletor lógico que define o algoritmo de integridade. Se definido como `true`, o sistema utiliza o mecanismo matemático **CRC-16-CCITT**; se `false`, adota-se o método **Checksum de 16 bits**.
*   **`ACK_TIMEOUT_MS 2500`:** Limite de tempo (2,5 segundos) que o transmissor aguarda pela resposta de confirmação de cada pacote individual antes de disparar sua retransmissão seletiva.
*   **`MAX_RETRIES 6`:** Número máximo de tentativas de retransmissão permitidas para cada bloco individual antes do encerramento da conexão devido à instabilidade do canal físico.

---

## 2. Inicialização dos Dispositivos (`setup()`)

Ao energizar as placas microcontroladoras ESP32, as seguintes rotinas de hardware e software são executadas:

1.  **Rotina do Transmissor (TX):**
    *   Inicializa a comunicação serial via USB a 115200 bps (`Serial.begin`) para permitir o envio de dados via terminal e exibição de logs.
    *   Inicializa o driver de radiofrequência (`driver.init()`).
    *   Exibe no monitor de saída a tela de inicialização do sistema com os comandos disponíveis.
2.  **Rotina do Receptor (RX):**
    *   Inicializa a comunicação serial USB.
    *   Inicializa o driver de recepção de radiofrequência.
    *   Inicializa e limpa a tela do display LCD físico (I2C), exibindo a mensagem de status `"RX pronto"` na primeira linha e `"Aguardando..."` na segunda.

---

## 3. Fluxo de Transmissão da Mensagem "Hello World"

Abaixo descreve-se o caminho e a transformação dos dados desde a entrada do comando do usuário até a exibição no receptor:

```mermaid
flowchart LR
    A["Terminal do Usuário"] -->|Texto: 'Hello World'| B["ESP32 Transmissor"]
    B -->|Construção do Quadro e Cálculo do FCS| C["Antena Transmissora"]
    C -->|Ondas de Rádio 433 MHz| D["Antena Receptora"]
    D -->|Validação do FCS e Reconstrução| E["Display LCD / RX"]
```

### Fase A: Processamento e Envio (Transmissor)
1.  O usuário envia a string `"Hello World"` através do terminal serial do transmissor.
2.  O sistema identifica a entrada e chama o método **`sendTextMessage()`**.
3.  Como a string contém 11 bytes, ela é dividida em apenas **1 fragmento** (índice 0), pois o tamanho é inferior ao limite de payload de 24 bytes.
4.  O método **`buildFrame()`** monta o quadro de dados com cabeçalho e rodapé:
    *   Insere o byte delimitador de início de quadro `0xA5` (Magic Byte).
    *   Configura os metadados: tipo de quadro (`TYPE_DATA`), sequência (`0`, indicando o índice absoluto do bloco) e tamanho útil (`11`).
    *   Copia a mensagem `"Hello World"` para a seção de payload.
    *   Chama a função **`calcFCS()`** para calcular a assinatura digital (CRC-16 ou Checksum) e anexa os 2 bytes resultantes ao final do quadro.
5.  O transmissor emite o quadro no ar e registra o tempo de envio em `lastSentTime[0]`. Em seguida, inicia o período de 500ms de escuta ativa por ACKs (`listenForAcksDuring()`).

### Fase B: Recepção e Validação (Receptor)
1.  O receptor detecta as oscilações de rádio e realiza a leitura do buffer bruto recebido.
2.  Como é o primeiro bloco recebido da mensagem, o receptor limpa a tela do LCD e exibe temporariamente a mensagem `"Recebendo..."`.
3.  Chama a função **`decodeFrame()`** para validação:
    *   Verifica a assinatura inicial `0xA5` e se o tamanho do pacote é coerente.
    *   Recalcula localmente o FCS e compara com os bytes finais recebidos do transmissor.
    *   **Quadro Íntegro:** O receptor armazena o payload no buffer interno no local exato correspondente ao índice (neste caso, offset `0 * MAX_PAYLOAD`).
4.  O receptor introduz uma pausa de **450 milissegundos** para comutação de hardware e envia de volta um quadro de confirmação (**ACK**) contendo `seq = 0`.

### Fase C: Conclusão do Protocolo (Transmissor & Receptor)
1.  O transmissor capta o `ACK(0)` dentro da janela de escuta, marca o bloco 0 como confirmado (`acked[0] = true`) e avança a janela.
2.  Como todos os fragmentos da mensagem foram confirmados, o transmissor monta e envia um quadro de encerramento do tipo **`TYPE_END`** contendo `seq = 1` (total de fragmentos enviados).
3.  O receptor capta o quadro `END` indicando total de 1 bloco. Ele verifica se todos os blocos do intervalo `0` a `0` foram recebidos.
    *   Constatando a integridade, o receptor envia o `ACK(1)` correspondente ao transmissor e chama a função **`onCompleteMessage()`**.
    *   A função insere o caractere terminador `\0`, imprime a string `"Hello World"` no monitor serial e no LCD físico (permanecendo persistente na tela).
    *   O receptor limpa o buffer e os estados internos de fragmentos, aguardando o próximo ciclo de comunicação.

---

## 4. Diagrama de Estados do Protocolo

O fluxo a seguir ilustra a sequência de troca de mensagens, confirmações e retransmissões seletivas sob o protocolo Selective Repeat ARQ:

```mermaid
sequenceDiagram
    autonumber
    actor Usuario as Usuário (Serial Monitor)
    participant TX as Transmissor (TX)
    participant Canal as Canal de Rádio (Ar)
    participant RX as Receptor (RX)
    participant LCD as Tela LCD (Receptor)

    Note over Usuario, TX: 1. Envio de Mensagem Multi-quadro
    Usuario->>TX: Insere "Mensagem Longa" via Terminal
    TX->>TX: Segmenta a mensagem em 2 blocos (Seq=0 e Seq=1)
    
    TX->>Canal: Transmite DATA (Seq=0)
    Note over Canal: Ruído corrompe o frame (descartado pelo RX)
    
    TX->>Canal: Transmite DATA (Seq=1)
    Canal->>RX: Entrega DATA (Seq=1)
    Note over RX: LCD exibe "Recebendo..."
    RX->>RX: Valida FCS (OK) e armazena no offset 1
    RX->>Canal: Transmite Confirmação ACK (Seq=1)
    Canal->>TX: Entrega ACK (Seq=1)
    
    Note over TX: Timeout do Seq=0 estourou (Retransmissão Seletiva)
    TX->>Canal: Retransmite apenas DATA (Seq=0)
    Canal->>RX: Entrega DATA (Seq=0)
    RX->>RX: Valida FCS (OK) e armazena no offset 0
    RX->>Canal: Transmite Confirmação ACK (Seq=0)
    Canal->>TX: Entrega ACK (Seq=0)
    
    Note over TX: Todos os blocos confirmados (Seq 0 e 1)
    TX->>Canal: Transmite END (Seq=2)
    Canal->>RX: Entrega END (Seq=2)
    RX->>RX: Confirma que todos os 2 fragmentos foram recebidos
    RX->>LCD: Imprime "Mensagem Longa" no visor físico
    RX->>Canal: Transmite ACK (Seq=2) para o END
    Canal->>TX: Entrega ACK (Seq=2)
    Note over TX: Envio concluído com sucesso!
```

---

## 5. Fluxo de Transmissão de Imagens Dinâmicas

A transmissão de imagens dinâmicas segue o mesmo modelo de controle de fluxo e detecção de erros por Selective Repeat ARQ, adicionando etapas de conversão e processamento de dados nas extremidades da comunicação:

```mermaid
flowchart TD
    subgraph Computador ["1. Computador de Origem"]
        A["Imagem (100x32 pixels)"] --> B("Quantização Vetorial")
        B --> C["Pacote Binário (Header + CGRAM + Tela)"]
        C --> D("Codificação Hexadecimal")
    end

    subgraph Transmissor ["2. ESP32 Transmissor"]
        E["Monitor Serial (Comando IMG:...)"] --> F["Decodificação Hexadecimal"]
        F --> G["Fatiamento e Envio Confiável (Selective Repeat)"]
    end

    subgraph Receptor ["3. ESP32 Receptor"]
        H["Recepção e Validação de Enlace"] --> I{"Detecta Assinatura IMG?"}
        I -- Sim --> J["Atualiza CGRAM (Registra até 8 bitmaps)"]
        I -- Sim --> K["Limpa e Renderiza no LCD (Mapeamento de 80 posições)"]
    end

    D --> E
    G -->|Canal Sem Fio RF 433 MHz| H
```

1.  **Geração do Pacote (Computador):** O script `image_encoder.py` lê o arquivo de imagem, reduz a escala de pixels para $100 \times 32$ e localiza as 8 geometrias mais recorrentes. Ele compõe um cabeçalho fixo com `0x1B, 'I', 'M', 'G'`, empacota os bitmaps de 5x8 correspondentes para serem carregados na CGRAM e gera a matriz de endereçamento da tela com 80 bytes. O pacote binário completo é codificado em hexadecimal para evitar perdas de caracteres de controle ao ser enviado pelo Serial Monitor.
2.  **Transmissão Confiável (Transmissor):** O transmissor lê a linha via monitor serial, valida o prefixo `IMG:` e realiza a conversão inversa do texto hexadecimal para bytes puros. O buffer binário resultante é fatiado em pedaços de 24 bytes e transmitido de forma confiável pelo meio físico sem fio com suporte à retransmissão seletiva.
3.  **Processamento de Hardware (Receptor):** Ao consolidar o recebimento confiável da mensagem com a recepção do frame `END`, o receptor identifica os bytes identificadores no início do buffer. O receptor extrai a quantidade de caracteres customizados, realiza as chamadas `lcd.createChar()` para reconfigurar a memória do display de forma transparente e envia os 80 bytes do visor para desenho imediato no visor LCD.

