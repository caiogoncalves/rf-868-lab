// io_relay1: REPETIDOR io-homecontrol (868 MHz) para o dongle RAMSES-ESP (ESP32-S3 + CC1101). TRANSMITE, em potencia baixa.
// Repete SOMENTE quadros com CRC valido de AA0001 (master blind) para BB0002 (TaHoma), no mesmo canal em que foram ouvidos.
// A autenticacao (desafio 3C / resposta 3D) e ponta a ponta: repetir o quadro sem alterar nao exige chave.
// Salvaguardas: filtro de endereco, potencia limitada (PATABLE <= 0x84), cerca de 5 repeticoes por minuto, comando r0 desliga.
// Comandos: r1/r0 liga/desliga o rele, pXX define PATABLE em hex (max 84), v estatisticas, c1/c2/c3 fixa canal, h volta a saltar.
// io_sniffer2c = io_sniffer2b com o medidor de preambulo corrigido (guarda o ultimo trecho alternado LONGO, >= 8 bits).
// io_sniffer2b = io_sniffer2 + mede o tamanho do preambulo (bits alternados antes do sincronismo) e imprime pre=N (26 us por bit).
// io_sniffer2 (salta entre os 3 canais io e fica no canal quando ha atividade; imprime rajadas A mesmo sem decodificar). Base: io_sniffer1.
// io_sniffer1: escuta PASSIVA de Somfy io-homecontrol (868 MHz) com o CC1101 do dongle RAMSES-ESP (ESP32-S3). NAO TRANSMITE.
// CC1101 em modo transparente assincrono: GDO0 (GPIO39) entrega o fluxo cru de bits demodulado (2-FSK, 38,4 kbps, desvio ~19 kHz).
// O SX1276 tem o modo "IoHome" em hardware; o CC1101 nao, entao o enquadramento UART (inicio 0, 8 bits LSB primeiro, parada 1) e feito aqui.
// Formato no ar: preambulo 0x55 repetido, sincronismo 0xFF 0x33 (ambos com enquadramento), byte 0 (& 0x1F = tamanho L), L bytes, 2 bytes de CRC.
// Gravar: --fqbn esp32:esp32:esp32s3:CDCOnBoot=cdc   Comandos (linha): fNNNNNN (kHz), c1/c2/c3 fixa o canal, h volta a saltar, v (estatisticas)
#include <SPI.h>
#include "driver/gpio.h"
#include "esp_cpu.h"

#define PIN_MOSI 35
#define PIN_SCK  36
#define PIN_MISO 37
#define PIN_CSN  38
#define PIN_GDO0 39

#define CC_SRES 0x30
#define CC_SRX 0x34
#define CC_SIDLE 0x36
#define CC_SFRX 0x3A
#define CC_SCAL 0x33
#define CC_R_BURST 0xC0

SPIClass spi(FSPI);
static inline void csLow()  { spi.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0)); digitalWrite(PIN_CSN, LOW); while (digitalRead(PIN_MISO)) {} }
static inline void csHigh() { digitalWrite(PIN_CSN, HIGH); spi.endTransaction(); }
void ccWrite(uint8_t a, uint8_t v) { csLow(); spi.transfer(a & 0x3F); spi.transfer(v); csHigh(); }
uint8_t ccStrobe(uint8_t c) { csLow(); uint8_t s = spi.transfer(c); csHigh(); return s; }
uint8_t ccStatus(uint8_t a) { csLow(); spi.transfer(a | CC_R_BURST); uint8_t v = spi.transfer(0); csHigh(); return v; }
int rssiDbm() { uint8_t r = ccStatus(0x34); return (r >= 128) ? ((int)r - 256) / 2 - 74 : (int)r / 2 - 74; }

// >>> EDIT: these are EXAMPLE addresses. Put the 3-byte addresses of YOUR devices (read them with the sniffer first).
const uint8_t SRC_BLIND[3] = {0xAA, 0x00, 0x01};   // master blind
const uint8_t DST_HUB[3]   = {0xBB, 0x00, 0x02};   // TaHoma
bool relayOn = true; uint8_t paVal = 0x60;          // ~0 dBm em 868 MHz
uint32_t relays = 0, relaysBloq = 0, relaysFalha = 0, holdUntil = 0;
float tokens = 4.0f; uint32_t tokT = 0;             // balde: 4 repeticoes de reserva, 1 a cada 12 s
uint32_t freqKhz = 868250;
const uint32_t CANAIS[3] = {868250, 868950, 869850};
bool saltar = true; uint8_t canalIdx = 0; uint32_t tSalto = 0, tAtivIni = 0; uint32_t rajadas = 0;

void setFreq(uint32_t khz) {
  uint32_t w = (uint32_t)(((uint64_t)khz * 1000ULL * 65536ULL + 13000000ULL) / 26000000ULL);
  ccWrite(0x0D, (w >> 16) & 0xFF); ccWrite(0x0E, (w >> 8) & 0xFF); ccWrite(0x0F, w & 0xFF);
}

void aplica() {
  ccStrobe(CC_SIDLE);
  setFreq(freqKhz);
  ccStrobe(CC_SCAL); delay(2);
  ccStrobe(CC_SRX);
  Serial.printf("# F=%lu kHz\n", (unsigned long)freqKhz);
}

void setPA(uint8_t v) {
  if (v > 0xC0) v = 0xC0;                          // teto: maximo do CC1101 a 868 MHz (~+10 dBm, 10 mW < 25 mW ERP)
  csLow(); spi.transfer(0x7E); spi.transfer(v); for (int i = 0; i < 7; i++) spi.transfer(0); csHigh();
  paVal = v;
}

void sintoniza() { ccStrobe(CC_SIDLE); setFreq(freqKhz); ccStrobe(CC_SRX); }   // calibra sozinho ao entrar em RX (MCSM0=0x18)

void ccConfig() {
  ccStrobe(CC_SRES); delay(10);
  ccWrite(0x00, 0x2E);  // IOCFG2: alta impedancia
  ccWrite(0x02, 0x0D);  // IOCFG0: dados seriais demodulados (modo assincrono)
  ccWrite(0x07, 0x00);  // PKTCTRL1
  ccWrite(0x08, 0x32);  // PKTCTRL0: modo serial assincrono, sem CRC, comprimento infinito
  ccWrite(0x0B, 0x06);  // FSCTRL1: IF ~152 kHz
  ccWrite(0x0C, 0x00);
  ccWrite(0x10, 0xCA);  // MDMCFG4: BW canal ~102 kHz, DRATE_E=10
  ccWrite(0x11, 0x83);  // MDMCFG3: 38,4 kBaud
  ccWrite(0x12, 0x00);  // MDMCFG2: 2-FSK, sem sincronismo
  ccWrite(0x13, 0x22);
  ccWrite(0x14, 0xF8);
  ccWrite(0x15, 0x34);  // DEVIATN: ~19 kHz
  ccWrite(0x17, 0x30);  // MCSM1
  ccWrite(0x18, 0x18);  // MCSM0: calibra ao entrar em RX
  ccWrite(0x19, 0x16);  // FOCCFG
  ccWrite(0x1A, 0x6C);  // BSCFG
  ccWrite(0x1B, 0x43);  // AGCCTRL2
  ccWrite(0x1C, 0x40);
  ccWrite(0x1D, 0x91);
  ccWrite(0x21, 0x56);  // FREND1
  ccWrite(0x22, 0x10);  // FREND0
  ccWrite(0x23, 0xE9); ccWrite(0x24, 0x2A); ccWrite(0x25, 0x00); ccWrite(0x26, 0x1F);  // FSCAL3..0
  ccWrite(0x2C, 0x81); ccWrite(0x2D, 0x35); ccWrite(0x2E, 0x09);  // TEST2..0
  setPA(paVal);
}

// ---- captura de bordas em GDO0 ----
#define RING 8192
volatile uint32_t evT[RING];
volatile uint8_t evL[RING];
volatile uint16_t head = 0, tail = 0;
volatile uint32_t overflow = 0;
void IRAM_ATTR isr() {
  uint16_t h = head, n = (h + 1) & (RING - 1);
  if (n == tail) { overflow++; return; }
  evT[h] = (uint32_t)micros();
  evL[h] = gpio_get_level((gpio_num_t)PIN_GDO0);
  head = n;
}

// ---- decodificador ----
static uint32_t SYNC_BITS = 0;       // 12 bits de preambulo + 20 bits de sincronismo framado
static void buildSync() {
  // sequencia no tempo: 0x555 (12 bits) | 0 + FF(LSB primeiro) + 1 | 0 + 33(LSB primeiro) + 1
  uint32_t v = 0; auto push = [&](int b) { v = (v << 1) | (b & 1); };
  for (int i = 0; i < 12; i++) push(i & 1);           // 0,1,0,1,...,0,1
  auto framed = [&](uint8_t byte) { push(0); for (int i = 0; i < 8; i++) push((byte >> i) & 1); push(1); };
  framed(0xFF); framed(0x33);
  SYNC_BITS = v;
}

uint32_t sr = 0;
enum { BUSCA, BYTES } estado = BUSCA;
uint8_t frame[80]; uint8_t nBytes = 0, total = 0; uint16_t cur = 0; uint8_t nb = 0; int rssiQuadro = 0;
uint32_t quadros = 0, validos = 0, bitsTotal = 0;
uint32_t altRun = 0, lastAltLen = 0, lastLongAlt = 0, preBits = 0; int lastBit = -1;

uint16_t crc16(const uint8_t *d, int n, uint16_t poly, uint16_t init, bool refl) {
  uint16_t c = init;
  for (int i = 0; i < n; i++) {
    if (refl) { c ^= d[i]; for (int k = 0; k < 8; k++) c = (c & 1) ? (c >> 1) ^ poly : (c >> 1); }
    else      { c ^= (uint16_t)d[i] << 8; for (int k = 0; k < 8; k++) c = (c & 0x8000) ? (c << 1) ^ poly : (c << 1); }
  }
  return c;
}

// ---- transmissao (modo transparente assincrono: o CC1101 modula o nivel que o ESP32 coloca em GDO0) ----
static uint8_t txbits[11000]; static int ntx;
static void addBit(int b) { if (ntx < (int)sizeof(txbits)) txbits[ntx++] = (uint8_t)b; }
static void addFramed(uint8_t v) { addBit(0); for (int i = 0; i < 8; i++) addBit((v >> i) & 1); addBit(1); }

bool IRAM_ATTR relayTx(const uint8_t *d, int n, uint32_t tFimRx, uint32_t *latUs, uint32_t *txUs, int preBytes = 10) {
  ntx = 0;
  for (int i = 0; i < preBytes; i++) addFramed(0x55);   // preambulo: bits alternados
  addFramed(0xFF); addFramed(0x33);                // sincronismo
  for (int i = 0; i < n; i++) addFramed(d[i]);     // quadro como recebido (inclui o CRC)
  for (int i = 0; i < 4; i++) addBit(1);           // inatividade final
  detachInterrupt(digitalPinToInterrupt(PIN_GDO0));
  ccStrobe(CC_SIDLE);
  pinMode(PIN_GDO0, OUTPUT); gpio_set_level((gpio_num_t)PIN_GDO0, 1);
  ccStrobe(0x35);                                  // STX (auto-calibra ao sair de IDLE)
  uint32_t t0 = micros(); bool ok = false;
  while (micros() - t0 < 4000) { if ((ccStatus(0x35) & 0x1F) == 0x13) { ok = true; break; } }   // MARCSTATE = TX
  if (ok) {
    uint32_t hz = (uint32_t)getCpuFrequencyMhz() * 1000000UL;
    *latUs = micros() - tFimRx;
    uint32_t c0 = esp_cpu_get_cycle_count();
    int iIrq = ntx - 400;                          // so os ultimos bits (sync+quadro) sem interrupcoes; o preambulo longo tolera jitter e evita o WDT
    if (iIrq < 0) noInterrupts();
    for (int i = 0; i < ntx; i++) {
      if (i == iIrq && iIrq >= 0) noInterrupts();
      uint32_t alvo = c0 + (uint32_t)((uint64_t)i * hz / 38400ULL);
      while ((int32_t)(esp_cpu_get_cycle_count() - alvo) < 0) {}
      gpio_set_level((gpio_num_t)PIN_GDO0, txbits[i]);
    }
    uint32_t fim = c0 + (uint32_t)((uint64_t)ntx * hz / 38400ULL);
    while ((int32_t)(esp_cpu_get_cycle_count() - fim) < 0) {}
    interrupts();
    *txUs = (uint32_t)((uint64_t)ntx * 1000000ULL / 38400ULL);
  }
  ccStrobe(CC_SIDLE);
  pinMode(PIN_GDO0, INPUT);
  ccStrobe(CC_SFRX); ccStrobe(CC_SRX);
  tail = head;                                     // descarta bordas geradas durante a transmissao
  attachInterrupt(digitalPinToInterrupt(PIN_GDO0), isr, CHANGE);
  return ok;
}

void fimQuadro() {
  uint32_t tFimRx = micros();
  quadros++;
  int n = nBytes - 2;
  uint16_t rx_be = ((uint16_t)frame[n] << 8) | frame[n + 1], rx_le = ((uint16_t)frame[n + 1] << 8) | frame[n];
  struct V { const char *nome; uint16_t poly, init, xo; bool refl; } vs[] = {
    {"CCITT-FALSE", 0x1021, 0xFFFF, 0, false}, {"XMODEM", 0x1021, 0x0000, 0, false}, {"KERMIT", 0x8408, 0x0000, 0, true},
    {"X25", 0x8408, 0xFFFF, 0xFFFF, true}, {"MCRF4XX", 0x8408, 0xFFFF, 0, true}, {"CCITT-FALSE-REFL", 0x8408, 0xFFFF, 0, true}};
  const char *achou = "crc?";
  for (auto &v : vs) {
    uint16_t c = crc16(frame, n, v.poly, v.init, v.refl) ^ v.xo;
    if (c == rx_be || c == rx_le) { achou = v.nome; validos++; break; }
  }
  bool crcOk = (strcmp(achou, "crc?") != 0);
  bool deCega = nBytes >= 9 && memcmp(frame + 5, SRC_BLIND, 3) == 0;
  bool paraHub = nBytes >= 9 && memcmp(frame + 2, DST_HUB, 3) == 0;
  if (nBytes >= 9 && (deCega || memcmp(frame + 2, SRC_BLIND, 3) == 0)) holdUntil = millis() + 250;   // fica no canal
  const char *rel = ""; uint32_t latUs = 0, txUs = 0;
  if (relayOn && crcOk && deCega && paraHub && rssiQuadro >= -92) {
    uint32_t agora = millis();
    tokens += (float)(agora - tokT) / 12000.0f; if (tokens > 4.0f) tokens = 4.0f; tokT = agora;
    bool canalOk = (freqKhz == 868250 || freqKhz == 868950 || freqKhz == 869850);
    if (!canalOk) { rel = " rele=canal"; }
    else if (tokens < 1.0f) { relaysBloq++; rel = " rele=limite"; }
    else {
      tokens -= 1.0f;
      uint8_t copia[80]; memcpy(copia, frame, nBytes); int nCopia = nBytes;
      if (relayTx(copia, nCopia, tFimRx, &latUs, &txUs)) { relays++; rel = " rele=OK"; } else { relaysFalha++; rel = " rele=FALHA"; }
    }
  }
  Serial.printf("Q %lu f=%lu rssi=%d pre=%lu len=%d crc=%s%s lat=%luus tx=%luus hex=", (unsigned long)millis(), (unsigned long)freqKhz, rssiQuadro, (unsigned long)preBits, nBytes, achou, rel, (unsigned long)latUs, (unsigned long)txUs);
  for (int i = 0; i < nBytes; i++) Serial.printf("%02X", frame[i]);
  Serial.println();
}

void pushBit(uint8_t b) {
  bitsTotal++;
  if (lastBit >= 0 && (int)b != lastBit) altRun++; else { if (altRun >= 8) lastLongAlt = altRun; altRun = 1; }
  lastBit = b;
  sr = (sr << 1) | b;
  if (estado == BUSCA) {
    if (sr == SYNC_BITS) { preBits = lastLongAlt; estado = BYTES; nBytes = 0; total = 0; cur = 0; nb = 0; rssiQuadro = rssiDbm(); }
    return;
  }
  cur |= (uint16_t)b << nb; nb++;                  // bit 0 = inicio, 1..8 = dados LSB primeiro, 9 = parada
  if (nb < 10) return;
  bool ok = ((cur & 1) == 0) && ((cur & 0x200) != 0);
  uint8_t val = (cur >> 1) & 0xFF; cur = 0; nb = 0;
  if (!ok) { estado = BUSCA; return; }
  if (nBytes < sizeof(frame)) frame[nBytes++] = val;
  if (nBytes == 1) { total = 1 + (val & 0x1F) + 2; if (total > 64) { estado = BUSCA; return; } }
  if (nBytes >= total) { fimQuadro(); estado = BUSCA; }
}

uint32_t prevT = 0; bool prevOk = false;
void processa() {
  while (tail != head) {
    uint32_t t = evT[tail]; uint8_t l = evL[tail]; tail = (tail + 1) & (RING - 1);
    if (!prevOk) { prevT = t; prevOk = true; continue; }
    uint32_t dt = t - prevT; prevT = t;
    uint8_t nivelAntes = l ? 0 : 1;               // nivel durante a corrida que terminou
    if (dt > 2000) { estado = BUSCA; sr = 0; continue; }   // pausa: fim de quadro ou ruido
    int nbits = (int)(((uint64_t)dt * 1000 + 13020) / 26040);
    if (nbits < 1) continue;                      // glitch
    if (nbits > 24) nbits = 24;
    for (int i = 0; i < nbits; i++) pushBit(nivelAntes);
  }
}

String linha;
void comando(const String &c) {
  if (c == "v") { Serial.printf("# quadros=%lu validos=%lu bits=%lu overflow=%lu rssi=%d reles=%lu bloqueados=%lu falhas=%lu rele=%d pa=0x%02X tokens=%.1f\n", (unsigned long)quadros, (unsigned long)validos, (unsigned long)bitsTotal, (unsigned long)overflow, rssiDbm(), (unsigned long)relays, (unsigned long)relaysBloq, (unsigned long)relaysFalha, (int)relayOn, paVal, tokens); return; }
  if (c == "r1") { relayOn = true; Serial.println("# rele LIGADO"); return; }
  if (c == "r0") { relayOn = false; Serial.println("# rele DESLIGADO"); return; }
  if (c.length() > 1 && c[0] == 't') {             // t<hex do quadro inteiro, com CRC>: transmite 1x, so para a pergola
    static const uint8_t DST_PERGOLA[3] = {0xCC, 0x00, 0x03};
    uint8_t q[40]; int nq = 0; String h = c.substring(1); int preB = 20;
    if (h.length() && h[0] == '@') { int d = h.indexOf(':'); if (d > 0) { preB = h.substring(1, d).toInt(); h = h.substring(d + 1); } }
    if (preB < 4) preB = 4; if (preB > 1000) preB = 1000;
    for (int i = 0; i + 1 < (int)h.length() && nq < 40; i += 2) q[nq++] = (uint8_t)strtol(h.substring(i, i + 2).c_str(), nullptr, 16);
    if (nq < 9 || (memcmp(q + 2, DST_PERGOLA, 3) != 0 && memcmp(q + 2, SRC_BLIND, 3) != 0)) { Serial.println("# t recusado: destino nao e pergola nem blind"); return; }
    if (saltar) { Serial.println("# t recusado: fixe o canal antes (c1/c2/c3)"); return; }
    uint32_t agora = millis();
    tokens += (float)(agora - tokT) / 12000.0f; if (tokens > 4.0f) tokens = 4.0f; tokT = agora;
    if (tokens < 1.0f) { Serial.println("# t recusado: limite de taxa"); return; }
    tokens -= 1.0f;
    uint32_t latUs = 0, txUs = 0; uint32_t t0 = micros();
    bool ok = relayTx(q, nq, t0, &latUs, &txUs, preB);
    holdUntil = millis() + 400;                    // fica no canal para ouvir a resposta
    Serial.printf("# T %s f=%lu n=%d tx=%luus\n", ok ? "OK" : "FALHA", (unsigned long)freqKhz, nq, (unsigned long)txUs);
    return;
  }
  if (c.length() > 1 && c[0] == 'p') { setPA((uint8_t)strtol(c.c_str() + 1, nullptr, 16)); Serial.printf("# PATABLE=0x%02X\n", paVal); return; }
  if (c == "c1") { saltar = false; freqKhz = 868250; aplica(); return; }
  if (c == "c2") { saltar = false; freqKhz = 868950; aplica(); return; }
  if (c == "c3") { saltar = false; freqKhz = 869850; aplica(); return; }
  if (c == "h") { saltar = true; Serial.println("# saltando entre os 3 canais"); return; }
  if (c.length() > 1 && c[0] == 'f') { long v = c.substring(1).toInt(); if (v >= 860000 && v <= 870000) { saltar = false; freqKhz = v; aplica(); return; } }
  Serial.println("# comando invalido (fNNNNNN kHz, c1/c2/c3, v)");
}

void setup() {
  Serial.begin(115200); delay(1500);
  buildSync();
  pinMode(PIN_CSN, OUTPUT); digitalWrite(PIN_CSN, HIGH); pinMode(PIN_GDO0, INPUT);
  spi.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_CSN);
  ccConfig();
  Serial.printf("# io_sniffer1 pronto. VERSION=%02X (esperado 14). Somente escuta.\n", ccStatus(0x31));
  aplica();
  attachInterrupt(digitalPinToInterrupt(PIN_GDO0), isr, CHANGE);
}

uint32_t ultStat = 0;
void salto() {
  if (!saltar || estado != BUSCA || millis() < holdUntil) return;
  uint32_t agora = millis();
  if (agora - tSalto < 100) return;
  tSalto = agora;
  int r = rssiDbm();
  if (r > -85) {                                   // atividade: fica no canal (no maximo 3 s seguidos)
    if (tAtivIni == 0 || agora - tAtivIni > 4000) tAtivIni = agora;
    if (agora - tAtivIni < 3000) { rajadas++; Serial.printf("A %lu f=%lu rssi=%d\n", (unsigned long)agora, (unsigned long)freqKhz, r); return; }
  } else tAtivIni = 0;
  canalIdx = (canalIdx + 1) % 3; freqKhz = CANAIS[canalIdx];
  sintoniza(); sr = 0; prevOk = false; altRun = 0; lastBit = -1; lastLongAlt = 0;
}

void loop() {
  processa();
  salto();
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\n' || ch == '\r') { if (linha.length()) { comando(linha); linha = ""; } } else if (linha.length() < 96) linha += ch;
  }
  if (millis() - ultStat > 10000) { ultStat = millis(); Serial.printf("# t=%lus quadros=%lu validos=%lu overflow=%lu rssi=%d\n", (unsigned long)(millis() / 1000), (unsigned long)quadros, (unsigned long)validos, (unsigned long)overflow, rssiDbm()); }
}
