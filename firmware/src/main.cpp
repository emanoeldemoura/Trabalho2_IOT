// VacinaSegura - monitor de geladeira de vacinas (ESP32)
// No Wokwi este arquivo é o sketch.ino
#include <Arduino.h>
#include <DHT.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <mbedtls/md.h>
#include <esp_sntp.h>
#include "secrets.h"  // não vai para o GitHub

// fica no topo para compilar também como .ino
enum TipoAlerta { ALERTA_TEMPERATURA, ALERTA_PORTA, ALERTA_SENSOR, NUM_TIPOS };

// Identificação e tópicos
const char* ID_DISPOSITIVO = "geladeira01";
const String TOPICO_BASE   = "sis1a/emanoel_iot/posto01/geladeira01";
const String T_TELEMETRIA  = TOPICO_BASE + "/telemetria";
const String T_ESTADO      = TOPICO_BASE + "/estado";
const String T_ALERTA      = TOPICO_BASE + "/alerta";
const String T_COMANDO     = TOPICO_BASE + "/comando";
const String T_ACK         = TOPICO_BASE + "/ack";
const String T_LOTE        = TOPICO_BASE + "/lote";
const String T_LOTE_ACK    = TOPICO_BASE + "/lote_ack";
const String T_CONFIG      = TOPICO_BASE + "/config";

// Certificado raiz ISRG Root X1 (test.mosquitto.org:8886), válido até 2035
const char* CA_ISRG_ROOT_X1 =
  "-----BEGIN CERTIFICATE-----\n"
  "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\n"
  "-----END CERTIFICATE-----\n";

// Pinos (ver diagram.json)
const int PIN_DHT      = 15;  // DHT22
const int PIN_TRIG     = 5;   // HC-SR04 (porta)
const int PIN_ECHO     = 18;
const int PIN_BUZZER   = 4;
const int PIN_LED_ALARME = 2; // vermelho
const int PIN_LED_ONLINE = 23;// verde = online
const int PIN_BOTAO_TESTE = 13; // teste: derruba o Wi-Fi
const int CANAL_BUZZER = 0;

// PWM do buzzer: a API mudou no core 3 (site do Wokwi)
void buzzerIniciar() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(PIN_BUZZER, 1000, 8);
#else
  ledcSetup(CANAL_BUZZER, 1000, 8);
  ledcAttachPin(PIN_BUZZER, CANAL_BUZZER);
#endif
}
void buzzerTom(uint32_t freqHz) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWriteTone(PIN_BUZZER, freqHz);
#else
  ledcWriteTone(CANAL_BUZZER, freqHz);
#endif
}

DHT dht(PIN_DHT, DHT22);
WiFiClientSecure clienteTls;
PubSubClient mqtt(clienteTls);

// Configuração
// valores padrão; a nuvem pode mandar outros (KV)
struct Config {
  float t_max = 8.0;
  float t_min = 2.0;
  float t_desliga_max = 7.0;  // histerese
  float t_desliga_min = 3.0;
  unsigned long tempo_alarme_s = 300;
  unsigned long tempo_porta_s = 60;
};
Config cfg;

bool modoDemo = true;  // tempos curtos para a apresentação

unsigned long intervaloLeituraMs() { return modoDemo ? 2000UL : 10000UL; }
unsigned long tempoAlarmeMs()      { return (modoDemo ? 20UL : cfg.tempo_alarme_s) * 1000UL; }
unsigned long tempoPortaMs()       { return (modoDemo ? 10UL : cfg.tempo_porta_s) * 1000UL; }
unsigned long cooldownAlertaMs()   { return modoDemo ? 60000UL : 600000UL; }

// porta: < 5 cm fechada, > 10 cm aberta, entre os dois mantém
const float PORTA_FECHADA_CM = 5.0;
const float PORTA_ABERTA_CM  = 10.0;

const int FALHAS_DHT_LIMITE = 3;

// Estado do sistema
float tempC = NAN, umidPct = NAN, distCm = NAN;
int   falhasDhtSeguidas = 0;
bool  sensorFalha  = false;
bool  alarmeTemp   = false;
bool  alarme       = false;   // temperatura ou sensor
bool  silenciado   = false;   // só desliga o buzzer
bool  portaAberta  = false;

unsigned long foraDaFaixaDesde = 0;
unsigned long portaAbertaDesde = 0;
bool alertaPortaEnviado = false;
float tempExtrema = NAN;            // pior temperatura do alarme

// cooldown dos alertas
const char* NOME_ALERTA[NUM_TIPOS] = {"temperatura", "porta", "sensor_falha"};
unsigned long ultimoAlertaMs[NUM_TIPOS] = {0, 0, 0};
bool alertaJaEnviado[NUM_TIPOS] = {false, false, false};

unsigned long ultimaLeituraMs = 0;
unsigned long ultimaTentativaMqttMs = 0;
bool estadoPrecisaPublicar = true;

// nonce dos comandos (muda a cada comando aceito)
String nonceComando = "";
void novoNonce() {
  char buf[9];
  snprintf(buf, sizeof(buf), "%08lx", (unsigned long)esp_random());
  nonceComando = buf;
  estadoPrecisaPublicar = true;
}

// Relógio (NTP)
bool relogioOk() { return time(nullptr) > 1700000000; }

// millis() -> epoch UTC (0 se ainda não sincronizou)
uint32_t epochDe(unsigned long instanteMs) {
  if (!relogioOk()) return 0;
  return (uint32_t)time(nullptr) - (uint32_t)((millis() - instanteMs) / 1000UL);
}
uint32_t agoraEpoch() { return epochDe(millis()); }

double umaCasa(float v) { return round(v * 10.0) / 10.0; }

// Publicação MQTT
bool publicarJson(const String& topico, JsonDocument& doc, bool retido = false) {
  if (!mqtt.connected()) return false;
  char buf[384];
  size_t n = serializeJson(doc, buf, sizeof(buf));
  return mqtt.publish(topico.c_str(), (const uint8_t*)buf, n, retido);
}

void publicarEstado() {
  JsonDocument doc;
  doc["online"] = true;
  doc["alarme"] = alarme;
  doc["silenciado"] = silenciado;
  doc["modo"] = modoDemo ? "demo" : "real";
  doc["nonce"] = nonceComando;
  doc["ts"] = agoraEpoch();
  if (publicarJson(T_ESTADO, doc, true)) estadoPrecisaPublicar = false;
}

void publicarTelemetria() {
  JsonDocument doc;
  doc["id"] = ID_DISPOSITIVO;
  doc["ts"] = agoraEpoch();
  if (isnan(tempC)) doc["temp_c"] = nullptr; else doc["temp_c"] = umaCasa(tempC);
  if (isnan(umidPct)) doc["umid_pct"] = nullptr; else doc["umid_pct"] = (int)round(umidPct);
  if (isnan(distCm)) doc["dist_cm"] = nullptr; else doc["dist_cm"] = umaCasa(distCm);
  doc["porta_aberta"] = portaAberta;
  doc["alarme"] = alarme;
  publicarJson(T_TELEMETRIA, doc);
}

// Assinatura (HMAC-SHA256)
String hmacHexChave(const char* chave, const String& mensagem) {
  uint8_t saida[32];
  mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                  (const uint8_t*)chave, strlen(chave),
                  (const uint8_t*)mensagem.c_str(), mensagem.length(), saida);
  char hex[65];
  for (int i = 0; i < 32; i++) sprintf(hex + 2 * i, "%02x", saida[i]);
  return String(hex);
}

// comparação em tempo constante
bool iguaisSeguro(const String& a, const String& b) {
  if (a.length() != b.length()) return false;
  uint8_t dif = 0;
  for (unsigned int i = 0; i < a.length(); i++) dif |= a[i] ^ b[i];
  return dif == 0;
}

// formato: <assinatura>\n<json>
String conferirDoWorker(const String& msg) {
  int nl = msg.indexOf('\n');
  if (nl != 64) return "";
  String json = msg.substring(65);
  return iguaisSeguro(hmacHexChave(INGEST_TOKEN, json), msg.substring(0, 64)) ? json : "";
}

// Histórico na nuvem (via MQTT)
// Fila na RAM enviada em lotes assinados pelo MQTT (o Worker grava no D1
// e responde no lote_ack). Sem internet a fila cresce e depois é enviada.

struct Registro {
  uint32_t ms;
  int16_t  temp10;         // temperatura x10
  uint8_t  umid;
  uint8_t  flags;          // 1 = porta, 2 = sem temp, 4 = sem umid
};
// 720 = 2 h no demo, 12 h no modo real
const int CAP_FILA = 720;
Registro fila[CAP_FILA];
int filaInicio = 0, filaTam = 0;

struct Evento { uint8_t tipo; uint8_t evento; uint32_t ms; int16_t temp10; bool semTemp; };
const char* NOME_EVENTO[3] = {"inicio", "fim", "atendido"};
enum { EV_INICIO, EV_FIM, EV_ATENDIDO };
const int CAP_EVENTOS = 24;
Evento eventos[CAP_EVENTOS];
int evInicio = 0, evTam = 0;

const int LOTE_MAX_LEITURAS = 40;
const int LOTE_MAX_EVENTOS = 8;
const unsigned long ESPERA_ACK_MS = 150000UL;

unsigned long ultimaGravacaoMs = 0;
bool loteEmVoo = false;          // esperando o ack
String loteId = "";
int loteLeituras = 0, loteEventos = 0;
unsigned long loteEnviadoMs = 0;
unsigned long numeroLote = 0;
bool configCarregada = false;

unsigned long intervaloGravacaoMs() { return modoDemo ? 10000UL : 60000UL; }

void enfileirarLeitura() {
  Registro r;
  r.ms = millis();
  r.flags = portaAberta ? 1 : 0;
  if (isnan(tempC)) { r.temp10 = 0; r.flags |= 2; } else r.temp10 = (int16_t)lround(tempC * 10);
  if (isnan(umidPct)) { r.umid = 0; r.flags |= 4; } else r.umid = (uint8_t)constrain(lround(umidPct), 0, 100);
  if (filaTam == CAP_FILA) {            // cheia: perde a mais antiga
    filaInicio = (filaInicio + 1) % CAP_FILA;
    filaTam--;
    if (loteEmVoo && loteLeituras > 0) loteLeituras--;
    Serial.println("(fila cheia: descartada a leitura mais antiga)");
  }
  fila[(filaInicio + filaTam) % CAP_FILA] = r;
  filaTam++;
}

void registrarEvento(TipoAlerta tipo, uint8_t evento, unsigned long ms, float temp) {
  if (evTam == CAP_EVENTOS) {
    evInicio = (evInicio + 1) % CAP_EVENTOS; evTam--;
    if (loteEmVoo && loteEventos > 0) loteEventos--;
  }
  Evento e;
  e.tipo = tipo; e.evento = evento; e.ms = ms;
  e.semTemp = isnan(temp);
  e.temp10 = e.semTemp ? 0 : (int16_t)lround(temp * 10);
  eventos[(evInicio + evTam) % CAP_EVENTOS] = e;
  evTam++;
}

void enviarFilas() {
  if (!mqtt.connected() || !relogioOk()) return;
  if (loteEmVoo) {
    if (millis() - loteEnviadoMs < ESPERA_ACK_MS) return;
    Serial.printf("Lote %s sem confirmacao: reenviando\n", loteId.c_str());
    loteEmVoo = false;
  }
  if (filaTam == 0 && evTam == 0) return;

  int n = min(filaTam, LOTE_MAX_LEITURAS);
  int ne = min(evTam, LOTE_MAX_EVENTOS);
  JsonDocument d;
  loteId = "L" + String(++numeroLote) + "-" + String(agoraEpoch() % 100000);
  d["id"] = loteId;
  JsonArray ls = d["leituras"].to<JsonArray>();
  for (int i = 0; i < n; i++) {
    Registro& r = fila[(filaInicio + i) % CAP_FILA];
    JsonObject o = ls.add<JsonObject>();
    o["ts"] = epochDe(r.ms);           // horário original
    if (r.flags & 2) o["temp_c"] = nullptr; else o["temp_c"] = r.temp10 / 10.0;
    if (r.flags & 4) o["umid_pct"] = nullptr; else o["umid_pct"] = r.umid;
    o["porta_aberta"] = (bool)(r.flags & 1);
  }
  JsonArray es = d["eventos"].to<JsonArray>();
  for (int i = 0; i < ne; i++) {
    Evento& e = eventos[(evInicio + i) % CAP_EVENTOS];
    JsonObject o = es.add<JsonObject>();
    o["evento"] = NOME_EVENTO[e.evento];
    o["tipo"] = NOME_ALERTA[e.tipo];
    o[e.evento == EV_INICIO ? "desde_ts" : "ts"] = epochDe(e.ms);
    if (e.semTemp) o["temp_c"] = nullptr; else o["temp_c"] = e.temp10 / 10.0;
  }
  String json;
  serializeJson(d, json);
  String msg = hmacHexChave(INGEST_TOKEN, json) + "\n" + json;
  if (mqtt.publish(T_LOTE.c_str(), (const uint8_t*)msg.c_str(), msg.length(), true)) {
    loteEmVoo = true;
    loteLeituras = n; loteEventos = ne;
    loteEnviadoMs = millis();
    Serial.printf("Lote %s publicado (%d leituras, %d eventos, fila=%d)\n", loteId.c_str(), n, ne, filaTam);
  } else {
    Serial.println("Lote nao publicado (MQTT ocupado); tento de novo");
  }
}

void aoReceberAck(const String& json) {
  JsonDocument d;
  if (deserializeJson(d, json)) return;
  String id = d["id"] | "";
  if (!loteEmVoo || id != loteId) return;
  filaInicio = (filaInicio + loteLeituras) % CAP_FILA; filaTam -= loteLeituras;
  evInicio = (evInicio + loteEventos) % CAP_EVENTOS;   evTam -= loteEventos;
  loteEmVoo = false;
  mqtt.publish(T_LOTE.c_str(), (const uint8_t*)"", 0, true);
  Serial.printf("Lote %s gravado na nuvem (%d leituras). fila=%d\n",
                id.c_str(), (int)(d["gravadas"] | 0), filaTam);
}

void aoReceberConfig(const String& json) {
  JsonDocument d;
  if (deserializeJson(d, json)) return;
  Config c;
  c.t_max = d["t_max"] | cfg.t_max;
  c.t_min = d["t_min"] | cfg.t_min;
  c.t_desliga_max = d["t_desliga_max"] | cfg.t_desliga_max;
  c.t_desliga_min = d["t_desliga_min"] | cfg.t_desliga_min;
  c.tempo_alarme_s = d["tempo_alarme_s"] | cfg.tempo_alarme_s;
  c.tempo_porta_s = d["tempo_porta_s"] | cfg.tempo_porta_s;
  bool ok = c.t_min < c.t_desliga_min && c.t_desliga_min < c.t_desliga_max &&
            c.t_desliga_max < c.t_max && c.tempo_alarme_s >= 5 && c.tempo_porta_s >= 5;
  if (!ok) { Serial.println("Config da nuvem invalida: mantendo a atual"); return; }
  cfg = c;
  configCarregada = true;
  Serial.printf("Config da nuvem: faixa %.1f-%.1f C, desliga %.1f-%.1f C, alarme %lus, porta %lus\n",
                cfg.t_min, cfg.t_max, cfg.t_desliga_min, cfg.t_desliga_max,
                cfg.tempo_alarme_s, cfg.tempo_porta_s);
}

// Wi-Fi e botão de teste
// botão de teste: derruba o Wi-Fi por 60 s
const unsigned long TESTE_SEM_WIFI_MS = 60000UL;
unsigned long wifiPausadoAte = 0;
unsigned long ultimoWifiBeginMs = 0;

bool emTesteSemWifi() { return wifiPausadoAte != 0; }

void lerBotaoTeste() {
  static bool anterior = HIGH;
  static unsigned long ultimaMudancaMs = 0;
  bool atual = digitalRead(PIN_BOTAO_TESTE);
  if (atual != anterior && millis() - ultimaMudancaMs > 50) {
    ultimaMudancaMs = millis();
    anterior = atual;
    if (atual == LOW && !emTesteSemWifi()) {
      Serial.println("### TESTE: Wi-Fi derrubado por 60 s (alarme local continua)");
      mqtt.disconnect();
      WiFi.disconnect();
      wifiPausadoAte = millis() + TESTE_SEM_WIFI_MS;
    }
  }
}

void gerenciarWifi() {
  if (emTesteSemWifi()) {
    if ((long)(millis() - wifiPausadoAte) < 0) return;
    wifiPausadoAte = 0;
    Serial.println("### TESTE: religando o Wi-Fi");
    WiFi.begin(WIFI_SSID, WIFI_SENHA);
    ultimoWifiBeginMs = millis();
    return;
  }
  // NTP só depois de conectar (antes trava no core 3)
  static bool ntpLigado = false;
  if (!ntpLigado && WiFi.status() == WL_CONNECTED) {
    esp_sntp_set_sync_interval(60000);
    configTime(0, 0, "pool.ntp.org");
    ntpLigado = true;
  }
  if (WiFi.status() != WL_CONNECTED && millis() - ultimoWifiBeginMs > 15000UL) {
    WiFi.begin(WIFI_SSID, WIFI_SENHA);
    ultimoWifiBeginMs = millis();
  }
}

// Sensores e regras

float medirDistanciaCm() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);
  unsigned long duracaoUs = pulseIn(PIN_ECHO, HIGH, 30000UL);
  if (duracaoUs == 0) return NAN;
  return duracaoUs * 0.0343f / 2.0f;
}

void enviarAlerta(TipoAlerta tipo, unsigned long desdeMs) {
  unsigned long agora = millis();
  if (alertaJaEnviado[tipo] && agora - ultimoAlertaMs[tipo] < cooldownAlertaMs()) {
    Serial.printf("(alerta %s em cooldown)\n", NOME_ALERTA[tipo]);
    return;
  }
  alertaJaEnviado[tipo] = true;
  ultimoAlertaMs[tipo] = agora;
  Serial.printf(">>> ALERTA %s | temp_c=%.1f | desde=%lus atras\n",
                NOME_ALERTA[tipo], tempC, (agora - desdeMs) / 1000UL);

  JsonDocument doc;
  doc["tipo"] = NOME_ALERTA[tipo];
  if (isnan(tempC)) doc["temp_c"] = nullptr; else doc["temp_c"] = umaCasa(tempC);
  doc["desde_ts"] = epochDe(desdeMs);
  doc["ts"] = agoraEpoch();
  if (!publicarJson(T_ALERTA, doc)) {
    Serial.println("(sem conexao: alerta so local por enquanto)");
  }
}

void lerTemperatura() {
  float t = dht.readTemperature();
  float u = dht.readHumidity();
  if (isnan(t)) {
    falhasDhtSeguidas++;
    tempC = NAN;
    umidPct = NAN;
    if (falhasDhtSeguidas >= FALHAS_DHT_LIMITE && !sensorFalha) {
      sensorFalha = true;
      Serial.println("!!! Sensor de temperatura sem resposta (3 leituras)");
      enviarAlerta(ALERTA_SENSOR, millis());
      registrarEvento(ALERTA_SENSOR, EV_INICIO, millis(), NAN);
    }
    return;
  }
  if (sensorFalha) {
    Serial.println("Sensor de temperatura voltou a responder");
    registrarEvento(ALERTA_SENSOR, EV_FIM, millis(), NAN);
  }
  falhasDhtSeguidas = 0;
  sensorFalha = false;
  tempC = t;
  umidPct = u;
}

void decidirTemperatura() {
  if (isnan(tempC)) return;
  unsigned long agora = millis();
  bool fora = (tempC > cfg.t_max || tempC < cfg.t_min);

  if (fora) {
    if (foraDaFaixaDesde == 0) foraDaFaixaDesde = agora;
    if (!alarmeTemp && agora - foraDaFaixaDesde >= tempoAlarmeMs()) {
      alarmeTemp = true;
      tempExtrema = tempC;
      enviarAlerta(ALERTA_TEMPERATURA, foraDaFaixaDesde);
      registrarEvento(ALERTA_TEMPERATURA, EV_INICIO, foraDaFaixaDesde, tempC);
    }
  } else {
    foraDaFaixaDesde = 0;
  }

  if (alarmeTemp && !isnan(tempExtrema)) {
    float distAtual = tempC > cfg.t_max ? tempC - cfg.t_max : (tempC < cfg.t_min ? cfg.t_min - tempC : 0);
    float distPior  = tempExtrema > cfg.t_max ? tempExtrema - cfg.t_max
                    : (tempExtrema < cfg.t_min ? cfg.t_min - tempExtrema : 0);
    if (distAtual > distPior) tempExtrema = tempC;
  }

  // só desliga entre 3 e 7 °C
  if (alarmeTemp && tempC >= cfg.t_desliga_min && tempC <= cfg.t_desliga_max) {
    alarmeTemp = false;
    registrarEvento(ALERTA_TEMPERATURA, EV_FIM, agora, tempExtrema);
    tempExtrema = NAN;
  }
}

// leitura inválida = porta aberta
void decidirPorta() {
  distCm = medirDistanciaCm();
  if (isnan(distCm) || distCm > PORTA_ABERTA_CM) portaAberta = true;
  else if (distCm < PORTA_FECHADA_CM)            portaAberta = false;

  unsigned long agora = millis();
  if (portaAberta) {
    if (portaAbertaDesde == 0) portaAbertaDesde = agora;
    if (!alertaPortaEnviado && agora - portaAbertaDesde >= tempoPortaMs()) {
      alertaPortaEnviado = true;
      enviarAlerta(ALERTA_PORTA, portaAbertaDesde);
      registrarEvento(ALERTA_PORTA, EV_INICIO, portaAbertaDesde, tempC);
    }
  } else {
    if (alertaPortaEnviado) registrarEvento(ALERTA_PORTA, EV_FIM, agora, tempC);
    portaAbertaDesde = 0;
    alertaPortaEnviado = false;
  }
}

void atualizarAtuadores() {
  bool novoAlarme = alarmeTemp || sensorFalha;
  if (novoAlarme != alarme) {
    alarme = novoAlarme;
    estadoPrecisaPublicar = true;
    if (alarme) {
      Serial.printf("*** ALARME LIGADO (%s)\n", sensorFalha ? "sensor_falha" : "temperatura");
    } else {
      Serial.println("*** ALARME DESLIGADO (temperatura normalizada)");
      silenciado = false;
    }
  }
  digitalWrite(PIN_LED_ALARME, alarme ? HIGH : LOW);

  static bool buzzerLigado = false;
  bool deveTocar = alarme && !silenciado;
  if (deveTocar != buzzerLigado) {
    buzzerLigado = deveTocar;
    buzzerTom(deveTocar ? 1000 : 0);
  }
}

void imprimirLeitura() {
  Serial.printf("T=%.1fC U=%.0f%% dist=%.1fcm porta=%s alarme=%d silenciado=%d modo=%s online=%d fila=%d mem=%uKB\n",
                tempC, umidPct, distCm, portaAberta ? "aberta" : "fechada",
                alarme, silenciado, modoDemo ? "demo" : "real", mqtt.connected(),
                filaTam, (unsigned)(ESP.getFreeHeap() / 1024));
}

// Comandos (painel -> ESP32)
// Só "silenciar" e "modo". Assinatura = HMAC(COMANDO_SENHA, "cmd|valor|id|nonce")

const int N_IDS = 8;
String idsUsados[N_IDS];
int proximoId = 0;
bool idJaUsado(const char* id) {
  for (int i = 0; i < N_IDS; i++) if (idsUsados[i] == id) return true;
  return false;
}
void lembrarId(const char* id) {
  idsUsados[proximoId] = id;
  proximoId = (proximoId + 1) % N_IDS;
}

String hmacHex(const String& mensagem) { return hmacHexChave(COMANDO_SENHA, mensagem); }

void aoReceberMensagem(char* topico, byte* payload, unsigned int tamanho) {
  // mensagens do Worker
  if (T_LOTE_ACK == topico || T_CONFIG == topico) {
    if (tamanho == 0 || tamanho > 1024) return;
    String msg;
    msg.reserve(tamanho);
    for (unsigned int i = 0; i < tamanho; i++) msg += (char)payload[i];
    String json = conferirDoWorker(msg);
    if (json.length() == 0) { Serial.println("Mensagem da nuvem com assinatura invalida: ignorada"); return; }
    if (T_LOTE_ACK == topico) aoReceberAck(json); else aoReceberConfig(json);
    return;
  }
  if (T_COMANDO != topico) return;
  if (tamanho > 300) { Serial.println("Comando ignorado: grande demais"); return; }

  JsonDocument doc;
  if (deserializeJson(doc, payload, tamanho)) {
    Serial.println("Comando ignorado: JSON invalido");
    return;
  }
  const char* cmd   = doc["cmd"]   | "";
  const char* valor = doc["valor"] | "";
  const char* id    = doc["id"]    | "";
  const char* assin = doc["assinatura"] | "";
  const char* nonce = doc["nonce"] | "";

  if (!doc["id"].is<const char*>() || strlen(id) == 0 || strlen(id) > 32) {
    Serial.println("Comando ignorado: sem id valido");
    return;
  }
  if (strcmp(cmd, "silenciar") != 0 && strcmp(cmd, "modo") != 0) {
    Serial.println("Comando ignorado: cmd desconhecido");
    return;
  }
  String mensagem = String(cmd) + "|" + valor + "|" + id + "|" + nonce;
  if (!iguaisSeguro(hmacHex(mensagem), String(assin))) {
    Serial.println("Comando ignorado: assinatura invalida");
    return;
  }
  if (nonceComando.length() == 0 || !iguaisSeguro(String(nonce), nonceComando)) {
    Serial.println("Comando ignorado: nonce vencido (comando antigo/repetido?)");
    return;
  }
  if (idJaUsado(id)) {
    Serial.println("Comando ignorado: id repetido");
    return;
  }

  if (strcmp(cmd, "silenciar") == 0) {
    if (alarme && !silenciado)   // guarda só o horário (LGPD)
      registrarEvento(sensorFalha ? ALERTA_SENSOR : ALERTA_TEMPERATURA, EV_ATENDIDO, millis(), tempC);
    silenciado = true;
    Serial.println("Comando: silenciar");
  } else {
    if (strcmp(valor, "demo") == 0)      modoDemo = true;
    else if (strcmp(valor, "real") == 0) modoDemo = false;
    else { Serial.println("Comando ignorado: modo invalido"); return; }
    Serial.printf("Comando: modo %s\n", valor);
  }
  lembrarId(id);
  novoNonce();

  atualizarAtuadores();
  estadoPrecisaPublicar = true;

  JsonDocument ack;
  ack["id"] = id;
  ack["ok"] = true;
  ack["ts"] = agoraEpoch();
  if (publicarJson(T_ACK, ack)) Serial.printf("ack enviado (id=%s)\n", id);
}

// Conexão
void manterConexao() {
  if (emTesteSemWifi() || WiFi.status() != WL_CONNECTED) return;
  if (mqtt.connected()) return;
  if (millis() - ultimaTentativaMqttMs < 5000UL && ultimaTentativaMqttMs != 0) return;
  ultimaTentativaMqttMs = millis();

  Serial.println("Conectando ao broker MQTT (TLS)...");
  String clientId = "vacinasegura-emanoel_iot-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  // Last Will
  const char* testamento = "{\"online\":false}";
  if (!mqtt.connect(clientId.c_str(), T_ESTADO.c_str(), 1, true, testamento)) {
    Serial.printf("MQTT falhou (codigo %d). Nova tentativa em 5 s\n", mqtt.state());
    return;
  }
  Serial.println("MQTT conectado");
  mqtt.subscribe(T_COMANDO.c_str(), 1);
  mqtt.subscribe(T_LOTE_ACK.c_str(), 1);
  mqtt.subscribe(T_CONFIG.c_str(), 1);
  estadoPrecisaPublicar = true;
}

// Programa principal
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("olá - VacinaSegura iniciado");

  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  buzzerIniciar();
  buzzerTom(0);
  pinMode(PIN_LED_ALARME, OUTPUT);
  pinMode(PIN_LED_ONLINE, OUTPUT);
  pinMode(PIN_BOTAO_TESTE, INPUT_PULLUP);
  digitalWrite(PIN_LED_ONLINE, LOW);

  dht.begin();
  novoNonce();
  Serial.printf("Modo: %s | faixa %.0f-%.0f C | alarme apos %lus | porta apos %lus\n",
                modoDemo ? "demonstracao" : "real", cfg.t_min, cfg.t_max,
                tempoAlarmeMs() / 1000UL, tempoPortaMs() / 1000UL);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_SENHA);
  ultimoWifiBeginMs = millis();

  clienteTls.setCACert(CA_ISRG_ROOT_X1);
  mqtt.setServer(MQTT_HOST, MQTT_PORTA);
  mqtt.setCallback(aoReceberMensagem);
  mqtt.setBufferSize(4096);
  mqtt.setKeepAlive(30);
}

void loop() {
  lerBotaoTeste();
  gerenciarWifi();
  manterConexao();
  mqtt.loop();

  bool online = WiFi.status() == WL_CONNECTED && mqtt.connected();
  digitalWrite(PIN_LED_ONLINE, online ? HIGH : LOW);

  unsigned long agora = millis();
  if (agora - ultimaLeituraMs >= intervaloLeituraMs()) {
    ultimaLeituraMs = agora;
    lerTemperatura();
    decidirTemperatura();
    decidirPorta();
    atualizarAtuadores();
    imprimirLeitura();
    publicarTelemetria();
  }
  if (agora - ultimaGravacaoMs >= intervaloGravacaoMs() && ultimaLeituraMs != 0) {
    ultimaGravacaoMs = agora;
    enfileirarLeitura();
  }
  enviarFilas();
  if (estadoPrecisaPublicar) publicarEstado();
}
