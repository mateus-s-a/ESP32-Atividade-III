# Arquitetura de Hardware e Conexões

Esta seção descreve a montagem física do hardware, a pinagem utilizada para interligar os módulos RF e o LCD I2C às placas ESP32, e recomendações técnicas para otimizar o sinal.

## Materiais Utilizados
*   **2x** Placas de Desenvolvimento ESP32 (NodeMCU / DevKit).
*   **2x** Módulos RF 433 MHz (Ex: Par Transmissor MX-FS-03V e Receptor MX-05V).
*   **1x** Display LCD 16x2 com Módulo de Interface I2C (conectado ao Receptor).
*   **2x** Protoboards e cabos jumpers para conexões.

---

## Pinagem de Conexão (Fiação)

| Componente | Pino no ESP32 | Função / Descrição |
| :--- | :--- | :--- |
| **Transmissor (Data)** | **GPIO 25** | Saída digital para modulação do sinal de rádio |
| **Receptor (Data)** | **GPIO 26** | Entrada digital de dados vindos do rádio receptor |
| **Módulo I2C LCD (SDA)** | **GPIO 21** | Barramento de dados I2C (apenas no Receptor) |
| **Módulo I2C LCD (SCL)** | **GPIO 22** | Barramento de clock I2C (apenas no Receptor) |
| **VCC (Módulos)** | **5V / VIN** | Alimentação dos módulos RF e LCD (requer 5V estável) |
| **GND (Módulos)** | **GND** | Terra de referência comum |

---

## Recomendação de Antena (Critério de Distância)

> [!NOTE]
> Para obter uma distância estável acima de 1 metro (conforme os critérios de aceitação do projeto), é altamente recomendável soldar um fio rígido de cobre de **17.3 cm** (equivalente a um quarto de onda de 433 MHz) em cada uma das antenas dos módulos RF.
