# Detecção de Erros e Assinaturas Digitais (FCS)

Para garantir a integridade dos dados transmitidos contra ruídos eletromagnéticos e interferências no canal físico, o protocolo utiliza uma **"assinatura digital"** de verificação anexada ao final de cada quadro (o campo FCS). Se a assinatura calculada localmente pelo receptor não for idêntica à assinatura que acompanha o quadro, o pacote é descartado sem o envio de ACK.

O projeto suporta dois tipos distintos de assinaturas digitais de validação, selecionáveis em tempo de compilação:

---

## 1. Checksum de 16 Bits (`USE_CRC16 = false`)

O Checksum funciona somando sequencialmente todos os bytes do cabeçalho e payload. Caso a soma exceda o limite de 16 bits, o carry é jogado de volta no bit menos significativo (soma de complemento de um). O resultado é negado bit a bit (complemento de um do total).

*   **Conceito didático:** Funciona como uma "soma de verificação" simplificada. É ótimo para detecção de erros básicos de transmissão, mas falha em detectar erros simétricos (como a troca de posição de dois bytes ou inversões de bits que se compensam mutuamente na soma).
*   **Vantagem:** Muito leve para processar computacionalmente.
*   **Desvantagem:** Vulnerabilidade a padrões de erro específicos.

```cpp
// Implementação do Checksum de 16 bits no código
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
```

---

## 2. CRC-16-CCITT (`USE_CRC16 = true`) [Máxima Pontuação - 3 pts]

O CRC (Cyclic Redundancy Check) trata os dados do pacote como coeficientes de um polinômio binário e realiza divisões matemáticas sucessivas usando lógica XOR com o polinômio gerador padrão **$X^{16} + X^{12} + X^5 + 1$ (representado pelo valor hexadecimal `0x1021`)**.

*   **Conceito didático:** Funciona como uma assinatura matemática de alta integridade. Qualquer alteração de bit durante a transmissão altera de forma pseudo-aleatória o resto da divisão polinomial, tornando impossível que erros de rajada passem despercebidos.
*   **Vantagem:** Altíssima confiabilidade. Garante a detecção de praticamente 100% de erros isolados, duplos, erros de quantidade ímpar de bits e erros em rajada menores ou iguais a 16 bits.
*   **Desvantagem:** Levemente mais complexo computacionalmente (embora irrelevante para a capacidade de processamento do ESP32).

```cpp
// Implementação do CRC-16 CCITT no código
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
```

---

## Como alternar entre CRC-16 e Checksum (Switch de Assinatura)

A escolha da assinatura de validação é definida no início de ambos os arquivos do projeto através da diretiva de compilação `USE_CRC16`. Siga os passos abaixo para alternar os modos:

1. Abra os arquivos `rf_tx_test.ino` (Transmissor) e `rf_rx_test.ino` (Receptor) na sua IDE.
2. Localize a linha que declara a constante `USE_CRC16` (próxima à linha 8 em ambos os arquivos).
3. Modifique o valor conforme desejado:
   * **Para usar CRC-16-CCITT (Padrão):**
     ```cpp
     #define USE_CRC16 true
     ```
   * **Para usar Checksum de 16 Bits:**
     ```cpp
     #define USE_CRC16 false
     ```
4. > [!IMPORTANT]
   > **A diretiva `USE_CRC16` deve conter exatamente o mesmo valor nos códigos do transmissor e do receptor.** Caso contrário, as assinaturas digitais geradas serão incompatíveis e todos os quadros serão descartados pelo receptor como se estivessem corrompidos.
5. Realize o upload do código atualizado para ambas as placas ESP32.
