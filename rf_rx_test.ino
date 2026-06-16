#include <LiquidCrystal_I2C.h>
#include <RH_ASK.h>
#include <SPI.h>
#include <Wire.h>

// Instanciação do LCD 20x4 no endereço I2C 0x27
LiquidCrystal_I2C lcd(0x27, 20, 4);

#define RF_BITRATE 500
#define RF_RX_PIN 26
#define RF_TX_PIN 25

#define USE_CRC16 true // tem que bater com o TX

const uint8_t FRAME_MAGIC = 0xA5;
const uint8_t TYPE_DATA = 0x01;
const uint8_t TYPE_ACK = 0x02;
const uint8_t TYPE_END = 0x03;

const uint8_t MAX_PAYLOAD = 24;
const size_t RX_MESSAGE_BUFFER_SIZE = 512;

RH_ASK driver(RF_BITRATE, RF_RX_PIN, RF_TX_PIN, -1, false);

struct DecodedFrame {
  uint8_t type;
  uint8_t seq;
  uint8_t len;
  uint8_t payload[MAX_PAYLOAD];
};

enum ParseStatus {
  FRAME_OK,
  FRAME_TOO_SHORT,
  FRAME_BAD_MAGIC,
  FRAME_BAD_LEN,
  FRAME_BAD_FCS
};

uint8_t rxMessage[RX_MESSAGE_BUFFER_SIZE];
size_t rxMessageLen = 0;
bool chunkReceived[256] = {false};
uint8_t chunkLengths[256] = {0};
uint8_t totalChunks = 0;
unsigned long lastPacketTime = 0;

/*
 * Função: checksum16
 * Objetivo: Calcula uma soma de verificação (checksum) simples de 16 bits sobre
 * os dados recebidos. Funcionamento: Ela soma todos os bytes do vetor. Se a
 * soma ultrapassar 16 bits (estouro), os bits extras (carregamento/carry) são
 * somados de volta no final para garantir que o resultado caiba em 16 bits. Por
 * fim, retorna o complemento de um da soma. Serve para validar se houve erros.
 * Parâmetros:
 *   - data: Ponteiro para o array de bytes a ser verificado.
 *   - len: Quantidade de bytes no array.
 * Retorno: O valor do checksum calculado de 16 bits.
 */
uint16_t checksum16(const uint8_t *data, uint8_t len) {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < len; i++) {
    sum += data[i];
  }
  while (sum >> 16) {
    sum = (sum & 0xFFFF) + (sum >> 16);
  }
  return (uint16_t)(~sum);
}

/*
 * Função: crc16_ccitt
 * Objetivo: Calcula o CRC16 (Cyclic Redundancy Check) utilizando o polinômio
 * padrão CCITT (0x1021). Funcionamento: O CRC é um método de detecção de erros
 * robusto. Ele processa os dados bit a bit simulando uma divisão polinomial
 * binária. A cada bit, realiza operações de deslocamento de bits (shift) e
 * operações lógicas XOR baseadas no polinômio gerador. Usado para garantir a
 * integridade dos dados recebidos. Parâmetros:
 *   - data: Ponteiro para o array de bytes a ser verificado.
 *   - len: Quantidade de bytes no array.
 * Retorno: O código CRC de 16 bits calculado.
 */
uint16_t crc16_ccitt(const uint8_t *data, uint8_t len) {
  uint16_t crc = 0xFFFF;
  for (uint8_t i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (uint8_t b = 0; b < 8; b++) {
      if (crc & 0x8000)
        crc = (crc << 1) ^ 0x1021;
      else
        crc <<= 1;
    }
  }
  return crc;
}

/*
 * Função: calcFCS
 * Objetivo: Calcula a FCS (Frame Check Sequence) do quadro para comparação.
 * Funcionamento: Dependendo da configuração da diretiva 'USE_CRC16' (que deve
 * ser igual à do transmissor), ela decide se o código de detecção de erros será
 * calculado usando o algoritmo CRC16 ou o Checksum de 16 bits. Parâmetros:
 *   - data: Ponteiro para o array de bytes.
 *   - len: Quantidade de bytes.
 * Retorno: O valor da verificação calculado em 16 bits.
 */
uint16_t calcFCS(const uint8_t *data, uint8_t len) {
  return USE_CRC16 ? crc16_ccitt(data, len) : checksum16(data, len);
}

/*
 * Função: typeToStr
 * Objetivo: Converte o código numérico do tipo de quadro em uma representação
 * textual. Funcionamento: Recebe o byte identificador do tipo do pacote e
 * retorna uma string descritiva
 * ("DATA", "ACK", "END", "UNK"). Isso ajuda a mostrar informações formatadas e
 * legíveis no Serial Monitor. Parâmetros:
 *   - type: Byte representando o tipo do quadro.
 * Retorno: Uma string literal constante correspondente ao tipo.
 */
const char *typeToStr(uint8_t type) {
  switch (type) {
  case TYPE_DATA:
    return "DATA";
  case TYPE_ACK:
    return "ACK";
  case TYPE_END:
    return "END";
  default:
    return "UNK";
  }
}

/*
 * Função: buildFrame
 * Objetivo: Constrói um quadro padrão (geralmente usado pelo receptor para
 * gerar o quadro de confirmação ACK). Funcionamento: Organiza a estrutura
 * padrão do cabeçalho de rede (byte mágico, tipo, sequência, tamanho), copia os
 * dados (se houver) e calcula os dois bytes de FCS ao final, guardando o
 * resultado no buffer de saída. Parâmetros:
 *   - type: Tipo do quadro (ex: TYPE_ACK).
 *   - seq: Número de sequência correspondente.
 *   - payload: Dados adicionais (usualmente nulo para o ACK).
 *   - len: Tamanho do payload (usualmente 0 para o ACK).
 *   - out: Buffer onde o quadro finalizado será gerado.
 * Retorno: Tamanho total do quadro construído.
 */
uint8_t buildFrame(uint8_t type, uint8_t seq, const uint8_t *payload,
                   uint8_t len, uint8_t *out) {
  out[0] = FRAME_MAGIC;
  out[1] = type;
  out[2] = seq;
  out[3] = len;

  for (uint8_t i = 0; i < len; i++) {
    out[4 + i] = payload[i];
  }

  uint16_t fcs = calcFCS(out, 4 + len);
  out[4 + len] = lowByte(fcs);
  out[5 + len] = highByte(fcs);

  return len + 6;
}

/*
 * Função: decodeFrame
 * Objetivo: Desempacota e valida a integridade de um quadro recebido.
 * Funcionamento: É um filtro de segurança. Ela checa se o quadro tem tamanho
 * mínimo aceitável (6 bytes), se inicia com o byte mágico correto, se o tamanho
 * do payload informado é condizente e se o código de verificação de erro (FCS)
 * recebido bate com o FCS calculado no momento. Se tudo estiver correto,
 * preenche a estrutura 'frame'. Parâmetros:
 *   - raw: Vetor com bytes brutos recebidos da biblioteca de rádio.
 *   - rawLen: Comprimento dos dados recebidos.
 *   - frame: Estrutura destino onde os campos decodificados serão copiados.
 * Retorno: O status resultante da decodificação (ex: FRAME_OK, FRAME_BAD_FCS).
 */
ParseStatus decodeFrame(const uint8_t *raw, uint8_t rawLen,
                        DecodedFrame &frame) {
  if (rawLen < 6)
    return FRAME_TOO_SHORT;
  if (raw[0] != FRAME_MAGIC)
    return FRAME_BAD_MAGIC;

  uint8_t payloadLen = raw[3];
  if (payloadLen > MAX_PAYLOAD)
    return FRAME_BAD_LEN;
  if (rawLen != payloadLen + 6)
    return FRAME_BAD_LEN;

  uint16_t rxFcs = (uint16_t)raw[rawLen - 2] | ((uint16_t)raw[rawLen - 1] << 8);
  uint16_t calc = calcFCS(raw, rawLen - 2);
  if (rxFcs != calc)
    return FRAME_BAD_FCS;

  frame.type = raw[1];
  frame.seq = raw[2];
  frame.len = payloadLen;
  for (uint8_t i = 0; i < payloadLen; i++) {
    frame.payload[i] = raw[4 + i];
  }

  return FRAME_OK;
}

/*
 * Função: sendAck
 * Objetivo: Envia um pacote de confirmação de recebimento (ACK) de volta ao
 * transmissor. Funcionamento: Após processar um quadro válido de dados, o
 * receptor monta um quadro de ACK com o mesmo número de sequência do quadro
 * recebido. Ele introduz um pequeno atraso (delay de 450ms) para dar tempo de a
 * biblioteca de rádio do transmissor passar de transmissão para escuta, envia o
 * ACK e aguarda a conclusão. Parâmetros:
 *   - seq: O número de sequência a ser confirmado.
 */
void sendAck(uint8_t seq) {
  delay(450);

  uint8_t ackBuf[16];
  uint8_t ackLen = buildFrame(TYPE_ACK, seq, nullptr, 0, ackBuf);

  driver.send(ackBuf, ackLen);
  driver.waitPacketSent();

  Serial.printf("RX: ACK enviado para seq=%u\n", seq);
}

/*
 * Função: clearAssemblyBuffer
 * Objetivo: Limpa o buffer acumulador de mensagens e os estados dos fragmentos.
 * Funcionamento: Reseta o tamanho acumulador, o total de blocos esperados e
 * zera os vetores que rastreiam quais fragmentos foram recebidos.
 */
void clearAssemblyBuffer() {
  rxMessageLen = 0;
  totalChunks = 0;
  memset(chunkReceived, 0, sizeof(chunkReceived));
  memset(chunkLengths, 0, sizeof(chunkLengths));
}

/*
 * Função: writePayloadAtOffset
 * Objetivo: Grava um fragmento de dados recebido na posição correta do buffer
 * geral da mensagem. Funcionamento: Utiliza o offset correspondente (seq *
 * MAX_PAYLOAD) para gravar os bytes recebidos. Possui proteção contra estouro
 * de memória para não corromper o buffer de destino. Parâmetros:
 *   - data: Ponteiro para o payload recebido.
 *   - len: Tamanho do payload.
 *   - offset: Posição de escrita no buffer.
 */
void writePayloadAtOffset(const uint8_t *data, uint8_t len, uint16_t offset) {
  if (offset + len > RX_MESSAGE_BUFFER_SIZE) {
    Serial.println("RX: buffer da mensagem cheio, descartando bloco");
    return;
  }
  memcpy(rxMessage + offset, data, len);
}

/*
 * Função: showTextOnLcd
 * Objetivo: Exibe o texto recebido de forma formatada nas quatro linhas do
 * display LCD 20x4. Funcionamento: Limpa o visor, posiciona o cursor e escreve
 * os caracteres respeitando o limite de 20 caracteres por linha, quebrando o
 * texto automaticamente para as linhas seguintes. Parâmetros:
 *   - text: Ponteiro para a string de texto a ser exibida.
 */
void showTextOnLcd(const char *text) {
  lcd.clear();

  size_t len = strlen(text);

  // Mapeamento dinâmico de caracteres (suporte a building blocks)
  auto printChar = [](char c) -> uint8_t {
    if (c == '#') return 255; // Bloco totalmente aceso no LCD
    if (c == '.') return ' '; // Espaço vazio
    if (c >= '0' && c <= '7') return c - '0'; // Custom characters 0-7
    return (uint8_t)c;
  };

  // Linha 0 (caracteres 0 a 19)
  lcd.setCursor(0, 0);
  for (size_t i = 0; i < len && i < 20; i++) {
    lcd.write(printChar(text[i]));
  }

  // Linha 1 (caracteres 20 a 39)
  if (len > 20) {
    lcd.setCursor(0, 1);
    for (size_t i = 20; i < len && i < 40; i++) {
      lcd.write(printChar(text[i]));
    }
  }

  // Linha 2 (caracteres 40 a 59)
  if (len > 40) {
    lcd.setCursor(0, 2);
    for (size_t i = 40; i < len && i < 60; i++) {
      lcd.write(printChar(text[i]));
    }
  }

  // Linha 3 (caracteres 60 a 79)
  if (len > 60) {
    lcd.setCursor(0, 3);
    for (size_t i = 60; i < len && i < 80; i++) {
      lcd.write(printChar(text[i]));
    }
  }
}

/*
 * Função: onCompleteMessage
 * Objetivo: Processa e exibe a mensagem de texto completa quando todos os
 * pedaços foram recebidos. Funcionamento: É acionada após a recepção
 * bem-sucedida do END e confirmação de todos os fragmentos. Adiciona o
 * caractere nulo ('\0') para formar uma String válida, exibe-a no Serial
 * Monitor, no display LCD I2C e, em seguida, limpa o buffer para a próxima
 * mensagem.
 */
void onCompleteMessage() {
  // Verifica se é um pacote de imagem dinâmica
  if (rxMessageLen >= 4 && rxMessage[0] == 0x1B && rxMessage[1] == 'I' && rxMessage[2] == 'M' && rxMessage[3] == 'G') {
    Serial.println("RX: pacote de imagem dinâmica detectado. Carregando CGRAM...");

    uint8_t numCustom = rxMessage[4];
    size_t headerSize = 5 + 8 * numCustom;

    if (rxMessageLen < headerSize + 80) {
      Serial.printf("RX: Erro - Tamanho do pacote de imagem inválido (recebido %u bytes, esperado %u)\n", rxMessageLen, headerSize + 80);
      clearAssemblyBuffer();
      return;
    }

    // Define os caracteres customizados dinamicamente na CGRAM
    for (uint8_t i = 0; i < numCustom && i < 8; i++) {
      uint8_t bitmap[8];
      memcpy(bitmap, rxMessage + 5 + i * 8, 8);
      lcd.createChar(i, bitmap);
    }

    // Exibe a imagem no LCD usando os novos caracteres
    lcd.clear();

    // Mapeamento de pixels para a imagem dinâmica
    auto printChar = [](char c) -> uint8_t {
      if (c == '#') return 255; // Bloco totalmente aceso
      if (c == '.') return ' '; // Espaço vazio
      if (c >= '0' && c <= '7') return c - '0'; // Caractere customizado correspondente
      return (uint8_t)c;
    };

    const uint8_t *screenData = rxMessage + headerSize;

    // Linha 0 (0-19)
    lcd.setCursor(0, 0);
    for (size_t i = 0; i < 20; i++) {
      lcd.write(printChar(screenData[i]));
    }

    // Linha 1 (20-39)
    lcd.setCursor(0, 1);
    for (size_t i = 20; i < 40; i++) {
      lcd.write(printChar(screenData[i]));
    }

    // Linha 2 (40-59)
    lcd.setCursor(0, 2);
    for (size_t i = 40; i < 60; i++) {
      lcd.write(printChar(screenData[i]));
    }

    // Linha 3 (60-79)
    lcd.setCursor(0, 3);
    for (size_t i = 60; i < 80; i++) {
      lcd.write(printChar(screenData[i]));
    }

    Serial.println("RX: Imagem dinâmica renderizada no LCD com sucesso!");
    clearAssemblyBuffer();
    return;
  }

  char text[RX_MESSAGE_BUFFER_SIZE + 1];
  memcpy(text, rxMessage, rxMessageLen);
  text[rxMessageLen] = '\0';

  Serial.println("RX: mensagem completa reconstruida:");
  Serial.println(text);

  // Exibe a mensagem no display LCD I2C
  showTextOnLcd(text);

  clearAssemblyBuffer();
}

/*
 * Função: processValidFrame
 * Objetivo: Processa um quadro já decodificado e validado aplicando a lógica do
 * Selective Repeat ARQ. Funcionamento: Analisa o tipo do quadro:
 *   - Se for DATA: Se for o primeiro bloco de uma nova mensagem, atualiza o LCD
 * para "Recebendo...". Grava o payload na posição correspondente ('seq *
 * MAX_PAYLOAD'), marca o bloco como recebido e envia o ACK para 'seq'.
 *   - Se for END: Armazena o total de blocos esperados (que vem no campo
 * 'seq'). Verifica se todos os fragmentos de 0 a N-1 foram recebidos. Se
 * estiver completo, envia o ACK para 'N', calcula o tamanho total e reconstrói
 * a mensagem. Se incompleto, não envia o ACK para que o transmissor
 * retransmita. Parâmetros:
 *   - frame: Estrutura contendo o quadro decodificado de forma amigável.
 */
void processValidFrame(const DecodedFrame &frame) {
  if (frame.type == TYPE_DATA || frame.type == TYPE_END) {
    lastPacketTime = millis();
  }

  if (frame.type == TYPE_DATA) {
    uint8_t seq = frame.seq;

    // Se for o primeiro fragmento recebido de uma nova mensagem, limpa a tela e
    // mostra status
    bool anyReceived = false;
    for (int i = 0; i < 256; i++) {
      if (chunkReceived[i]) {
        anyReceived = true;
        break;
      }
    }

    if (!anyReceived) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Recebendo...");
    }

    if (seq < 256) {
      if (!chunkReceived[seq]) {
        chunkReceived[seq] = true;
        chunkLengths[seq] = frame.len;

        uint16_t offset = seq * MAX_PAYLOAD;
        writePayloadAtOffset(frame.payload, frame.len, offset);

        Serial.printf("RX: DATA novo seq=%u len=%u armazenado no offset=%u\n",
                      seq, frame.len, offset);
      } else {
        Serial.printf("RX: DATA duplicado seq=%u recebido novamente\n", seq);
      }

      // Envia o ACK correspondente ao fragmento recebido
      sendAck(seq);
    }
    return;
  }

  if (frame.type == TYPE_END) {
    uint8_t expectedTotal = frame.seq;
    totalChunks = expectedTotal;

    Serial.printf("RX: END recebido indicando total de %u quadros. Verificando "
                  "integridade da mensagem...\n",
                  totalChunks);

    // Verifica se todos os fragmentos de 0 a totalChunks-1 foram recebidos
    bool complete = true;
    for (uint8_t i = 0; i < totalChunks; i++) {
      if (!chunkReceived[i]) {
        complete = false;
        Serial.printf("RX: Falta o quadro seq=%u\n", i);
      }
    }

    if (complete) {
      // Envia ACK para o END (o número total de chunks atua como seq para o
      // END)
      sendAck(totalChunks);

      // Calcula o tamanho real total somando os comprimentos dos blocos
      // recebidos
      rxMessageLen = 0;
      for (uint8_t i = 0; i < totalChunks; i++) {
        rxMessageLen += chunkLengths[i];
      }

      onCompleteMessage();
    } else {
      Serial.println("RX: Mensagem incompleta. Aguardando retransmissão de "
                     "quadros perdidos.");
    }
    return;
  }

  if (frame.type == TYPE_ACK) {
    Serial.printf("RX: ACK inesperado seq=%u ignorado\n", frame.seq);
    return;
  }
}

/*
 * Função: setup
 * Objetivo: Inicializa o hardware, o LCD I2C, a porta serial e a biblioteca de
 * comunicação RF no receptor. Funcionamento: É executada uma única vez na
 * inicialização.
 */
void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println("RX: iniciando protocolo confiável");

  // Configura os pinos I2C e inicializa o LCD
  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();

  // Definição dos 8 caracteres customizados (building blocks para curvas, colunas e linhas)
  uint8_t cc0[8] = { 0b00111, 0b01111, 0b11111, 0b11111, 0b11111, 0b11111, 0b11111, 0b11111 }; // 0: Top-Left Corner
  uint8_t cc1[8] = { 0b11100, 0b11110, 0b11111, 0b11111, 0b11111, 0b11111, 0b11111, 0b11111 }; // 1: Top-Right Corner
  uint8_t cc2[8] = { 0b11111, 0b11111, 0b11111, 0b11111, 0b11111, 0b11111, 0b01111, 0b00111 }; // 2: Bottom-Left Corner
  uint8_t cc3[8] = { 0b11111, 0b11111, 0b11111, 0b11111, 0b11111, 0b11111, 0b11110, 0b11100 }; // 3: Bottom-Right Corner
  uint8_t cc4[8] = { 0b11100, 0b11100, 0b11100, 0b11100, 0b11100, 0b11100, 0b11100, 0b11100 }; // 4: Left Column
  uint8_t cc5[8] = { 0b00111, 0b00111, 0b00111, 0b00111, 0b00111, 0b00111, 0b00111, 0b00111 }; // 5: Right Column
  uint8_t cc6[8] = { 0b11111, 0b11111, 0b11111, 0b11111, 0b00000, 0b00000, 0b00000, 0b00000 }; // 6: Top Row
  uint8_t cc7[8] = { 0b00000, 0b00000, 0b00000, 0b00000, 0b11111, 0b11111, 0b11111, 0b11111 }; // 7: Bottom Row

  lcd.createChar(0, cc0);
  lcd.createChar(1, cc1);
  lcd.createChar(2, cc2);
  lcd.createChar(3, cc3);
  lcd.createChar(4, cc4);
  lcd.createChar(5, cc5);
  lcd.createChar(6, cc6);
  lcd.createChar(7, cc7);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("RX pronto");
  lcd.setCursor(0, 1);
  lcd.print("Aguardando mensagem...");

  if (!driver.init()) {
    Serial.println("RX: falha ao iniciar RH_ASK");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Erro: RH_ASK");
    while (true)
      delay(1000);
  }

  clearAssemblyBuffer();
  Serial.println("RX: pronto");
}

/*
 * Função: loop
 * Objetivo: Loop de execução contínua no receptor.
 */
void loop() {
  // Verifica se há alguma mensagem em andamento e limpa por inatividade
  // (timeout de 4 segundos)
  bool active = false;
  for (int i = 0; i < 256; i++) {
    if (chunkReceived[i]) {
      active = true;
      break;
    }
  }

  if (active && (millis() - lastPacketTime > 8000)) {
    Serial.println("RX: tempo limite de inatividade atingido (4s), limpando "
                   "buffer de montagem");
    clearAssemblyBuffer();

    // Restaura as mensagens iniciais do LCD
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("RX pronto");
    lcd.setCursor(0, 1);
    lcd.print("Aguardando mensagem...");
  }

  uint8_t raw[64];
  uint8_t rawLen = sizeof(raw);

  if (driver.recv(raw, &rawLen)) {
    DecodedFrame frame;
    ParseStatus st = decodeFrame(raw, rawLen, frame);

    if (st == FRAME_BAD_FCS) {
      Serial.println("RX: quadro corrompido detectado -> descartado, sem ACK");
      return;
    }

    if (st != FRAME_OK) {
      Serial.printf("RX: quadro invalido (status=%d) descartado\n", st);
      return;
    }

    processValidFrame(frame);
  }

  delay(10);
}
