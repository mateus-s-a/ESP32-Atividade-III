#include <RH_ASK.h>
#include <SPI.h>

#define RF_BITRATE 500
#define RF_RX_PIN 26
#define RF_TX_PIN 25

#define USE_CRC16 true          // true = CRC16-CCITT / false = checksum 16-bit
#define AUTO_SEND_COUNTER true  // envia contador automaticamente se não houver texto no Serial

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
uint32_t dataFrameCounter = 0;
int contador = 1;
unsigned long lastAutoSend = 0;

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

uint16_t calcFCS(const uint8_t* data, uint8_t len) {
  return USE_CRC16 ? crc16_ccitt(data, len) : checksum16(data, len);
}

const char* typeToStr(uint8_t type) {
  switch (type) {
    case TYPE_DATA: return "DATA";
    case TYPE_ACK:  return "ACK";
    case TYPE_END:  return "END";
    default:        return "UNK";
  }
}

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

void maybeCorruptFirstAttempt(uint8_t* frame, uint8_t frameLen, bool shouldCorrupt) {
  if (!shouldCorrupt) return;

  if (frameLen > 6) {
    frame[4] ^= 0x5A;   // corrompe 1 byte do payload depois de o FCS já ter sido calculado
  } else {
    frame[frameLen - 2] ^= 0xFF; // se não tiver payload, corrompe FCS
  }
}

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

bool sendTextMessage(const String& text) {
  Serial.print("TX: iniciando envio confiável de: ");
  Serial.println(text);

  bool ok = sendBufferReliable((const uint8_t*)text.c_str(), text.length());

  if (ok) Serial.println("TX: mensagem concluída com sucesso");
  else    Serial.println("TX: mensagem falhou");

  return ok;
}

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

  sendTextMessage(line);
}

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
  Serial.println("TX: comandos disponiveis -> ERR ON / ERR OFF");
}

void loop() {
  handleSerialCommands();

  if (AUTO_SEND_COUNTER && millis() - lastAutoSend >= 3000) {
    char msg[40];
    snprintf(msg, sizeof(msg), "contador=%d", contador++);
    sendTextMessage(String(msg));
    lastAutoSend = millis();
  }
}
