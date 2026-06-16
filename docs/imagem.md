# Codificação e Transmissão de Imagens Dinâmicas em Alta Resolução

Este documento descreve a arquitetura, o algoritmo e a especificação do sistema de transmissão e exibição de imagens desenvolvido para o projeto de comunicação via rádio. O sistema permite exibir representações gráficas detalhadas de até 100x32 pixels em um display LCD alfanumérico convencional de 20x4 caracteres, utilizando técnicas de quantização vetorial e atualização dinâmica da memória do display (CGRAM).

---

## 1. O Desafio Técnico do Display Alfanumérico

Os displays LCD alfanuméricos baseados no controlador HD44780 (como o modelo 20x4 utilizado no projeto) são projetados primariamente para exibir texto. Eles organizam a tela em uma grade de caracteres predefinidos. Exibições gráficas diretas são limitadas pelas seguintes restrições do hardware:
* Cada célula de caractere é composta por uma matriz física fixa de 5 pixels de largura por 8 pixels de altura.
* Há um limite físico de apenas 8 posições de memória na CGRAM (Character Generator RAM) destinadas a caracteres customizados pelo programador (índices de `0` a `7`).

Para contornar essa barreira e obter uma imagem contínua e rica em detalhes (como logotipos, desenhos e fontes customizadas), foi desenvolvido um método híbrido que combina a binarização de imagem com a **Quantização Vetorial Dinâmica** executada no computador e processada em tempo real no receptor.

---

## 2. Quantização Vetorial Dinâmica em Python

O processo inicia no computador de envio, por meio do script utilitário `scripts/image_encoder.py`. O algoritmo realiza as seguintes etapas:

### 2.1. Redimensionamento e Binarização
A imagem fornecida é convertida para escala de cinza e redimensionada para a resolução exata da grade de pixels do LCD:
$$\text{Largura} = 20 \text{ colunas} \times 5 \text{ pixels} = 100 \text{ pixels}$$
$$\text{Altura} = 4 \text{ linhas} \times 8 \text{ pixels} = 32 \text{ pixels}$$

Cada pixel resultante é comparado com um limiar de luminosidade (*threshold*) para gerar uma matriz binária de tamanho $100 \times 32$, onde o valor `1` indica pixel aceso e `0` indica pixel apagado.

### 2.2. Agrupamento e Contagem de Frequência
A matriz binária é dividida em 80 células, cada uma com dimensões de $5 \times 8$ pixels (correspondendo exatamente aos caracteres físicos da tela).
O script avalia cada célula para identificar sua geometria:
* Células totalmente apagadas (todos os 40 pixels em `0`) são associadas diretamente ao caractere padrão de espaço (`' '` ou `'.'`).
* Células totalmente acesas (todos os 40 pixels em `1`) são associadas ao caractere padrão de bloco sólido (`255` ou `'#'` na tabela do controlador).
* Células mistas (que possuem partes desenhadas) são convertidas para uma tupla de 8 bytes de 5 bits. O script contabiliza a ocorrência de cada padrão geométrico único nas 80 células.

### 2.3. Seleção dos Blocos de Construção (Building Blocks)
O script ordena os padrões mistos por ordem decrescente de frequência e seleciona os **8 padrões mais recorrentes**. Estes serão os caracteres customizados carregados dinamicamente na CGRAM do receptor. 

Se a imagem contiver mais de 8 geometrias mistas distintas, o script executa uma aproximação baseada na **Distância Hamming**: cada célula restante da imagem original é mapeada para o padrão selecionado que apresentar a menor quantidade de pixels divergentes.

---

## 3. Estrutura do Pacote de Dados de Imagem

Para transferir tanto os mapas de bits customizados quanto a posição deles na tela de 20x4, foi projetado um formato de pacote binário de tamanho variável transmitido como payload pelo protocolo Selective Repeat ARQ:

| Campo | Tamanho | Descrição |
|:---|:---|:---|
| **Assinatura (Header)** | 4 Bytes | Sequência de controle `[0x1B, 'I', 'M', 'G']` |
| **Quantidade de Caracteres ($N$)** | 1 Byte | Número de caracteres customizados gerados (de `0` a `8`) |
| **Bitmaps da CGRAM** | $8 \times N$ Bytes | Vetor contendo os 8 bytes de dados de linha de pixel para cada caractere |
| **Matriz da Tela (Layout)** | 80 Bytes | Mapeamento dos 80 caracteres a serem escritos nas posições da tela |

### Codificação Hexadecimal para Entrada Serial
Como os monitores seriais padrão convertem caracteres binários brutos (como `0x1B` ou `0x00`) em formatos inválidos, o codificador Python encapsula todo o pacote binário gerado em uma única representação textual hexadecimal, precedida da palavra-chave `IMG:`.

Exemplo de estrutura de saída gerada:
`IMG:1b494d47081f1f1f1f1f1f000000000000...`

---

## 4. Processamento nos Dispositivos Embarcados

### 4.1. Transmissão (`rf_tx_test.ino`)
O transmissor intercepta a entrada serial recebida do computador no laço de controle de comandos:
```cpp
if (line.startsWith("IMG:")) {
  String hexData = line.substring(4);
  hexData.trim();
  size_t binLen = hexData.length() / 2;
  uint8_t *binBuf = (uint8_t *)malloc(binLen);
  
  // Decodifica a string hexadecimal para o buffer binário
  for (size_t i = 0; i < binLen; i++) {
    binBuf[i] = (hexCharToVal(hexData.charAt(2 * i)) << 4) | hexCharToVal(hexData.charAt(2 * i + 1));
  }
  
  // Envia o buffer estruturado usando a janela confiável do Selective Repeat ARQ
  sendBufferReliable(binBuf, binLen);
  free(binBuf);
}
```

### 4.2. Recepção e Atualização da CGRAM (`rf_rx_test.ino`)
No receptor, ao remontar uma mensagem completa enviada sob o controle de enlace, a função de callback `onCompleteMessage()` inspeciona o início do payload. Se detectar a sequência `0x1B, 'I', 'M', 'G'`, o fluxo de imagem dinâmica é ativado:

```cpp
if (rxMessageLen >= 4 && rxMessage[0] == 0x1B && rxMessage[1] == 'I' && rxMessage[2] == 'M' && rxMessage[3] == 'G') {
  uint8_t numCustom = rxMessage[4];
  size_t headerSize = 5 + 8 * numCustom;
  
  // Carrega as novas formas dinamicamente nos canais da CGRAM
  for (uint8_t i = 0; i < numCustom && i < 8; i++) {
    uint8_t bitmap[8];
    memcpy(bitmap, rxMessage + 5 + i * 8, 8);
    lcd.createChar(i, bitmap);
  }
  
  // Limpa o visor e imprime as 4 linhas com mapeamento direcionado
  lcd.clear();
  const uint8_t *screenData = rxMessage + headerSize;
  
  for (int row = 0; row < 4; row++) {
    lcd.setCursor(0, row);
    for (int col = 0; col < 20; col++) {
      char c = screenData[row * 20 + col];
      if (c == '#') lcd.write(255); // Bloco aceso completo
      else if (c == '.') lcd.write(' '); // Bloco vazio
      else if (c >= '0' && c <= '7') lcd.write(c - '0'); // Registros da CGRAM
      else lcd.write(c);
    }
  }
}
```

---

## 5. Guia Prático de Operação e Testes

O diretório `images/` contém arquivos gráficos de teste preparados para demonstrar a eficiência do algoritmo e o comportamento sob diferentes densidades de traços.

### 5.1. Teste 1: Imagem Geométrica (`images/5968851.png`)
Esta imagem apresenta linhas curvas e formas ovais, ideal para demonstrar o agrupamento de cantos arredondados na CGRAM.

1. No terminal do computador, codifique o arquivo:
   ```bash
   python3 scripts/image_encoder.py images/5968851.png
   ```
2. Observe as duas pré-visualizações exibidas no terminal. O script listará a string hexadecimal gerada.
3. Copie a linha iniciada com `IMG:` e cole-a no Serial Monitor do transmissor.
4. No monitor serial do transmissor, acompanhe o fatiamento em blocos e o envio confiável.
5. No receptor, acompanhe a montagem dos blocos. Ao fim, o LCD exibirá o desenho correspondente com bordas contínuas e curvas suavizadas.

### 5.2. Teste 2: Imagem de Contraste Elevado (`images/badapple.jpg`)
Esta imagem possui alto contraste em silhueta (preto e branco puro), sendo uma excelente demonstração do algoritmo de aproximação geométrica em contornos contínuos.

1. Execute a codificação no terminal:
   ```bash
   python3 scripts/image_encoder.py images/badapple.jpg
   ```
2. Copie o comando hexadecimal resultante e transmita-o via monitor serial.
3. A imagem será desenhada no display LCD, evidenciando o uso inteligente das linhas inclinadas e cantos personalizados da CGRAM para representar a silhueta geométrica aproximada.
