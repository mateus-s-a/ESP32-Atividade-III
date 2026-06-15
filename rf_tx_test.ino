#include <RH_ASK.h>
#include <SPI.h>

#define RF_BITRATE 500
#define RF_RX_PIN 26
#define RF_TX_PIN 25

#define USE_CRC16 true          // true = CRC16-CCITT / false = checksum 16-bit

const uint8_t FRAME_MAGIC = 0xA5;
const uint8_t TYPE_DATA   = 0x01;
const uint8_t TYPE_ACK    = 0x02;
const uint8_t TYPE_END    = 0x03;

const uint8_t MAX_PAYLOAD = 24;
const uint16_t ACK_TIMEOUT_MS = 2500;
const uint8_t MAX_RETRIES = 6;
const uint8_t ERROR_EVERY_N_DATA_FRAMES = 4;

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

uint8_t nextSeq = 0;
bool injectErrors = false;
bool autoSendCounter = true;  // envia contador automaticamente se não houver texto no Serial
uint32_t dataFrameCounter = 0;
int contador = 1;
unsigned long lastAutoSend = 0;

/*
 * Função: checksum16
 * Objetivo: Calcula uma soma de verificação (checksum) simples de 16 bits sobre um conjunto de dados.
 * Funcionamento: Ela soma todos os bytes do vetor. Se a soma passar de 16 bits (estouro),
 * os bits extras (carregamento/carry) são somados de volta no final para garantir que o resultado
 * caiba em 16 bits. Por fim, retorna o complemento de um da soma.
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
 * Funcionamento: O CRC é um método de detecção de erros muito mais robusto que o checksum simples.
 * Ele processa os dados bit a bit como se estivesse realizando uma divisão polinomial binária. A cada bit,
 * realiza operações de deslocamento de bits (shift) e lógica XOR baseadas no polinômio gerador.
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
 * Objetivo: Calcula a FCS (Frame Check Sequence / Sequência de Verificação de Quadro).
 * Funcionamento: Essa função serve como um seletor. Dependendo da configuração da diretiva
 * 'USE_CRC16', ela decide se o código de detecção de erros será calculado usando o algoritmo CRC16 (mais seguro)
 * ou o Checksum de 16 bits (mais simples e rápido).
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
 * Funcionamento: Recebe o identificador do tipo do pacote (como TYPE_DATA, TYPE_ACK) e 
 * retorna uma string descritiva correspondente ("DATA", "ACK", "END", "UNK"). Isso facilita muito na
 * impressão das mensagens de depuração no Serial Monitor.
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
 * Objetivo: Constrói (empacota) o quadro completo de transmissão RF com o cabeçalho e código de erro.
 * Funcionamento: Ela monta a estrutura padrão do protocolo:
 *   [0] Byte Mágico de início (FRAME_MAGIC)
 *   [1] Tipo de Quadro (type)
 *   [2] Número de Sequência (seq)
 *   [3] Tamanho do Payload (len)
 *   [4...] Os dados úteis em si (payload)
 *   [FCS] Dois bytes de verificação de erros no final.
 * Parâmetros:
 *   - type: Tipo do quadro.
 *   - seq: Número de sequência (0 ou 1).
 *   - payload: Os dados úteis a serem enviados.
 *   - len: Tamanho dos dados úteis.
 *   - out: Vetor de destino onde o quadro montado será gravado.
 * Retorno: Tamanho total do quadro gerado (em bytes).
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
 * Objetivo: Desempacota e valida um quadro de dados recebido por RF.
 * Funcionamento: Realiza testes de sanidade no pacote: checa tamanho mínimo, valida o byte mágico,
 * verifica se o tamanho do payload é condizente, calcula o FCS dos dados recebidos e compara com o FCS que veio no quadro.
 * Se o FCS for igual, os dados estão intactos e a estrutura DecodedFrame é preenchida.
 * Parâmetros:
 *   - raw: Vetor com os bytes brutos recebidos.
 *   - rawLen: Tamanho do vetor bruto recebido.
 *   - frame: Referência para a estrutura de dados onde o quadro decodificado será armazenado.
 * Retorno: Status da decodificação (FRAME_OK, FRAME_BAD_FCS, etc.).
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
 * Função: waitForAck
 * Objetivo: Aguarda o recebimento do quadro de confirmação (ACK) para uma sequência específica.
 * Funcionamento: Entra em loop por um tempo definido (ACK_TIMEOUT_MS). Fica lendo a recepção RF.
 * Se decodificar um pacote válido do tipo ACK com o número de sequência correto, confirma o recebimento.
 * Se receber um quadro corrompido ou de outra sequência, o programa reporta e continua aguardando até dar timeout.
 * Parâmetros:
 *   - wantedSeq: O número de sequência (0 ou 1) que o transmissor está esperando confirmação.
 * Retorno: True se o ACK foi recebido com sucesso no prazo; False caso contrário (timeout).
 */
bool waitForAck(uint8_t wantedSeq) {
  unsigned long start = millis();

  while (millis() - start < ACK_TIMEOUT_MS) {
    uint8_t raw[64];
    uint8_t rawLen = sizeof(raw);

    if (driver.recv(raw, &rawLen)) {
      DecodedFrame frame;
      ParseStatus st = decodeFrame(raw, rawLen, frame);

      if (st == FRAME_BAD_FCS) {
        Serial.println("TX: quadro corrompido recebido enquanto aguardava ACK");
        continue;
      }

      if (st != FRAME_OK) {
        continue;
      }

      if (frame.type == TYPE_ACK && frame.seq == wantedSeq) {
        Serial.printf("TX: ACK confirmado para seq=%u\n", wantedSeq);
        return true;
      }

      Serial.printf("TX: recebeu quadro %s seq=%u enquanto aguardava ACK=%u\n",
                    typeToStr(frame.type), frame.seq, wantedSeq);
    }

    delay(10);
  }

  return false;
}

/*
 * Função: maybeCorruptFirstAttempt
 * Objetivo: Introduz intencionalmente um erro de bit no quadro para simular ruídos no canal RF (apenas na 1ª tentativa).
 * Funcionamento: É uma função de teste. Se a injeção de erros estiver ativa, ela altera um bit do payload
 * ou do FCS (usando a operação XOR) simulando uma interferência real do ambiente na primeira tentativa de envio.
 * Parâmetros:
 *   - frame: Ponteiro para o buffer do quadro.
 *   - frameLen: Tamanho do quadro em bytes.
 *   - shouldCorrupt: Flag indicando se a simulação de corrupção deve ser executada.
 */
void maybeCorruptFirstAttempt(uint8_t* frame, uint8_t frameLen, bool shouldCorrupt) {
  if (!shouldCorrupt) return;

  if (frameLen > 6) {
    frame[4] ^= 0x5A;   // corrompe 1 byte do payload depois de o FCS já ter sido calculado
  } else {
    frame[frameLen - 2] ^= 0xFF; // se não tiver payload, corrompe FCS
  }
}

/*
 * Função: sendStopAndWaitFrame
 * Objetivo: Envia um único quadro e gerencia retransmissões usando a técnica Stop-and-Wait.
 * Funcionamento: Ela envia o pacote pelo transmissor de RF e espera pela confirmação (ACK).
 * Se o ACK não chegar dentro do tempo limite, ela retransmite o mesmo quadro, fazendo isso até um máximo de MAX_RETRIES vezes.
 * Também controla a simulação de erros nas transmissões de DATA.
 * Parâmetros:
 *   - type: Tipo do quadro a enviar (ex: DATA, END).
 *   - payload: Ponteiro para os dados a serem transmitidos.
 *   - len: Tamanho dos dados.
 * Retorno: True se o quadro foi enviado e confirmado com sucesso; False se esgotaram as tentativas (falha).
 */
bool sendStopAndWaitFrame(uint8_t type, const uint8_t* payload, uint8_t len) {
  uint8_t pristine[64];
  uint8_t frameLen = buildFrame(type, nextSeq, payload, len, pristine);

  bool corruptThisFrame = false;
  if (type == TYPE_DATA) {
    dataFrameCounter++;
    corruptThisFrame = injectErrors && ((dataFrameCounter % ERROR_EVERY_N_DATA_FRAMES) == 0);
  }

  for (uint8_t attempt = 1; attempt <= MAX_RETRIES; attempt++) {
    uint8_t txBuf[64];
    memcpy(txBuf, pristine, frameLen);

    if (attempt == 1 && corruptThisFrame) {
      maybeCorruptFirstAttempt(txBuf, frameLen, true);
      Serial.println("TX: erro proposital injetado neste DATA");
    }

    driver.send(txBuf, frameLen);
    driver.waitPacketSent();

    Serial.printf("TX: enviou %s seq=%u len=%u tentativa=%u\n",
                  typeToStr(type), nextSeq, len, attempt);

    if (waitForAck(nextSeq)) {
      nextSeq ^= 1;
      return true;
    }

    Serial.println("TX: timeout de ACK, retransmitindo...");
    delay(40);
  }

  Serial.printf("TX: falha definitiva no quadro %s seq=%u\n", typeToStr(type), nextSeq);
  return false;
}

/*
 * Função: sendBufferReliable
 * Objetivo: Transmite um bloco de dados grande de forma confiável, dividindo-o em vários quadros se necessário.
 * Funcionamento: Como o transmissor possui um limite físico de tamanho por quadro (MAX_PAYLOAD),
 * essa função fragmenta o bloco de dados original em vários blocos menores (chunks), envia cada um como um quadro
 * de DATA (esperando confirmação para cada um) e, após enviar tudo, envia um quadro de fim (END) para concluir.
 * Parâmetros:
 *   - data: Ponteiro para o bloco de dados original.
 *   - totalLen: Tamanho total do bloco de dados.
 * Retorno: True se todos os pedaços e o fim foram transmitidos e confirmados; False se algum falhou.
 */
bool sendBufferReliable(const uint8_t* data, size_t totalLen) {
  size_t offset = 0;

  while (offset < totalLen) {
    uint8_t chunk = (totalLen - offset > MAX_PAYLOAD) ? MAX_PAYLOAD : (uint8_t)(totalLen - offset);

    if (!sendStopAndWaitFrame(TYPE_DATA, data + offset, chunk)) {
      return false;
    }

    offset += chunk;
  }

  if (!sendStopAndWaitFrame(TYPE_END, nullptr, 0)) {
    return false;
  }

  return true;
}

/*
 * Função: sendTextMessage
 * Objetivo: Envia uma string de texto de forma legível e estruturada.
 * Funcionamento: Recebe uma String, exibe mensagens informativas na saída serial para fins
 * de depuração e repassa os caracteres para a função 'sendBufferReliable'.
 * Parâmetros:
 *   - text: A mensagem de texto a ser enviada.
 * Retorno: True se o envio completo foi bem-sucedido; False se falhou.
 */
bool sendTextMessage(const String& text) {
  Serial.print("TX: iniciando envio confiável de: ");
  Serial.println(text);

  bool ok = sendBufferReliable((const uint8_t*)text.c_str(), text.length());

  if (ok) Serial.println("TX: mensagem concluída com sucesso");
  else    Serial.println("TX: mensagem falhou");

  return ok;
}

/*
 * Função: handleSerialCommands
 * Objetivo: Monitora e processa os comandos digitados pelo usuário no Serial Monitor do Arduino IDE.
 * Funcionamento: Fica checando se há dados no buffer serial do Arduino. Ao ler uma linha digitada,
 * ela verifica se o comando corresponde a comandos especiais do sistema (para ligar/desligar a injeção de erro,
 * ou ligar/desligar o envio automático do contador). Se não for um comando especial, ela envia a linha digitada
 * como uma mensagem de texto de RF convencional.
 */
void handleSerialCommands() {
  if (!Serial.available()) return;

  String line = Serial.readStringUntil('\n');
  line.trim();

  if (line.length() == 0) return;

  if (line.equalsIgnoreCase("ERR ON")) {
    injectErrors = true;
    Serial.println("TX: injecao de erro = ATIVADA");
    return;
  }

  if (line.equalsIgnoreCase("ERR OFF")) {
    injectErrors = false;
    Serial.println("TX: injecao de erro = DESATIVADA");
    return;
  }

  if (line.equalsIgnoreCase("COUNT ON") || line.equalsIgnoreCase("CNT ON")) {
    autoSendCounter = true;
    Serial.println("TX: envio automatico do contador = ATIVADO");
    return;
  }

  if (line.equalsIgnoreCase("COUNT OFF") || line.equalsIgnoreCase("CNT OFF")) {
    autoSendCounter = false;
    Serial.println("TX: envio automatico do contador = DESATIVADO");
    return;
  }

  sendTextMessage(line);
}

/*
 * Função: setup
 * Objetivo: Realiza as configurações e inicializações iniciais do hardware e software no microcontrolador.
 * Funcionamento: É a primeira função a rodar no Arduino. Ela inicializa a comunicação Serial
 * (115200 bps), inicia o driver de rádio frequência (RH_ASK) e exibe no Serial Monitor as informações de boas-vindas
 * e comandos disponíveis.
 */
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("TX: iniciando protocolo confiável");

  if (!driver.init()) {
    Serial.println("TX: falha ao iniciar RH_ASK");
    while (true) delay(1000);
  }

  Serial.println("TX: pronto");
  Serial.println("TX: digite um texto e pressione Enter para enviar");
  Serial.println("TX: comandos disponiveis -> ERR ON / ERR OFF / COUNT ON / COUNT OFF");
}

/*
 * Função: loop
 * Objetivo: Loop de execução principal do programa, que roda continuamente após a inicialização.
 * Funcionamento: Executa continuamente duas tarefas fundamentais:
 *   1. Chama a função de leitura de comandos no Serial Monitor.
 *   2. Caso o envio automático do contador esteja habilitado, verifica se passaram 3 segundos desde o
 *      último envio automático para enviar o valor incrementado do contador e reiniciar o temporizador.
 */
void loop() {
  handleSerialCommands();

  if (autoSendCounter && millis() - lastAutoSend >= 3000) {
    char msg[40];
    snprintf(msg, sizeof(msg), "contador=%d", contador++);
    sendTextMessage(String(msg));
    lastAutoSend = millis();
  }
}
