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

void sendAck(uint8_t seq) {
  delay(450);
  
  uint8_t ackBuf[16];
  uint8_t ackLen = buildFrame(TYPE_ACK, seq, nullptr, 0, ackBuf);

  driver.send(ackBuf, ackLen);
  driver.waitPacketSent();

  Serial.printf("RX: ACK enviado para seq=%u\n", seq);
}

void clearAssemblyBuffer() {
  rxMessageLen = 0;
}

void appendPayload(const uint8_t* data, uint8_t len) {
  if (rxMessageLen + len > RX_MESSAGE_BUFFER_SIZE) {
    Serial.println("RX: buffer da mensagem cheio, descartando montagem atual");
    clearAssemblyBuffer();
    return;
  }

  memcpy(rxMessage + rxMessageLen, data, len);
  rxMessageLen += len;
}

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
