#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <StreamString.h>
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
#define PINO_DHT 5

const unsigned long TEMPO_DEBOUNCE = 50;
const unsigned long INTERVALO_RELOGIO = 1000;
const unsigned long INTERVALO_DHT = 2500;
const unsigned long VALIDADE_LEITURA_DHT = 30000;

const unsigned long INTERVALO_ATUALIZACAO = 10UL * 60UL * 1000UL;
const unsigned long INTERVALO_REPETICAO = 60UL * 1000UL;

const size_t TAMANHO_MAXIMO_RESPOSTA = 8UL * 1024UL;

const int DIAS_PREVISAO = 3;

Adafruit_SSD1306 display(LARGURA_TELA, ALTURA_TELA, &Wire, -1);
DHT dht(PINO_DHT, DHT11);

struct Clima {
  float temperatura;
  float sensacao;
  int umidade;
  float vento;
  int codigo;
};

struct PrevisaoDia {
  float minima;
  float maxima;
  int codigo;
  int chuva;
  char data[11];
};

struct LeituraLocal {
  float temperatura;
  float umidade;
  bool valida;
  unsigned long ultimaLeitura;
};

enum ModoVisualizacao {
  MODO_CLIMA,
  MODO_RELOGIO,
  MODO_PREVISAO
};

Clima clima;
PrevisaoDia previsao[DIAS_PREVISAO];
LeituraLocal leituraLocal = {0.0f, 0.0f, false, 0};
ModoVisualizacao modoAtual = MODO_CLIMA;

bool climaValido = false;
bool previsaoValida = false;
bool ntpConfigurado = false;
bool estadoBotao = HIGH;
bool ultimaLeituraBotao = HIGH;
unsigned long tempoMudancaBotao = 0;
unsigned long proximaAtualizacao = 0;
unsigned long proximaAtualizacaoRelogio = 0;
unsigned long proximaLeituraDHT = 0;

String erroLinha1 = "Sem dados";
String erroLinha2 = "Aguardando clima";

void setup() {
  Serial.begin(115200);
  Wire.begin(PINO_SDA, PINO_SCL);
  dht.begin();

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

  atualizarLeituraLocal();

  if (WiFi.status() == WL_CONNECTED) {
    mostrarMensagem("Wi-Fi OK", "Sincronizando hora");
    sincronizarHora();
    mostrarMensagem("Hora atual", horaFormatada());
  } else {
    erroLinha1 = "Sem Wi-Fi";
    erroLinha2 = "Verifique os dados";
    exibirTelaClima();
  }

  delay(1200);
}

void loop() {
  verificarBotao();

  if (atualizarLeituraLocal() && modoAtual == MODO_CLIMA && !climaValido) {
    exibirTelaClima();
  }

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

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setMinSecurity(WIFI_AUTH_WPA2_PSK);
  WiFi.setAutoReconnect(true);
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

class RespostaLimitadaStream : public StreamString {
 public:
  explicit RespostaLimitadaStream(size_t limite) : limite(limite) {}

  size_t write(const uint8_t* buffer, size_t tamanho) override {
    if (tamanho > 0 && (length() + tamanho) > limite) {
      excedeuLimite = true;
      return 0;
    }
    return StreamString::write(buffer, tamanho);
  }

  size_t write(uint8_t dado) override {
    if (length() >= limite) {
      excedeuLimite = true;
      return 0;
    }
    return StreamString::write(dado);
  }

  bool excedeu() const {
    return excedeuLimite;
  }

 private:
  size_t limite;
  bool excedeuLimite = false;
};

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
  url += "&daily=temperature_2m_min,temperature_2m_max,weather_code,precipitation_probability_max";
  url += "&forecast_days=";
  url += String(DIAS_PREVISAO);
  url += "&timezone=";
  url += API_CLIMA_TIMEZONE;

  WiFiClientSecure cliente;
  cliente.useBuiltinCACertBundle();
  cliente.setHandshakeTimeout(15);

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

  RespostaLimitadaStream resposta(TAMANHO_MAXIMO_RESPOSTA);
  int resultado = http.writeToStream(&resposta);
  http.end();

  if (resposta.excedeu()) {
    Serial.println("Resposta da API excedeu o limite");
    registrarErro("Erro na API", "Resposta grande");
    return false;
  }

  if (resultado < 0) {
    Serial.print("Falha ao ler a resposta HTTP: ");
    Serial.println(resultado);
    registrarErro("Erro na API", "Falha na leitura");
    return false;
  }

  String payload = resposta;

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

  float temperatura = atual["temperature_2m"] | NAN;
  float sensacao = atual["apparent_temperature"] | NAN;
  int umidade = atual["relative_humidity_2m"] | -1;
  float vento = atual["wind_speed_10m"] | NAN;
  int codigo = atual["weather_code"] | -1;

  bool dadosValidos =
      isfinite(temperatura) && temperatura >= -90.0f && temperatura <= 60.0f &&
      isfinite(sensacao) && sensacao >= -90.0f && sensacao <= 70.0f &&
      umidade >= 0 && umidade <= 100 &&
      isfinite(vento) && vento >= 0.0f && vento <= 500.0f &&
      codigo >= 0 && codigo <= 99;

  if (!dadosValidos) {
    Serial.println("Dados da API fora das faixas esperadas");
    registrarErro("Dados invalidos", "Resposta suspeita");
    return false;
  }

  clima.temperatura = temperatura;
  clima.sensacao = sensacao;
  clima.umidade = umidade;
  clima.vento = vento;
  clima.codigo = codigo;

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

  JsonObject diario = doc["daily"].as<JsonObject>();
  if (!diario.isNull()) {
    atualizarPrevisao(diario);
  } else {
    Serial.println("Objeto 'daily' ausente");
    previsaoValida = false;
  }

  climaValido = true;
  if (modoAtual == MODO_CLIMA) {
    mostrarClima();
  } else if (modoAtual == MODO_PREVISAO) {
    desenharTelaAtual();
  }
  return true;
}

bool atualizarPrevisao(JsonObject diario) {
  JsonArray datas = diario["time"];
  JsonArray minimas = diario["temperature_2m_min"];
  JsonArray maximas = diario["temperature_2m_max"];
  JsonArray codigos = diario["weather_code"];
  JsonArray chuvas = diario["precipitation_probability_max"];

  if (datas.isNull() || minimas.isNull() || maximas.isNull() ||
      codigos.isNull() || datas.size() < DIAS_PREVISAO ||
      minimas.size() < DIAS_PREVISAO || maximas.size() < DIAS_PREVISAO ||
      codigos.size() < DIAS_PREVISAO) {
    Serial.println("Dados diarios ausentes ou incompletos");
    previsaoValida = false;
    return false;
  }

  PrevisaoDia novas[DIAS_PREVISAO];

  for (int i = 0; i < DIAS_PREVISAO; i++) {
    float minima = minimas[i] | NAN;
    float maxima = maximas[i] | NAN;
    int codigo = codigos[i] | -1;
    int chuva = -1;

    if (!chuvas.isNull() && i < (int)chuvas.size()) {
      chuva = chuvas[i] | -1;
    }

    bool dadosValidos =
        isfinite(minima) && minima >= -90.0f && minima <= 60.0f &&
        isfinite(maxima) && maxima >= -90.0f && maxima <= 60.0f &&
        minima <= maxima && codigo >= 0 && codigo <= 99 &&
        (chuva == -1 || (chuva >= 0 && chuva <= 100));

    if (!dadosValidos) {
      Serial.print("Previsao invalida no dia ");
      Serial.println(i);
      previsaoValida = false;
      return false;
    }

    const char* data = datas[i] | "";
    strncpy(novas[i].data, data, sizeof(novas[i].data) - 1);
    novas[i].data[sizeof(novas[i].data) - 1] = '\0';
    novas[i].minima = minima;
    novas[i].maxima = maxima;
    novas[i].codigo = codigo;
    novas[i].chuva = chuva;
  }

  for (int i = 0; i < DIAS_PREVISAO; i++) {
    previsao[i] = novas[i];
  }

  previsaoValida = true;
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

bool atualizarLeituraLocal() {
  if ((long)(millis() - proximaLeituraDHT) < 0) {
    if (leituraLocal.valida &&
        (long)(millis() - leituraLocal.ultimaLeitura) >= (long)VALIDADE_LEITURA_DHT) {
      leituraLocal.valida = false;
      return true;
    }
    return false;
  }

  proximaLeituraDHT = millis() + INTERVALO_DHT;

  float temperatura = dht.readTemperature();
  float umidade = dht.readHumidity();
  bool dadosValidos = isfinite(temperatura) && temperatura >= 0.0f &&
                      temperatura <= 50.0f && isfinite(umidade) &&
                      umidade >= 0.0f && umidade <= 100.0f;

  if (dadosValidos) {
    bool mudou = !leituraLocal.valida ||
                 fabsf(temperatura - leituraLocal.temperatura) >= 0.5f ||
                 fabsf(umidade - leituraLocal.umidade) >= 0.5f;
    leituraLocal.temperatura = temperatura;
    leituraLocal.umidade = umidade;
    leituraLocal.valida = true;
    leituraLocal.ultimaLeitura = millis();
    return mudou;
  }

  if (leituraLocal.valida &&
      (long)(millis() - leituraLocal.ultimaLeitura) >= (long)VALIDADE_LEITURA_DHT) {
    leituraLocal.valida = false;
    return true;
  }

  return false;
}

void mostrarClimaLocal() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Local (DHT11)");

  display.setTextSize(2);
  display.setCursor(0, 10);
  display.print(leituraLocal.temperatura, 1);
  display.print("C");

  display.setTextSize(1);
  display.setCursor(0, 28);
  display.print("Umidade ");
  display.print(leituraLocal.umidade, 0);
  display.print("%");

  display.setCursor(0, 38);
  display.print(erroLinha1);

  display.setCursor(0, 48);
  display.print(erroLinha2);

  atualizarRelogioTela();
}

void desenharTextoCentralizado(int x, int largura, int y, const String& texto) {
  int larguraTexto = texto.length() * 6;
  display.setCursor(x + (largura - larguraTexto) / 2, y);
  display.print(texto);
}

void mostrarPrevisao() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  desenharTextoCentralizado(0, LARGURA_TELA, 0, "Previsao");

  int larguraColuna = (LARGURA_TELA + DIAS_PREVISAO - 1) / DIAS_PREVISAO;

  for (int i = 0; i < DIAS_PREVISAO; i++) {
    int x = i * larguraColuna;

    char rotulo[8];
    rotuloPrevisao(previsao[i], i, rotulo, sizeof(rotulo));
    desenharTextoCentralizado(x, larguraColuna, 8, rotulo);

    const unsigned char* icone = bitmapClima(previsao[i].codigo, true);
    int iconeX = x + (larguraColuna - ICONE_CLIMA_LARGURA) / 2;
    display.drawXBitmap(iconeX, 16, icone, ICONE_CLIMA_LARGURA,
                        ICONE_CLIMA_ALTURA, SSD1306_WHITE);

    char faixa[12];
    snprintf(faixa, sizeof(faixa), "%.0f/%.0f", previsao[i].minima,
             previsao[i].maxima);
    desenharTextoCentralizado(x, larguraColuna, 32, faixa);

    char chuva[8];
    if (previsao[i].chuva >= 0) {
      snprintf(chuva, sizeof(chuva), "%d%%", previsao[i].chuva);
    } else {
      snprintf(chuva, sizeof(chuva), "--");
    }
    desenharTextoCentralizado(x, larguraColuna, 40, chuva);

    desenharTextoCentralizado(x, larguraColuna, 48,
                              descricaoCurtaTempo(previsao[i].codigo));
  }

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

void exibirTelaClima() {
  if (climaValido) {
    mostrarClima();
  } else if (leituraLocal.valida) {
    mostrarClimaLocal();
  } else {
    mostrarMensagem(erroLinha1, erroLinha2);
  }
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
  display.drawXBitmap(x, y, icone, ICONE_CLIMA_LARGURA, ICONE_CLIMA_ALTURA, SSD1306_WHITE);
}

void desenharIconeHorarioPequeno(int x, int y) {
  const unsigned char* icone = horarioDiurno() ? icone_sol_pequeno : icone_lua_pequena;
  display.drawXBitmap(x, y, icone, ICONE_HORARIO_LARGURA, ICONE_HORARIO_ALTURA, SSD1306_WHITE);
}

void desenharIconeHorarioGrande(int x, int y) {
  const unsigned char* icone = horarioDiurno() ? icone_sol : icone_lua;
  display.drawXBitmap(x, y, icone, ICONE_RELOGIO_LARGURA, ICONE_RELOGIO_ALTURA, SSD1306_WHITE);
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
  } else if (modoAtual == MODO_PREVISAO) {
    if (previsaoValida) {
      mostrarPrevisao();
    } else {
      mostrarMensagem("Previsao", "Indisponivel");
    }
  } else {
    exibirTelaClima();
  }
}

void alternarModo() {
  switch (modoAtual) {
    case MODO_CLIMA:
      modoAtual = MODO_RELOGIO;
      break;
    case MODO_RELOGIO:
      modoAtual = MODO_PREVISAO;
      break;
    default:
      modoAtual = MODO_CLIMA;
      break;
  }

  Serial.print("Modo de visualizacao: ");
  switch (modoAtual) {
    case MODO_CLIMA:
      Serial.println("clima");
      break;
    case MODO_RELOGIO:
      Serial.println("relogio");
      break;
    default:
      Serial.println("previsao");
      break;
  }
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
  previsaoValida = false;

  Serial.print("Erro: ");
  Serial.print(linha1);
  Serial.print(" - ");
  Serial.println(linha2);

  if (modoAtual == MODO_CLIMA) {
    exibirTelaClima();
  } else if (modoAtual == MODO_PREVISAO) {
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

const char* descricaoCurtaTempo(int codigo) {
  switch (codigo) {
    case 0:
    case 1:
      return "Limpo";
    case 2:
      return "Parcial";
    case 3:
      return "Nublado";
    case 45:
    case 48:
      return "Nevoa";
    case 51:
    case 53:
    case 55:
    case 56:
    case 57:
      return "Garoa";
    case 61:
    case 63:
    case 65:
    case 66:
    case 67:
      return "Chuva";
    case 71:
    case 73:
    case 75:
    case 77:
    case 85:
    case 86:
      return "Neve";
    case 80:
    case 81:
    case 82:
      return "Panc.";
    case 95:
    case 96:
    case 99:
      return "Tempest";
    default:
      return "Nublado";
  }
}

void rotuloPrevisao(const PrevisaoDia& dia, int indice, char* buffer,
                    size_t tamanho) {
  if (indice == 0) {
    snprintf(buffer, tamanho, "Hoje");
    return;
  }

  if (indice == 1) {
    snprintf(buffer, tamanho, "Amanha");
    return;
  }

  int ano;
  int mes;
  int diaMes;
  struct tm info = {};

  if (sscanf(dia.data, "%d-%d-%d", &ano, &mes, &diaMes) == 3) {
    info.tm_year = ano - 1900;
    info.tm_mon = mes - 1;
    info.tm_mday = diaMes;
    info.tm_hour = 12;

    if (mktime(&info) != (time_t)-1) {
      static const char* diasSemana[] = {"Dom", "Seg", "Ter", "Qua",
                                         "Qui", "Sex", "Sab"};
      if (info.tm_wday >= 0 && info.tm_wday < 7) {
        snprintf(buffer, tamanho, "%s", diasSemana[info.tm_wday]);
        return;
      }
    }
  }

  snprintf(buffer, tamanho, "Depois");
}
