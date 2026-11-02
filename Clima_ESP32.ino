#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <time.h>
#include "icones.h"

#if __has_include("config.h")
#include "config.h"
#else
#error "Crie config.h a partir de config.example.h"
#endif

#define LARGURA_TELA 128
#define ALTURA_TELA 64
#define ENDERECO_OLED 0x3C
#define PINO_SDA 21
#define PINO_SCL 22
#define PINO_BOTAO 4

const unsigned long TEMPO_DEBOUNCE = 50;
const unsigned long INTERVALO_RELOGIO = 1000;

const unsigned long INTERVALO_ATUALIZACAO = 10UL * 60UL * 1000UL;
const unsigned long INTERVALO_REPETICAO = 60UL * 1000UL;

Adafruit_SSD1306 display(LARGURA_TELA, ALTURA_TELA, &Wire, -1);

struct Clima {
  float temperatura;
  float sensacao;
  int umidade;
  float vento;
  int codigo;
};

enum ModoVisualizacao {
  MODO_CLIMA,
  MODO_RELOGIO
};

Clima clima;
ModoVisualizacao modoAtual = MODO_CLIMA;

bool climaValido = false;
bool ntpConfigurado = false;
bool estadoBotao = HIGH;
bool ultimaLeituraBotao = HIGH;
unsigned long tempoMudancaBotao = 0;
unsigned long proximaAtualizacao = 0;
unsigned long proximaAtualizacaoRelogio = 0;

String erroLinha1 = "Sem dados";
String erroLinha2 = "Aguardando clima";

void setup() {
  Serial.begin(115200);
  Wire.begin(PINO_SDA, PINO_SCL);

  if (!display.begin(SSD1306_SWITCHCAPVCC, ENDERECO_OLED)) {
    Serial.println("Falha ao iniciar o OLED");
    while (true) {
      delay(1000);
    }
  }

  pinMode(PINO_BOTAO, INPUT_PULLUP);
  estadoBotao = digitalRead(PINO_BOTAO);
  ultimaLeituraBotao = estadoBotao;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  mostrarMensagem("Conectando", "ao Wi-Fi...");
  conectarWiFi();

  if (WiFi.status() == WL_CONNECTED) {
    mostrarMensagem("Wi-Fi OK", "Sincronizando hora");
    sincronizarHora();
    mostrarMensagem("Hora atual", horaFormatada());
  } else {
    mostrarMensagem("Sem Wi-Fi", "Verifique os dados");
  }

  delay(1200);
}

void loop() {
  verificarBotao();

  if ((long)(millis() - proximaAtualizacaoRelogio) >= 0) {
    if (modoAtual == MODO_RELOGIO) {
      mostrarRelogioGrande();
    } else {
      atualizarRelogioTela();
    }
    proximaAtualizacaoRelogio = millis() + INTERVALO_RELOGIO;
  }

  if ((long)(millis() - proximaAtualizacao) >= 0) {
    bool sucesso = atualizarClima();
    proximaAtualizacao = millis() + (sucesso ? INTERVALO_ATUALIZACAO : INTERVALO_REPETICAO);
  }

  delay(10);
}

void conectarWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < 15000) {
    delay(250);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Wi-Fi conectado: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Falha ao conectar no Wi-Fi");
  }
}

bool atualizarClima() {
  if (WiFi.status() != WL_CONNECTED) {
    conectarWiFi();
  }

  if (WiFi.status() != WL_CONNECTED) {
    registrarErro("Sem Wi-Fi", "Tentando de novo");
    return false;
  }

  sincronizarHora();

  String url = API_CLIMA_URL;
  url += "?latitude=";
  url += String(API_CLIMA_LATITUDE, 4);
  url += "&longitude=";
  url += String(API_CLIMA_LONGITUDE, 4);
  url += "&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m";
  url += "&timezone=";
  url += API_CLIMA_TIMEZONE;

  WiFiClientSecure cliente;
  cliente.setInsecure();

  HTTPClient http;
  http.setConnectTimeout(10000);
  http.setTimeout(10000);

  if (!http.begin(cliente, url)) {
    Serial.println("Falha ao iniciar a conexao HTTP");
    registrarErro("Erro HTTP", "Tente novamente");
    return false;
  }

  int codigoHttp = http.GET();
  if (codigoHttp != HTTP_CODE_OK) {
    Serial.print("HTTP ");
    Serial.println(codigoHttp);
    http.end();
    registrarErro("Erro na API", "HTTP " + String(codigoHttp));
    return false;
  }

  String payload = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError erro = deserializeJson(doc, payload);
  if (erro) {
    Serial.print("Erro no JSON: ");
    Serial.println(erro.c_str());
    registrarErro("Erro no JSON", erro.c_str());
    return false;
  }

  JsonObject atual = doc["current"].as<JsonObject>();
  if (atual.isNull()) {
    Serial.println("Objeto 'current' ausente");
    registrarErro("Dados ausentes", "Sem 'current'");
    return false;
  }

  clima.temperatura = atual["temperature_2m"] | 0.0f;
  clima.sensacao = atual["apparent_temperature"] | 0.0f;
  clima.umidade = atual["relative_humidity_2m"] | 0;
  clima.vento = atual["wind_speed_10m"] | 0.0f;
  clima.codigo = atual["weather_code"] | -1;

  Serial.print("Temperatura: ");
  Serial.print(clima.temperatura, 1);
  Serial.print(" C | Sensacao: ");
  Serial.print(clima.sensacao, 1);
  Serial.print(" C | Umidade: ");
  Serial.print(clima.umidade);
  Serial.print("% | Vento: ");
  Serial.print(clima.vento, 1);
  Serial.print(" km/h | Codigo: ");
  Serial.print(clima.codigo);
  Serial.print(" | Hora NTP: ");
  Serial.println(horaFormatada());

  climaValido = true;
  if (modoAtual == MODO_CLIMA) {
    mostrarClima();
  }
  return true;
}

void mostrarClima() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  desenharIconeClima(112, 0);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Rio de Janeiro");

  display.setTextSize(2);
  display.setCursor(0, 10);
  display.print(clima.temperatura, 1);
  display.print("C");

  display.setTextSize(1);
  display.setCursor(0, 28);
  display.print(descricaoTempo(clima.codigo));

  display.setCursor(0, 38);
  display.print("Sens ");
  display.print(clima.sensacao, 1);
  display.print("C  Umi ");
  display.print(clima.umidade);
  display.print("%");

  display.setCursor(0, 48);
  display.print("Vento ");
  display.print(clima.vento, 1);
  display.print(" km/h");

  atualizarRelogioTela();
}

void mostrarMensagem(const String& linha1, const String& linha2) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 20);
  display.println(linha1);
  display.setCursor(0, 32);
  display.println(linha2);
  atualizarRelogioTela();
}

void sincronizarHora() {
  if (ntpConfigurado) {
    return;
  }

  configTzTime("<-03>3", "pool.ntp.org", "time.google.com");
  ntpConfigurado = true;

  unsigned long inicio = millis();
  while (time(nullptr) < 1700000000 && millis() - inicio < 10000) {
    delay(200);
  }

  if (time(nullptr) >= 1700000000) {
    Serial.println("Hora sincronizada via NTP");
  } else {
    Serial.println("NTP sem resposta; sincronizacao continua em segundo plano");
  }
}

String horaFormatada() {
  time_t agora = time(nullptr);
  if (agora < 1700000000) {
    return "--:--:--";
  }

  struct tm info;
  localtime_r(&agora, &info);

  char buffer[9];
  strftime(buffer, sizeof(buffer), "%H:%M:%S", &info);
  return String(buffer);
}

int horaAtual() {
  time_t agora = time(nullptr);
  if (agora < 1700000000) {
    return -1;
  }

  struct tm info;
  localtime_r(&agora, &info);
  return info.tm_hour;
}

bool horarioDiurno() {
  int hora = horaAtual();
  if (hora < 0) {
    return true;
  }
  return hora >= 6 && hora < 18;
}

const unsigned char* bitmapClima(int codigo, bool diurno) {
  switch (codigo) {
    case 0:
    case 1:
      return diurno ? icone_sol : icone_lua;
    case 2:
      return icone_parcial;
    case 3:
      return icone_nublado;
    case 45:
    case 48:
      return icone_nevoeiro;
    case 51:
    case 53:
    case 55:
    case 56:
    case 57:
      return icone_garoa;
    case 61:
    case 63:
    case 65:
    case 66:
    case 67:
    case 80:
    case 81:
    case 82:
      return icone_chuva;
    case 71:
    case 73:
    case 75:
    case 77:
    case 85:
    case 86:
      return icone_neve;
    case 95:
    case 96:
    case 99:
      return icone_tempestade;
    default:
      return icone_nublado;
  }
}

void desenharIconeClima(int x, int y) {
  const unsigned char* icone = bitmapClima(clima.codigo, horarioDiurno());
  display.drawBitmap(x, y, icone, ICONE_CLIMA_LARGURA, ICONE_CLIMA_ALTURA, SSD1306_WHITE);
}

void desenharIconeHorarioPequeno(int x, int y) {
  const unsigned char* icone = horarioDiurno() ? icone_sol_pequeno : icone_lua_pequena;
  display.drawBitmap(x, y, icone, ICONE_HORARIO_LARGURA, ICONE_HORARIO_ALTURA, SSD1306_WHITE);
}

void desenharIconeHorarioGrande(int x, int y) {
  const unsigned char* icone = horarioDiurno() ? icone_sol : icone_lua;
  display.drawBitmap(x, y, icone, ICONE_RELOGIO_LARGURA, ICONE_RELOGIO_ALTURA, SSD1306_WHITE);
}

void atualizarRelogioTela() {
  display.fillRect(0, 56, LARGURA_TELA, 8, SSD1306_BLACK);
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  if (horaAtual() >= 0) {
    desenharIconeHorarioPequeno(0, 56);
    display.setCursor(10, 56);
  } else {
    display.setCursor(0, 56);
  }

  display.print("Atual ");
  display.print(horaFormatada());
  display.display();
}

void mostrarRelogioGrande() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  if (horaAtual() >= 0) {
    desenharIconeHorarioGrande(56, 2);
  }

  display.setTextSize(4);
  String hora = horaFormatada();
  hora = hora.substring(0, 5);

  display.setCursor(4, 22);
  display.print(hora);
  display.display();
}

void desenharTelaAtual() {
  if (modoAtual == MODO_RELOGIO) {
    mostrarRelogioGrande();
  } else if (climaValido) {
    mostrarClima();
  } else {
    mostrarMensagem(erroLinha1, erroLinha2);
  }
}

void alternarModo() {
  modoAtual = (modoAtual == MODO_CLIMA) ? MODO_RELOGIO : MODO_CLIMA;
  Serial.print("Modo de visualizacao: ");
  Serial.println(modoAtual == MODO_CLIMA ? "clima" : "relogio");
  desenharTelaAtual();
}

void verificarBotao() {
  bool leitura = digitalRead(PINO_BOTAO);

  if (leitura != ultimaLeituraBotao) {
    tempoMudancaBotao = millis();
    ultimaLeituraBotao = leitura;
  }

  if (millis() - tempoMudancaBotao >= TEMPO_DEBOUNCE && leitura != estadoBotao) {
    estadoBotao = leitura;
    if (estadoBotao == LOW) {
      alternarModo();
    }
  }
}

void registrarErro(const String& linha1, const String& linha2) {
  erroLinha1 = linha1;
  erroLinha2 = linha2;
  climaValido = false;

  Serial.print("Erro: ");
  Serial.print(linha1);
  Serial.print(" - ");
  Serial.println(linha2);

  if (modoAtual == MODO_CLIMA) {
    mostrarMensagem(linha1, linha2);
  }
}

const char* descricaoTempo(int codigo) {
  switch (codigo) {
    case 0:
      return "Ceu limpo";
    case 1:
      return "Predom. limpo";
    case 2:
      return "Parc. nublado";
    case 3:
      return "Nublado";
    case 45:
    case 48:
      return "Nevoeiro";
    case 51:
    case 53:
    case 55:
      return "Garoa";
    case 56:
    case 57:
      return "Garoa gelada";
    case 61:
      return "Chuva fraca";
    case 63:
      return "Chuva moderada";
    case 65:
      return "Chuva forte";
    case 66:
    case 67:
      return "Chuva gelada";
    case 71:
      return "Neve fraca";
    case 73:
      return "Neve moderada";
    case 75:
      return "Neve forte";
    case 77:
      return "Graos de neve";
    case 80:
      return "Pancadas fracas";
    case 81:
      return "Pancadas moderadas";
    case 82:
      return "Pancadas fortes";
    case 85:
    case 86:
      return "Pancadas de neve";
    case 95:
      return "Tempestade";
    case 96:
    case 99:
      return "Tempestade c/ granizo";
    default:
      return "Condicao desconhecida";
  }
}
