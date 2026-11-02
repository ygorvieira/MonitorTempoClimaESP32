# Clima_ESP32

Projeto para ESP32 com display OLED SSD1306 de 0,96" que exibe o clima atual do Rio de Janeiro e mantém um relógio em tempo real via NTP. Um botão permite alternar entre a tela de clima e um relógio com fonte grande.

## Funcionalidades

- Consulta do clima atual na API Open-Meteo (sem chave de API).
- Atualização automática do clima a cada 10 minutos.
- Hora sincronizada via NTP, com atualização a cada segundo.
- Botão no GPIO 4 para alternar entre:
  - modo clima;
  - modo relógio grande (`HH:MM`).
- Ícones de clima conforme o código WMO.
- Ícone de sol entre 6:00 e 17:59 e lua nos demais horários.
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

As configurações de rede e da API ficam em `config.h`, que não é versionado.

1. Copie o arquivo de exemplo:
   ```bash
   cp config.example.h config.h
   ```
2. Edite `config.h` com os dados da sua rede e da API:
   ```cpp
   #define WIFI_SSID "SUA_REDE_WIFI"
   #define WIFI_PASS "SUA_SENHA_WIFI"

   #define API_CLIMA_URL "https://api.open-meteo.com/v1/forecast"
   #define API_CLIMA_LATITUDE -22.9068
   #define API_CLIMA_LONGITUDE -43.1729
   #define API_CLIMA_TIMEZONE "America%2FSao_Paulo"
   ```
3. Se `config.h` não existir, a compilação para com uma mensagem orientando a criar o arquivo a partir de `config.example.h`.

Principais parâmetros:

| Constante | Valor padrão | Descrição |
|---|---|---|
| `WIFI_SSID` | `SUA_REDE_WIFI` | Nome da rede Wi-Fi |
| `WIFI_PASS` | `SUA_SENHA_WIFI` | Senha da rede Wi-Fi |
| `API_CLIMA_URL` | `https://api.open-meteo.com/v1/forecast` | URL base da Open-Meteo |
| `API_CLIMA_LATITUDE` | `-22.9068` | Latitude do Rio de Janeiro |
| `API_CLIMA_LONGITUDE` | `-43.1729` | Longitude do Rio de Janeiro |
| `API_CLIMA_TIMEZONE` | `America%2FSao_Paulo` | Fuso usado na consulta da API |
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

A hora da última linha vem do NTP e atualiza a cada segundo. O ícone de clima aparece no canto superior direito (16×16) e o ícone de horário no início do rodapé (8×8). Para céu limpo, o ícone de clima é sol durante o dia e lua à noite.

### Modo relógio

Mostra um ícone de sol/lua centralizado no topo e `HH:MM` com fonte tamanho 4 logo abaixo. Se o NTP ainda não sincronizou, mostra `--:--` e omite o ícone.

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

## Ícones

Os bitmaps monocromáticos ficam em `icones.h` e são desenhados com `display.drawXBitmap()`, sem biblioteca adicional.

| Condição | Ícone | Códigos WMO |
|---|---|---|
| Céu limpo | Sol de dia / lua à noite | 0, 1 |
| Parcialmente nublado | Sol com nuvem | 2 |
| Nublado | Nuvem | 3 |
| Nevoeiro | Névoa | 45, 48 |
| Garoa | Nuvem com gotas | 51–57 |
| Chuva | Nuvem com chuva | 61–67, 80–82 |
| Neve | Nuvem com flocos | 71–77, 85, 86 |
| Tempestade | Nuvem com raio | 95–99 |

Ícone de horário:

| Horário | Ícone |
|---|---|
| 6:00–17:59 | Sol |
| 18:00–5:59 | Lua |

Tamanhos usados:

- Ícone de clima: 16×16, no canto superior direito do modo clima.
- Ícone de horário no modo clima: 8×8, no início do rodapé.
- Ícone de horário no modo relógio: 16×16, centralizado acima de `HH:MM`.

Sem NTP sincronizado, o ícone de horário é omitido e o ícone de céu limpo usa sol como fallback.

## Pré-visualização no PC

Os ícones e as telas podem ser pré-visualizados sem gravar no ESP32:

```bash
python3 tools/preview_icones.py
```

O script lê `icones.h`, interpreta os bitmaps em LSB-first (o mesmo formato usado por `drawXBitmap()`) e gera em `preview/`:

- `icones.png`: contato com todos os ícones ampliados e rotulados;
- `tela_clima_dia.png` e `tela_clima_noite.png`: mockups do modo clima;
- `tela_relogio_dia.png` e `tela_relogio_noite.png`: mockups do modo relógio;
- `telas.png`: composição com as quatro telas.

Requisitos: Python 3 e Pillow. Se necessário:

```bash
pip install pillow
```

Os ícones são renderizados exatamente como no display; os textos dos mockups são aproximados com uma fonte monoespaçada do sistema. A pasta `preview/` está no `.gitignore`.

## Compilação e upload

1. Abra `Clima_ESP32.ino` na Arduino IDE.
2. Selecione a placa **ESP32 Dev Module**.
3. Selecione a porta correta.
4. Compile e faça o upload.
5. Abra o Monitor Serial em `115200`.

Compilação de referência para ESP32 Dev Module:

- Flash: aproximadamente 1.084.088 bytes (82%).
- RAM global: aproximadamente 50.912 bytes (15%).

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

O arquivo `config.h` está listado no `.gitignore` e não deve ser versionado. Mantenha apenas placeholders em `config.example.h`. Se credenciais reais já foram expostas em algum commit, troque a senha da rede.

As credenciais ficam embutidas no firmware em texto simples. O `.gitignore` evita exposição no repositório, mas não impede a extração por acesso físico ao ESP32.

## Melhorias futuras

- Salvar o modo de visualização na NVS/Preferences.
- Usar fonte customizada ou sete segmentos no modo relógio.
- Adicionar ícones animados ou novas condições de clima.
- Exibir previsão de mínima/máxima e probabilidade de chuva.
- Adicionar RTC DS3231 para manter a hora sem Wi-Fi desde o boot.
- Implementar atualização OTA.
- Usar deep sleep para reduzir consumo.

## Estrutura do projeto

```text
Clima_ESP32/
├── .gitignore
├── Clima_ESP32.ino
├── config.example.h
├── config.h          (local, ignorado pelo Git)
├── icones.h
├── preview/          (gerado, ignorado pelo Git)
├── README.md
└── tools/
    └── preview_icones.py
```
