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

### Exemplo Prático de Cálculo do Checksum

Imagine que queremos calcular a assinatura de Checksum de 16 bits sobre um array contendo 4 bytes de cabeçalho: `[0xA5, 0x01, 0x00, 0x02]`.

1. **Soma dos bytes brutos:**
   $$S = \sum D_i = 0\text{xA5} + 0\text{x}01 + 0\text{x}00 + 0\text{x}02$$
   $$S = 165_{10} + 1_{10} + 0_{10} + 2_{10} = 168_{10} = 0\text{x}00\text{A8}$$

2. **Verificação de estouro (carry):**
   Como $S = 0\text{x}00\text{A8}$ é menor que $65535$ ($0\text{xFFFF}$), a soma cabe perfeitamente em 16 bits e nenhuma operação de carry precisa ser realizada no laço `while (sum >> 16)`.

3. **Aplicação do Complemento de Um (bitwise NOT):**
   A assinatura final é o complemento de um da soma de 16 bits:
   $$\text{Checksum} = \sim S = \sim 0\text{x}00\text{A8} = 0\text{xFF}57$$
   
   Em binário:
   $$\text{Soma} = 0000\,0000\,1010\,1000_2$$
   $$\text{Checksum} = 1111\,1111\,0101\,0111_2$$

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

### Exemplo Prático de Cálculo do CRC-16-CCITT

Para demonstrar a matemática por trás do algoritmo, vamos calcular o CRC-16-CCITT (polinômio $0\text{x}1021$) de um único byte de dados: `[0x01]`.

1. **Inicialização:**
   O registrador de CRC é iniciado com o valor padrão:
   $$\text{crc} = 0\text{xFFFF} = 1111\,1111\,1111\,1111_2$$

2. **Processamento do byte `0x01`:**
   O byte é deslocado em 8 bits para a esquerda e aplicado XOR com a parte alta do registrador:
   $$\text{crc} = \text{crc} \oplus (0\text{x}01 \ll 8) = 0\text{xFFFF} \oplus 0\text{x}0100 = 0\text{xFEFF}$$
   Em binário:
   $$\text{crc} = 1111\,1110\,1111\,1111_2$$

3. **Laço bit a bit (8 repetições):**
   * **Bit 1:** O bit mais significativo (MSB) é $1$. Deslocamos 1 bit para a esquerda e aplicamos XOR com o polinômio gerador $0\text{x}1021$:
     $$\text{crc} = (0\text{xFEFF} \ll 1) \oplus 0\text{x}1021 = 0\text{xFDFE} \oplus 0\text{x}1021 = 0\text{xEDDF}$$
     $$(1111\,1101\,1111\,1110_2) \oplus (0001\,0000\,0010\,0001_2) = 1110\,1101\,1101\,1111_2$$
   * **Bit 2:** O MSB é $1$. Deslocamos e aplicamos XOR:
     $$\text{crc} = (0\text{xEDDF} \ll 1) \oplus 0\text{x}1021 = 0\text{xDBBE} \oplus 0\text{x}1021 = 0\text{xCB9F}$$
     $$(1101\,1011\,1011\,1110_2) \oplus (0001\,0000\,0010\,0001_2) = 1100\,1011\,1009\,1111_2$$
   * **Bit 3:** O MSB é $1$. Deslocamos e aplicamos XOR:
     $$\text{crc} = (0\text{xCB9F} \ll 1) \oplus 0\text{x}1021 = 0\text{x973E} \oplus 0\text{x}1021 = 0\text{x871F}$$
     $$(1001\,0111\,0011\,1110_2) \oplus (0001\,0000\,0010\,0001_2) = 1000\,0111\,0001\,1111_2$$
   * **Bit 4:** O MSB é $1$. Deslocamos e aplicamos XOR:
     $$\text{crc} = (0\text{x871F} \ll 1) \oplus 0\text{x}1021 = 0\text{x0E3E} \oplus 0\text{x}1021 = 0\text{x1E1F}$$
     $$(0000\,1110\,0011\,1110_2) \oplus (0001\,0000\,0010\,0001_2) = 0001\,1110\,0001\,1111_2$$
   * **Bit 5:** O MSB é $0$. Apenas deslocamos para a esquerda (sem XOR):
     $$\text{crc} = (0\text{x1E1F} \ll 1) = 0\text{x3C3E} = 0011\,1100\,0011\,1110_2$$
   * **Bit 6:** O MSB é $0$. Apenas deslocamos para a esquerda (sem XOR):
     $$\text{crc} = (0\text{x3C3E} \ll 1) = 0\text{x787C} = 0111\,1000\,0111\,1100_2$$
   * **Bit 7:** O MSB é $0$. Apenas deslocamos para a esquerda (sem XOR):
     $$\text{crc} = (0\text{x787C} \ll 1) = 0\text{xF0F8} = 1111\,0000\,1111\,1000_2$$
   * **Bit 8:** O MSB é $1$. Deslocamos e aplicamos XOR:
     $$\text{crc} = (0\text{xF0F8} \ll 1) \oplus 0\text{x}1021 = 0\text{xE1F0} \oplus 0\text{x}1021 = 0\text{xF1D1}$$
     $$(1110\,0001\,1111\,0000_2) \oplus (0001\,0000\,0010\,0001_2) = 1111\,0001\,1101\,0001_2$$

Portanto, o CRC-16-CCITT final resultante para o byte `[0x01]` é **`0xF1D1`**.

---

### Comparação da Diferença de Cálculo

Ao comparar os dois métodos para os mesmos dados brutos `[0x01]`:
*   **Checksum de 16 bits**: Apenas soma o valor $1$ e aplica a negação lógica, resultando em `0xFFFE`. Qualquer alteração de ordem de bytes ou soma nula resultaria no mesmo valor.
*   **CRC-16-CCITT**: Aplica divisões de polinômios bit a bit, resultando em `0xF1D1`. Uma pequena mudança de um único bit geraria uma assinatura drasticamente diferente e não correlacionada, provando ser imensamente mais robusto contra ruídos.

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
