#include <RH_ASK.h>
#include <SPI.h>

#define RF_BITRATE 500
#define RF_RX_PIN 26
#define RF_TX_PIN 25

#define USE_CRC16 true   // tem que bater com o TX

const uint8_t FRAME_MAGIC = 0xA5;
const uint8_t TYPE_DATA   = 0x01;
const uint8_t TYPE_ACK    = 0x02;
const uint8_t TYPE_END    = 0x03;

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

uint8_t expectedSeq = 0;
uint8_t rxMessage[RX_MESSAGE_BUFFER_SIZE];
size_t rxMessageLen = 0;

/*
 * Função: checksum16
 * Objetivo: Calcula uma soma de verificação (checksum) simples de 16 bits sobre os dados recebidos.
 * Funcionamento: Ela soma todos os bytes do vetor. Se a soma ultrapassar 16 bits (estouro),
 * os bits extras (carregamento/carry) são somados de volta no final para garantir que o resultado
 * caiba em 16 bits. Por fim, retorna o complemento de um da soma. Serve para validar se houve erros.
 * Parâmetros:
 *   - data: Ponteiro para o array de bytes a ser verificado.
 *   - len: Quantidade de bytes no array.
 * Retorno: O valor do checksum calculado de 16 bits.
 */
uint16_t checksum16(const uint8_t* data, uint8_t len) {
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
 * Objetivo: Calcula o CRC16 (Cyclic Redundancy Check) utilizando o polinômio padrão CCITT (0x1021).
 * Funcionamento: O CRC é um método de detecção de erros robusto. Ele processa os dados bit a bit
 * simulando uma divisão polinomial binária. A cada bit, realiza operações de deslocamento de bits (shift)
 * e operações lógicas XOR baseadas no polinômio gerador. Usado para garantir a integridade dos dados recebidos.
 * Parâmetros:
 *   - data: Ponteiro para o array de bytes a ser verificado.
 *   - len: Quantidade de bytes no array.
 * Retorno: O código CRC de 16 bits calculado.
 */
uint16_t crc16_ccitt(const uint8_t* data, uint8_t len) {
  uint16_t crc = 0xFFFF;
  for (uint8_t i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (uint8_t b = 0; b < 8; b++) {
      if (crc & 0x8000) crc = (crc << 1) ^ 0x1021;
      else              crc <<= 1;
    }
  }
  return crc;
}

/*
 * Função: calcFCS
 * Objetivo: Calcula a FCS (Frame Check Sequence) do quadro para comparação.
 * Funcionamento: Dependendo da configuração da diretiva 'USE_CRC16' (que deve ser igual à do transmissor),
 * ela decide se o código de detecção de erros será calculado usando o algoritmo CRC16 ou o Checksum de 16 bits.
 * Parâmetros:
 *   - data: Ponteiro para o array de bytes.
 *   - len: Quantidade de bytes.
 * Retorno: O valor da verificação calculado em 16 bits.
 */
uint16_t calcFCS(const uint8_t* data, uint8_t len) {
  return USE_CRC16 ? crc16_ccitt(data, len) : checksum16(data, len);
}

/*
 * Função: typeToStr
 * Objetivo: Converte o código numérico do tipo de quadro em uma representação textual.
 * Funcionamento: Recebe o byte identificador do tipo do pacote e retorna uma string descritiva
 * ("DATA", "ACK", "END", "UNK"). Isso ajuda a mostrar informações formatadas e legíveis no Serial Monitor.
 * Parâmetros:
 *   - type: Byte representando o tipo do quadro.
 * Retorno: Uma string literal constante correspondente ao tipo.
 */
const char* typeToStr(uint8_t type) {
  switch (type) {
    case TYPE_DATA: return "DATA";
    case TYPE_ACK:  return "ACK";
    case TYPE_END:  return "END";
    default:        return "UNK";
  }
}

/*
 * Função: buildFrame
 * Objetivo: Constrói um quadro padrão (geralmente usado pelo receptor para gerar o quadro de confirmação ACK).
 * Funcionamento: Organiza a estrutura padrão do cabeçalho de rede (byte mágico, tipo, sequência, tamanho),
 * copia os dados (se houver) e calcula os dois bytes de FCS ao final, guardando o resultado no buffer de saída.
 * Parâmetros:
 *   - type: Tipo do quadro (ex: TYPE_ACK).
 *   - seq: Número de sequência correspondente.
 *   - payload: Dados adicionais (usualmente nulo para o ACK).
 *   - len: Tamanho do payload (usualmente 0 para o ACK).
 *   - out: Buffer onde o quadro finalizado será gerado.
 * Retorno: Tamanho total do quadro construído.
 */
uint8_t buildFrame(uint8_t type, uint8_t seq, const uint8_t* payload, uint8_t len, uint8_t* out) {
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
 * Funcionamento: É um filtro de segurança. Ela checa se o quadro tem tamanho mínimo aceitável (6 bytes),
 * se inicia com o byte mágico correto, se o tamanho do payload informado é condizente e se o código de verificação
 * de erro (FCS) recebido bate com o FCS calculado no momento. Se tudo estiver correto, preenche a estrutura 'frame'.
 * Parâmetros:
 *   - raw: Vetor com bytes brutos recebidos da biblioteca de rádio.
 *   - rawLen: Comprimento dos dados recebidos.
 *   - frame: Estrutura destino onde os campos decodificados serão copiados.
 * Retorno: O status resultante da decodificação (ex: FRAME_OK, FRAME_BAD_FCS).
 */
ParseStatus decodeFrame(const uint8_t* raw, uint8_t rawLen, DecodedFrame& frame) {
  if (rawLen < 6) return FRAME_TOO_SHORT;
  if (raw[0] != FRAME_MAGIC) return FRAME_BAD_MAGIC;

  uint8_t payloadLen = raw[3];
  if (payloadLen > MAX_PAYLOAD) return FRAME_BAD_LEN;
  if (rawLen != payloadLen + 6) return FRAME_BAD_LEN;

  uint16_t rxFcs = (uint16_t)raw[rawLen - 2] | ((uint16_t)raw[rawLen - 1] << 8);
  uint16_t calc  = calcFCS(raw, rawLen - 2);
  if (rxFcs != calc) return FRAME_BAD_FCS;

  frame.type = raw[1];
  frame.seq  = raw[2];
  frame.len  = payloadLen;
  for (uint8_t i = 0; i < payloadLen; i++) {
    frame.payload[i] = raw[4 + i];
  }

  return FRAME_OK;
}

/*
 * Função: sendAck
 * Objetivo: Envia um pacote de confirmação de recebimento (ACK) de volta ao transmissor.
 * Funcionamento: Após processar um quadro válido de dados, o receptor monta um quadro de ACK
 * com o mesmo número de sequência do quadro recebido. Ele introduz um pequeno atraso (delay de 450ms) para 
 * dar tempo de a biblioteca de rádio do transmissor passar de transmissão para escuta, envia o ACK e aguarda a conclusão.
 * Parâmetros:
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
 * Objetivo: Limpa o buffer acumulador de mensagens.
 * Funcionamento: Zera o tamanho acumulado da mensagem recebida (rxMessageLen = 0), preparando
 * o buffer para receber novos conjuntos de fragmentos.
 */
void clearAssemblyBuffer() {
  rxMessageLen = 0;
}

/*
 * Função: appendPayload
 * Objetivo: Acumula um fragmento de payload de dados recebido no buffer geral da mensagem.
 * Funcionamento: Como as mensagens grandes vêm fragmentadas em vários pacotes menores,
 * essa função anexa os novos dados recebidos ao final do acumulador temporário 'rxMessage'. Ela também
 * protege contra estouro de memória caso a mensagem seja maior que a capacidade do buffer.
 * Parâmetros:
 *   - data: Ponteiro para o pedaço de payload recebido.
 *   - len: Tamanho desse pedaço de payload.
 */
void appendPayload(const uint8_t* data, uint8_t len) {
  if (rxMessageLen + len > RX_MESSAGE_BUFFER_SIZE) {
    Serial.println("RX: buffer da mensagem cheio, descartando montagem atual");
    clearAssemblyBuffer();
    return;
  }

  memcpy(rxMessage + rxMessageLen, data, len);
  rxMessageLen += len;
}

/*
 * Função: onCompleteMessage
 * Objetivo: Processa e exibe a mensagem de texto completa quando todos os pedaços foram recebidos.
 * Funcionamento: É acionada após o recebimento do quadro especial de término (TYPE_END).
 * Ela adiciona um caractere nulo ('\0') no final do buffer de bytes para que ele se comporte como uma String de C
 * válida e exibe a mensagem completa no Serial Monitor. Após isso, limpa o buffer para o próximo ciclo de recepção.
 */
void onCompleteMessage() {
  char text[RX_MESSAGE_BUFFER_SIZE + 1];
  memcpy(text, rxMessage, rxMessageLen);
  text[rxMessageLen] = '\0';

  Serial.println("RX: mensagem completa reconstruida:");
  Serial.println(text);

  // TODO quando adicionar o LCD:
  // showTextOnLcd(text);

  clearAssemblyBuffer();
}

/*
 * Função: processValidFrame
 * Objetivo: Processa um quadro já decodificado e validado e executa a lógica do protocolo Stop-and-Wait.
 * Funcionamento: Analisa o tipo do quadro:
 *   - Se for DATA com a sequência esperada, anexa ao buffer, imprime o fragmento, envia o ACK correspondente
 *     e inverte a sequência esperada (0 vira 1, 1 vira 0). Se for DATA duplicado, apenas reenvia o ACK.
 *   - Se for END (fim da mensagem) com a sequência esperada, envia ACK, inverte a sequência esperada e 
 *     chama a rotina para mostrar a mensagem completa.
 * Parâmetros:
 *   - frame: Estrutura contendo o quadro decodificado de forma amigável.
 */
void processValidFrame(const DecodedFrame& frame) {
  if (frame.type == TYPE_DATA) {
    if (frame.seq == expectedSeq) {
      appendPayload(frame.payload, frame.len);

      Serial.printf("RX: DATA novo seq=%u len=%u\n", frame.seq, frame.len);
      Serial.print("RX: payload parcial = ");
      for (uint8_t i = 0; i < frame.len; i++) Serial.write(frame.payload[i]);
      Serial.println();

      sendAck(frame.seq);
      expectedSeq ^= 1;
      return;
    }

    Serial.printf("RX: DATA duplicado seq=%u -> reenviando ACK sem reprocessar\n", frame.seq);
    sendAck(frame.seq);
    return;
  }

  if (frame.type == TYPE_END) {
    if (frame.seq == expectedSeq) {
      Serial.printf("RX: END novo seq=%u\n", frame.seq);
      sendAck(frame.seq);
      expectedSeq ^= 1;
      onCompleteMessage();
      return;
    }

    Serial.printf("RX: END duplicado seq=%u -> reenviando ACK\n", frame.seq);
    sendAck(frame.seq);
    return;
  }

  if (frame.type == TYPE_ACK) {
    Serial.printf("RX: ACK inesperado seq=%u ignorado\n", frame.seq);
    return;
  }
}

/*
 * Função: setup
 * Objetivo: Inicializa o hardware, a porta serial e a biblioteca de comunicação RF no receptor.
 * Funcionamento: É executada uma única vez quando o ESP32 inicia. Define a taxa da porta serial,
 * inicializa o driver de RF (RH_ASK) e zera o buffer de montagem da mensagem.
 */
void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println("RX: iniciando protocolo confiável");

  if (!driver.init()) {
    Serial.println("RX: falha ao iniciar RH_ASK");
    while (true) delay(1000);
  }

  clearAssemblyBuffer();
  Serial.println("RX: pronto");
}

/*
 * Função: loop
 * Objetivo: Loop de execução contínua no receptor.
 * Funcionamento: Fica monitorando constantemente o driver de rádio para ver se algum pacote chegou.
 * Se um pacote bruto for recebido, a função decodifica-o e, caso não apresente problemas de integridade (FCS),
 * encaminha-o para a função que processa os quadros do protocolo de parada-e-espera.
 */
void loop() {
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
