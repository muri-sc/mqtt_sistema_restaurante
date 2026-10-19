/*
  Foods - receptor de pedidos (ESP32 DevKit V1)

  Bibliotecas:
    - WiFi            (já vem no core ESP32)
    - PubSubClient    (Nick O'Leary)
    - ArduinoJson     (Benoit Blanchon, versão 7.x)
    - BluetoothSerial (já vem no core ESP32; só usada com IMPRESSORA_BT)

  Tópicos:
    foods/grupo_k7x2/pedidos  -> chega o pedido (JSON)
    foods/grupo_k7x2/status   -> o ESP32 responde {"id":"..","status":"impresso"|"recusado","motivo":".."}

  Formato do pedido:
    {"id":"K3F9A","mesa":5,"cliente":"João","obs":"sem cebola",
     "horario":"19/10/2026 22:44",
     "itens":[{"nome":"BBB","qtd":2,"preco":36.9}]}
*/

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// Descomente para imprimir de verdade via Bluetooth (não funciona no Wokwi).
// No Arduino IDE use Partition Scheme = "Huge APP (3MB No OTA)".
// #define IMPRESSORA_BT

#ifdef IMPRESSORA_BT
  #include <BluetoothSerial.h>
  BluetoothSerial SerialBT;
  const char* NOME_IMPRESSORA = "GoldenSky";  // nome Bluetooth (veja no auto-teste da impressora)
  #define SAIDA SerialBT
  const bool REAL = true;
#else
  #define SAIDA Serial                         // simulação: "imprime" no Serial Monitor
  const bool REAL = false;
#endif

// ---------- Configuração ----------
const char* WIFI_SSID   = "Wokwi-GUEST";
const char* WIFI_PASS   = "";
const int   WIFI_CANAL  = 6;                   // canal usado pelo Wokwi

const char* MQTT_SERVER = "broker.hivemq.com";
const uint16_t MQTT_PORT = 1883;
const char* TOPIC_PEDIDOS = "foods/grupo_k7x2/pedidos";
const char* TOPIC_STATUS  = "foods/grupo_k7x2/status";

const int MAX_MESA  = 99;
const int MAX_ITENS = 30;
const int MAX_QTD   = 99;
const int MAX_NOME  = 32;                      // colunas do nome do cliente
const int MAX_ITEM  = 40;                      // colunas do nome do item
const int MAX_OBS   = 120;                     // colunas da observação

const int LARGURA   = 48;                      // colunas do papel 80mm (ajuste se sair torto)
const uint8_t CODEPAGE = 2;                    // ESC t n: 2 = CP850, 3 = CP860 (Português)

const unsigned long INTERVALO_WIFI = 5000;
const unsigned long INTERVALO_MQTT = 3000;

WiFiClient wifi;
PubSubClient mqtt(wifi);
unsigned long ultimaTentativaWifi = 0;
unsigned long ultimaTentativaMqtt = 0;

// ---- HELPERS BEGIN (C++ puro, sem Arduino) ----
int larguraUtf8(const char* s) {
  int n = 0;
  for (; *s; s++) if (((uint8_t)*s & 0xC0) != 0x80) n++;
  return n;
}

// Copia no máximo "cols" caracteres UTF-8 sem cortar um acento no meio
void copiaUtf8(char* dst, size_t tam, const char* src, int cols) {
  size_t i = 0;
  int n = 0;
  while (*src && i + 1 < tam) {
    if (((uint8_t)*src & 0xC0) != 0x80) {
      if (n >= cols) break;
      n++;
    }
    dst[i++] = *src++;
  }
  dst[i] = 0;
}

// Code point Unicode (Latin-1) -> byte da code page CP850
uint8_t cp850(uint32_t cp) {
  switch (cp) {
    case 0xC7: return 0x80;  // Ç
    case 0xFC: return 0x81;  // ü
    case 0xE9: return 0x82;  // é
    case 0xE2: return 0x83;  // â
    case 0xE0: return 0x85;  // à
    case 0xE7: return 0x87;  // ç
    case 0xEA: return 0x88;  // ê
    case 0xE8: return 0x8A;  // è
    case 0xEE: return 0x8C;  // î
    case 0xEC: return 0x8D;  // ì
    case 0xC9: return 0x90;  // É
    case 0xF4: return 0x93;  // ô
    case 0xF2: return 0x95;  // ò
    case 0xFB: return 0x96;  // û
    case 0xF9: return 0x97;  // ù
    case 0xDC: return 0x9A;  // Ü
    case 0xE1: return 0xA0;  // á
    case 0xED: return 0xA1;  // í
    case 0xF3: return 0xA2;  // ó
    case 0xFA: return 0xA3;  // ú
    case 0xF1: return 0xA4;  // ñ
    case 0xD1: return 0xA5;  // Ñ
    case 0xAA: return 0xA6;  // ª
    case 0xBA: return 0xA7;  // º
    case 0xC1: return 0xB5;  // Á
    case 0xC2: return 0xB6;  // Â
    case 0xC0: return 0xB7;  // À
    case 0xE3: return 0xC6;  // ã
    case 0xC3: return 0xC7;  // Ã
    case 0xCA: return 0xD2;  // Ê
    case 0xCD: return 0xD6;  // Í
    case 0xD3: return 0xE0;  // Ó
    case 0xD4: return 0xE2;  // Ô
    case 0xF5: return 0xE4;  // õ
    case 0xD5: return 0xE5;  // Õ
    case 0xDA: return 0xE9;  // Ú
    default:   return '?';
  }
}

// UTF-8 -> CP850. Retorna quantos bytes foram escritos em "out".
size_t utf8ParaCp850(const char* s, uint8_t* out, size_t tam) {
  size_t n = 0;
  while (*s && n < tam) {
    uint8_t b = (uint8_t)*s++;
    if (b < 0x80) {
      out[n++] = b;
    } else if ((b & 0xE0) == 0xC0 && *s) {                  // caractere de 2 bytes
      uint32_t cp = ((uint32_t)(b & 0x1F) << 6) | ((uint8_t)*s++ & 0x3F);
      out[n++] = cp850(cp);
    } else {                                                // 3-4 bytes ou inválido
      out[n++] = '?';
      while (((uint8_t)*s & 0xC0) == 0x80) s++;
    }
  }
  return n;
}

void moeda(char* buf, size_t tam, float v) {
  snprintf(buf, tam, "R$ %.2f", v);
  for (char* p = buf; *p; p++) if (*p == '.') *p = ',';
}
// ---- HELPERS END ----

// ---------- Camada de impressão (ESC/POS) ----------
// Em simulação os comandos de controle são omitidos e o texto vai para o Serial Monitor.
void cmd(const uint8_t* c, size_t n) { if (REAL) SAIDA.write(c, n); }

void pInit()             { const uint8_t c[] = {0x1B, '@', 0x1B, 't', CODEPAGE}; cmd(c, sizeof(c)); }
void pAlinha(uint8_t a)  { const uint8_t c[] = {0x1B, 'a', a};                   cmd(c, sizeof(c)); }  // 0 esq, 1 centro, 2 dir
void pNegrito(bool on)   { const uint8_t c[] = {0x1B, 'E', (uint8_t)on};         cmd(c, sizeof(c)); }
void pTamanho(uint8_t n) { const uint8_t c[] = {0x1D, '!', n};                   cmd(c, sizeof(c)); }  // 0x00 normal, 0x01 alto 2x, 0x11 2x2
void pAvanca(uint8_t n)  { const uint8_t c[] = {0x1B, 'd', n};                   cmd(c, sizeof(c)); }

void pCorta() {
  const uint8_t c[] = {0x1D, 'V', 66, 3};                                       // corte parcial
  cmd(c, sizeof(c));
  if (!REAL) Serial.println("---------- corte ----------\n");
}

void pTexto(const char* s) {
  if (REAL) {
    uint8_t buf[200];
    size_t n = utf8ParaCp850(s, buf, sizeof(buf));
    SAIDA.write(buf, n);
  } else {
    Serial.print(s);
  }
}

void pLinha(const char* s = "") {
  pTexto(s);
  if (REAL) SAIDA.write('\n'); else Serial.println();
}

void pSeparador() {
  char s[LARGURA + 1];
  memset(s, '-', LARGURA);
  s[LARGURA] = 0;
  pLinha(s);
}

// Texto à esquerda e valor à direita na mesma linha
void pLinhaLR(const char* esq, const char* dir) {
  int wd = larguraUtf8(dir);
  int cols = LARGURA - wd - 1;
  if (cols < 0) cols = 0;
  char e[4 * LARGURA + 1];
  copiaUtf8(e, sizeof(e), esq, cols);
  int espacos = LARGURA - larguraUtf8(e) - wd;
  pTexto(e);
  for (int i = 0; i < espacos; i++) pTexto(" ");
  pLinha(dir);
}

void imprimirVia(bool caixa, const char* id, int mesa, const char* cliente,
                 const char* horario, const char* obs, JsonArray itens) {
  char buf[200], valor[24], nome[4 * MAX_ITEM + 1];

  pInit();
  pAlinha(1);
  pNegrito(true);
  pTamanho(0x11);
  pLinha(caixa ? "CAIXA" : "COZINHA");
  pTamanho(0x00);
  snprintf(buf, sizeof(buf), "Pedido #%s", id);
  pLinha(buf);
  pNegrito(false);
  pLinha(horario);
  pAlinha(0);
  pSeparador();

  pNegrito(true);
  snprintf(buf, sizeof(buf), "Mesa %d", mesa);
  pLinha(buf);
  pNegrito(false);
  snprintf(buf, sizeof(buf), "Cliente: %s", cliente);
  pLinha(buf);
  pSeparador();

  float total = 0;
  for (JsonObject it : itens) {
    int qtd = it["qtd"];
    float sub = qtd * it["preco"].as<float>();
    total += sub;

    copiaUtf8(nome, sizeof(nome), it["nome"] | "", MAX_ITEM);
    snprintf(buf, sizeof(buf), "%dx %s", qtd, nome);
    if (caixa) {
      moeda(valor, sizeof(valor), sub);
      pLinhaLR(buf, valor);
    } else {                                    // cozinha: letra maior, sem preço
      pNegrito(true);
      pTamanho(0x01);
      pLinha(buf);
      pTamanho(0x00);
      pNegrito(false);
    }
  }
  pSeparador();

  if (obs[0]) {
    pNegrito(true);
    pLinha("Obs:");
    pNegrito(false);
    pLinha(obs);
    pSeparador();
  }

  if (caixa) {
    moeda(valor, sizeof(valor), total);
    pNegrito(true);
    pTamanho(0x01);
    pLinhaLR("TOTAL", valor);
    pTamanho(0x00);
    pNegrito(false);
  }

  pAvanca(4);
  pCorta();
  if (REAL) SAIDA.flush();
}

bool impressoraPronta() {
#ifdef IMPRESSORA_BT
  if (SerialBT.connected()) return true;
  Serial.println("Conectando na impressora...");
  return SerialBT.connect(NOME_IMPRESSORA);
#else
  return true;
#endif
}

// ---------- Validação ----------
bool validarPedido(JsonDocument& doc, String& erro) {
  if (!doc["mesa"].is<int>()) { erro = "mesa ausente ou invalida"; return false; }
  int mesa = doc["mesa"];
  if (mesa < 1 || mesa > MAX_MESA) { erro = "mesa fora do intervalo"; return false; }

  if (!doc["itens"].is<JsonArray>()) { erro = "itens ausentes"; return false; }
  JsonArray itens = doc["itens"];
  if (itens.size() == 0)        { erro = "pedido sem itens"; return false; }
  if (itens.size() > MAX_ITENS) { erro = "itens demais"; return false; }

  for (JsonObject it : itens) {
    if (!it["nome"].is<const char*>() || strlen(it["nome"] | "") == 0) {
      erro = "item sem nome"; return false;
    }
    if (!it["qtd"].is<int>() || (int)it["qtd"] < 1 || (int)it["qtd"] > MAX_QTD) {
      erro = "quantidade invalida"; return false;
    }
    if (!it["preco"].is<float>() || (float)it["preco"] < 0) {
      erro = "preco invalido"; return false;
    }
  }
  return true;
}

// ---------- Status e duplicatas ----------
void publicarStatus(const char* id, const char* status, const char* motivo = nullptr) {
  JsonDocument out;
  out["id"] = id;
  out["status"] = status;
  if (motivo) out["motivo"] = motivo;
  char buf[200];
  serializeJson(out, buf, sizeof(buf));
  mqtt.publish(TOPIC_STATUS, buf);
}

// Com QoS 1 o broker pode reentregar a mesma mensagem; guardamos os últimos ids
const int N_IDS = 10;
char ultimosIds[N_IDS][24] = {};
int proxId = 0;

bool jaProcessado(const char* id) {
  for (int i = 0; i < N_IDS; i++) if (strcmp(ultimosIds[i], id) == 0) return true;
  return false;
}

void marcarProcessado(const char* id) {
  snprintf(ultimosIds[proxId], sizeof(ultimosIds[proxId]), "%s", id);
  proxId = (proxId + 1) % N_IDS;
}

// ---------- Callback MQTT ----------
void callback(char* topic, byte* payload, unsigned int length) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload, length);
  if (err) {
    Serial.printf("JSON invalido: %s\n", err.c_str());
    return;
  }

  char id[24];
  snprintf(id, sizeof(id), "%s", (const char*)(doc["id"] | ""));
  if (id[0] == 0) snprintf(id, sizeof(id), "m%lu", millis());

  if (jaProcessado(id)) {
    Serial.printf("Pedido %s repetido, ignorando\n", id);
    return;
  }

  String erro;
  if (!validarPedido(doc, erro)) {
    Serial.printf("Pedido %s recusado: %s\n", id, erro.c_str());
    publicarStatus(id, "recusado", erro.c_str());
    return;
  }

  if (!impressoraPronta()) {
    Serial.printf("Pedido %s: impressora offline\n", id);
    publicarStatus(id, "recusado", "impressora offline");
    return;
  }
  marcarProcessado(id);

  int mesa = doc["mesa"];
  char cliente[4 * MAX_NOME + 1], horario[32], obs[4 * MAX_OBS + 1];
  copiaUtf8(cliente, sizeof(cliente), doc["cliente"] | "", MAX_NOME);
  if (cliente[0] == 0) snprintf(cliente, sizeof(cliente), "Sem nome");
  snprintf(horario, sizeof(horario), "%s", (const char*)(doc["horario"] | ""));
  copiaUtf8(obs, sizeof(obs), doc["obs"] | "", MAX_OBS);
  JsonArray itens = doc["itens"];

  imprimirVia(false, id, mesa, cliente, horario, obs, itens);   // via da cozinha
  imprimirVia(true,  id, mesa, cliente, horario, obs, itens);   // via do caixa

  // publish() reaproveita o buffer do PubSubClient: por isso o status é o ÚLTIMO passo
  publicarStatus(id, "impresso");
}

// ---------- Conexões (sem travar o loop) ----------
void garantirWifi() {
  if (WiFi.status() == WL_CONNECTED) return;
  unsigned long agora = millis();
  if (agora - ultimaTentativaWifi < INTERVALO_WIFI) return;
  ultimaTentativaWifi = agora;

  Serial.println("Wi-Fi caiu, reconectando...");
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASS, WIFI_CANAL);
}

void garantirMqtt() {
  if (mqtt.connected()) return;
  unsigned long agora = millis();
  if (agora - ultimaTentativaMqtt < INTERVALO_MQTT) return;
  ultimaTentativaMqtt = agora;

  String id = "esp32-" + String((uint32_t)esp_random(), HEX);
  Serial.print("Conectando ao MQTT... ");
  if (mqtt.connect(id.c_str())) {
    mqtt.subscribe(TOPIC_PEDIDOS, 1);
    Serial.println("OK, aguardando pedidos...");
  } else {
    Serial.printf("falhou (rc=%d)\n", mqtt.state());
  }
}

// ---------- Setup / Loop ----------
void setup() {
  Serial.begin(115200);

#ifdef IMPRESSORA_BT
  SerialBT.begin("ESP32-pedidos", true);        // true = modo mestre (ESP32 conecta na impressora)
#endif

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS, WIFI_CANAL);
  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < 15000) delay(250);
  Serial.println(WiFi.status() == WL_CONNECTED ? "Wi-Fi OK" : "Wi-Fi indisponivel, tentando em segundo plano");

  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setBufferSize(2048);                     // ~30 itens cabem
  mqtt.setKeepAlive(30);
  mqtt.setCallback(callback);
}

void loop() {
  garantirWifi();
  if (WiFi.status() == WL_CONNECTED) {
    garantirMqtt();
    mqtt.loop();
  }
}
