# Clima_ESP32

Projeto para ESP32 com display OLED SSD1306 de 0,96" que exibe o clima atual do Rio de Janeiro e mantém um relógio em tempo real via NTP. Um botão permite alternar entre a tela de clima e um relógio com fonte grande.

## Funcionalidades

- Consulta do clima atual na API Open-Meteo (sem chave de API).
- Atualização automática do clima a cada 10 minutos.
- Hora sincronizada via NTP, com atualização a cada segundo.
- Botão no GPIO 4 para alternar entre:
  - modo clima;
  - modo relógio grande (`HH:MM`).
- Debounce de 50 ms no botão.
- Reconexão de Wi-Fi e nova tentativa em caso de falha na API.
- Mensagens de erro no display e logs no Monitor Serial.

## Hardware

- 1 placa ESP32 (DevKit com Wi-Fi).
- 1 display OLED SSD1306 128x64, I2C, 0,96".
- 1 botão tátil momentâneo.
- Jumpers.

### Ligação do display

| Display | ESP32 |
|---|---|
| VCC | 3V3 |
| GND | GND |
| SDA | GPIO 21 |
| SCL | GPIO 22 |

O firmware usa o endereço I2C `0x3C`. Alguns módulos usam `0x3D`; nesse caso, ajuste `ENDERECO_OLED`.

### Ligação do botão

| Botão | ESP32 |
|---|---|
| Uma perna | GPIO 4 |
| Perna diagonal | GND |

O botão usa o pull-up interno do ESP32 (`INPUT_PULLUP`), portanto não precisa de resistor externo. Em botões táteis de 4 pinos, as pernas do mesmo lado são interligadas internamente; usar pernas diagonais garante que os dois contatos elétricos sejam usados.

Com o botão solto, o GPIO 4 fica em `HIGH`. Quando pressionado, fica em `LOW` e o firmware alterna o modo de visualização.

## Bibliotecas

Instale pela Library Manager da Arduino IDE:

- **Adafruit GFX Library**
- **Adafruit SSD1306**
- **ArduinoJson** (testado com a versão 7.4.3)

Também é necessário o pacote de placas **ESP32 by Espressif Systems**.

As bibliotecas `WiFi`, `WiFiClientSecure`, `HTTPClient`, `Wire` e `time.h` já fazem parte do core do ESP32.

## Configuração

No arquivo `Clima_ESP32.ino`, preencha as credenciais da rede:

```cpp
const char* WIFI_SSID = "SUA_REDE_WIFI";
const char* WIFI_PASS = "SUA_SENHA_WIFI";
```

Principais parâmetros:

| Constante | Valor atual | Descrição |
|---|---|---|
| `LATITUDE` | `-22.9068` | Latitude do Rio de Janeiro |
| `LONGITUDE` | `-43.1729` | Longitude do Rio de Janeiro |
| `FUSO_HORARIO` | `America%2FSao_Paulo` | Fuso usado na consulta da API |
| `INTERVALO_ATUALIZACAO` | 10 minutos | Intervalo entre consultas de clima |
| `INTERVALO_REPETICAO` | 60 segundos | Nova tentativa após falha |
| `INTERVALO_RELOGIO` | 1 segundo | Atualização do relógio na tela |
| `TEMPO_DEBOUNCE` | 50 ms | Debounce do botão |
| `PINO_BOTAO` | GPIO 4 | Pino do botão |

O NTP usa o fuso `<-03>3` (UTC-3), configurado em `sincronizarHora()`.

## Como funciona

### Inicialização

1. Configura o Monitor Serial em `115200`.
2. Inicializa o barramento I2C e o display OLED.
3. Configura o botão como entrada com pull-up.
4. Conecta ao Wi-Fi.
5. Sincroniza a hora via NTP.
6. Faz a primeira consulta de clima.

### Loop principal

- Verifica o botão com debounce.
- A cada 1 segundo:
  - no modo clima, atualiza apenas a linha do relógio;
  - no modo relógio, redesenha `HH:MM` em fonte grande.
- A cada 10 minutos, consulta novamente a Open-Meteo.
- Em caso de falha, tenta de novo em 60 segundos.

### Modo clima

Mostra:

```
Rio de Janeiro
28.9C
Parc. nublado
Sens 34.0C  Umi 75%
Vento 15.7 km/h
Atual 13:45:30
```

A hora da última linha vem do NTP e atualiza a cada segundo.

### Modo relógio

Mostra somente `HH:MM` com fonte tamanho 4, centralizado no display. Se o NTP ainda não sincronizou, mostra `--:--`.

## API Open-Meteo

Endpoint utilizado:

```text
https://api.open-meteo.com/v1/forecast
  ?latitude=-22.9068
  &longitude=-43.1729
  &current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m
  &timezone=America%2FSao_Paulo
```

Campos consumidos:

| Campo | Uso |
|---|---|
| `temperature_2m` | Temperatura atual |
| `relative_humidity_2m` | Umidade relativa |
| `apparent_temperature` | Sensação térmica |
| `weather_code` | Código WMO da condição |
| `wind_speed_10m` | Velocidade do vento |

A API não exige chave. O horário exibido no display não vem da API: ele é obtido pelo NTP.

## Códigos WMO

| Código | Condição exibida |
|---|---|
| 0 | Céu limpo |
| 1 | Predominantemente limpo |
| 2 | Parcialmente nublado |
| 3 | Nublado |
| 45, 48 | Nevoeiro |
| 51, 53, 55 | Garoa |
| 56, 57 | Garoa gelada |
| 61 | Chuva fraca |
| 63 | Chuva moderada |
| 65 | Chuva forte |
| 66, 67 | Chuva gelada |
| 71 | Neve fraca |
| 73 | Neve moderada |
| 75 | Neve forte |
| 77 | Grãos de neve |
| 80 | Pancadas fracas |
| 81 | Pancadas moderadas |
| 82 | Pancadas fortes |
| 85, 86 | Pancadas de neve |
| 95 | Tempestade |
| 96, 99 | Tempestade com granizo |

No código, os textos são escritos sem acentos para evitar caracteres incorretos na fonte padrão do display.

## Compilação e upload

1. Abra `Clima_ESP32.ino` na Arduino IDE.
2. Selecione a placa **ESP32 Dev Module**.
3. Selecione a porta correta.
4. Compile e faça o upload.
5. Abra o Monitor Serial em `115200`.

Compilação de referência para ESP32 Dev Module:

- Flash: aproximadamente 1.082.924 bytes (82%).
- RAM global: aproximadamente 50.928 bytes (15%).

Os valores podem variar conforme a versão do core ESP32 e das bibliotecas.

## Diagnóstico

- **Display apagado:** confira VCC em 3,3 V, GND, SDA no GPIO 21 e SCL no GPIO 22. Se necessário, use um scanner I2C para confirmar o endereço `0x3C` ou `0x3D`.
- **Sem Wi-Fi:** confira SSID, senha e se a rede é de 2,4 GHz.
- **Hora `--:--`:** o NTP não sincronizou. Verifique se a rede permite UDP na porta 123.
- **Erro na API:** veja no Monitor Serial o código HTTP ou o erro de JSON.
- **Botão não alterna:** confirme a ligação no GPIO 4 e no GND e se o botão está realmente fechando o contato quando pressionado.

## Limitações

- O primeiro boot precisa de Wi-Fi para sincronizar o NTP e buscar o clima.
- Depois de sincronizado, o RTC interno continua contando mesmo sem Wi-Fi.
- Após perder a alimentação, a hora precisa ser sincronizada novamente.
- A conexão HTTPS usa `setInsecure()`, ou seja, não valida o certificado do servidor.
- O modo de visualização não é salvo; após reiniciar, o sistema volta ao modo clima.
- A Open-Meteo é gratuita para uso não comercial, mas está sujeita aos termos do serviço.

## Segurança

Não versionar SSID e senha reais. Mantenha os placeholders no código ou use um arquivo de configuração local que não seja enviado ao repositório. Se credenciais reais já foram expostas, troque a senha da rede.

## Melhorias futuras

- Salvar o modo de visualização na NVS/Preferences.
- Usar fonte customizada ou sete segmentos no modo relógio.
- Adicionar ícones de clima.
- Exibir previsão de mínima/máxima e probabilidade de chuva.
- Adicionar RTC DS3231 para manter a hora sem Wi-Fi desde o boot.
- Implementar atualização OTA.
- Usar deep sleep para reduzir consumo.

## Estrutura do projeto

```text
Clima_ESP32/
├── Clima_ESP32.ino
└── README.md
```
