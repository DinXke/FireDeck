#pragma once

/* De meldingslijst komt van het bestandssysteem, niet uit de firmware.
 *
 * WAAROM. Als een korps zijn meldingslijst wijzigt -- en dat gebeurt, want
 * die lijst is het resultaat van hoe ze werken en niet van hoe wij programmeren
 * -- dan mag dat geen hercompilatie kosten en al helemaal geen herflash van
 * tientallen toestellen. Het toestel leest zijn lijst dus bij het opstarten van
 * SPIFFS, en valt terug op de ingebouwde lijst als er niets staat. Een vers
 * toestel werkt daardoor meteen, en een geprovisioneerd toestel werkt zoals het
 * korps het wil.
 *
 * WAAROM GEEN JSON. Er zit geen JSON-bibliotheek in deze boom, en er een
 * bijhalen om op een microcontroller een boomstructuur te ontleden is de
 * verkeerde kant op optimaliseren. De beheersite en de simulator wisselen wél
 * JSON uit -- daar is rekenkracht -- en zetten dat om naar dit regelformaat:
 *
 *   D|1|Bericht|lijst|app:msg
 *   D|1|Uitgerukt|pomp|type:UIT
 *   D|2|Accu|accu|app:accu
 *   T|UIT|Uitgerukt|pomp
 *   R|dm|Dispatch|b405ac3d0aadffea
 *   R|room|Ploeg 04|2cb0c5eb47375780
 *   F|Voertuig|Autopomp 1;Autopomp 2;Ladderwagen
 *   F|Bemanning|2;4;6;8
 *   X|Vrije tekst|24
 *   T|TPL|Ter plaatse|brandgebouw
 *   F|Toegang|Vrij;Belemmerd
 *
 * D beschrijft een tegel op het dashboard: paginanummer, opschrift, icoon en
 * wat hij opent -- een ingebouwde app (app:...) of meteen een meldingstype
 * (type:...). Zo bepaalt het korps niet alleen wat er achter de knoppen zit
 * maar ook welke knoppen er zijn. Staat er geen enkele D-regel, dan valt het
 * toestel terug op zijn standaardraster.
 *
 * R zet een ontvanger achter het lopende type: soort, opschrift en het begin
 * van de publieke sleutel. Meerdere R-regels betekent meerdere ontvangers --
 * een melding mag tegelijk naar dispatch en naar de ploegroom. Het toestel
 * zoekt bij het verzenden op sleutel op in zijn contactenlijst, niet op
 * volgnummer: die lijst staat op elk toestel in een andere volgorde.
 *
 * T begint een type, F voegt een keuzeveld toe aan het lopende type, X een
 * vrijetekstveld. Regels die niet met een bekende letter beginnen worden
 * overgeslagen, zodat commentaar en toekomstige velden een oude firmware niet
 * omver halen.
 *
 * HET GEHEUGEN. Het bestand wordt één keer ingelezen en blijft staan; de
 * wijzers in de tabel wijzen erin, en de scheidingstekens worden ter plaatse
 * vervangen door nulbytes. Geen string per optie, geen versnippering.
 */

#include <Arduino.h>
#if defined(ESP32)
  #include <SPIFFS.h>
#endif

#define FD_MAX_TYPES     8
#define FD_MAX_VELDEN    4
#define FD_MAX_OPTIES    8
#define FD_PAD           "/firedeck.fdt"
#define FD_MAX_BESTAND   4096      // ruim; acht types met acht opties blijft hieronder

struct FdVeld {
  const char* label;
  const char* opties[FD_MAX_OPTIES];
  uint8_t     aantal;
  bool        vrij;     // vrijetekstveld
  uint8_t     maxlen;
};

#define FD_MAX_TEGELS   18      // twee pagina's van negen

struct FdTegel {
  uint8_t     pagina;     // 1 of 2
  const char* label;
  const char* icoon;
  const char* actie;      // "app:msg", "type:UIT", ...
};

#define FD_MAX_ONTVANGERS  4
#define FD_SLEUTEL_HEX    64

struct FdOntvanger {
  const char* soort;    // "dm" of "room"; alleen voor de weergave
  const char* label;
  const char* sleutel;  // hex, begin van de publieke sleutel volstaat
};

struct FdType {
  const char* code;
  const char* label;
  const char* icoon;
  uint8_t     aantal;
  FdVeld      velden[FD_MAX_VELDEN];
  uint8_t     ontvangers;
  FdOntvanger ontvanger[FD_MAX_ONTVANGERS];
};

class FireDeckTemplate {
  FdType   _lijst[FD_MAX_TYPES];
  uint8_t  _aantal;
  FdTegel  _tegels[FD_MAX_TEGELS];
  uint8_t  _tegelaantal;
  char*    _buf;         // het hele bestand; de wijzers hierboven wijzen erin
  bool     _uit_bestand;

  /* Knipt op het scheidingsteken door er een nulbyte van te maken, en geeft
   * het begin van het volgende stuk terug. Nooit voorbij het einde. */
  static char* knip(char* p, char scheiding) {
    if (p == NULL) return NULL;
    char* q = strchr(p, scheiding);
    if (q == NULL) return NULL;
    *q = 0;
    return q + 1;
  }

  void ingebouwd();      // de terugval, onderaan gedefinieerd

  bool leesBestand() {
#if defined(ESP32)
    if (!SPIFFS.exists(FD_PAD)) return false;
    File f = SPIFFS.open(FD_PAD, "r");
    if (!f) return false;
    size_t n = f.size();
    if (n == 0 || n > FD_MAX_BESTAND) { f.close(); return false; }

    _buf = (char*) malloc(n + 1);
    if (_buf == NULL) { f.close(); return false; }
    size_t gelezen = f.read((uint8_t*) _buf, n);
    f.close();
    _buf[gelezen] = 0;

    _aantal = 0;
    _tegelaantal = 0;
    char* regel = _buf;
    while (regel != NULL && *regel && _aantal <= FD_MAX_TYPES) {
      char* volgende = strchr(regel, '\n');
      if (volgende) { *volgende = 0; volgende++; }

      // \r van een Windows-regeleinde weghalen, anders zit hij in de laatste waarde
      size_t len = strlen(regel);
      while (len && (regel[len-1] == '\r' || regel[len-1] == ' ')) { regel[--len] = 0; }

      if (len >= 2 && regel[1] == '|') {
        char soort = regel[0];
        char* rest = regel + 2;

        if (soort == 'D') {
          if (_tegelaantal < FD_MAX_TEGELS) {
            FdTegel& g = _tegels[_tegelaantal];
            char* p = knip(rest, '|');
            long pg = strtol(rest, NULL, 10);
            g.pagina = (pg == 2) ? 2 : 1;
            g.label = (p ? p : "");
            char* q = knip(p, '|');
            g.icoon = (q ? q : "lijst");
            char* r = knip(q, '|');
            g.actie = (r ? r : "app:msg");
            _tegelaantal++;
          }

        } else if (soort == 'T') {
          if (_aantal >= FD_MAX_TYPES) { regel = volgende; continue; }
          FdType& t = _lijst[_aantal];
          t.code = rest;
          char* p = knip(rest, '|');
          t.label = (p ? p : "");
          char* q = knip(p, '|');
          t.icoon = (q ? q : "lijst");
          t.aantal = 0;
          t.ontvangers = 0;
          _aantal++;

        } else if (soort == 'R' && _aantal > 0) {
          FdType& t = _lijst[_aantal - 1];
          if (t.ontvangers < FD_MAX_ONTVANGERS) {
            FdOntvanger& o = t.ontvanger[t.ontvangers];
            o.soort = rest;
            char* p = knip(rest, '|');
            o.label = (p ? p : "");
            char* q = knip(p, '|');
            o.sleutel = (q ? q : "");
            if (o.sleutel[0]) t.ontvangers++;   // zonder sleutel valt er niets te sturen
          }

        } else if ((soort == 'F' || soort == 'X') && _aantal > 0) {
          FdType& t = _lijst[_aantal - 1];
          if (t.aantal >= FD_MAX_VELDEN) { regel = volgende; continue; }
          FdVeld& v = t.velden[t.aantal];
          v.label = rest;
          char* p = knip(rest, '|');
          v.aantal = 0;
          v.vrij = (soort == 'X');
          v.maxlen = 24;

          if (v.vrij) {
            if (p) { long m = strtol(p, NULL, 10); if (m > 0 && m <= 120) v.maxlen = (uint8_t) m; }
          } else {
            // De opties, gescheiden door puntkomma's, ter plaatse geknipt.
            while (p && *p && v.aantal < FD_MAX_OPTIES) {
              v.opties[v.aantal++] = p;
              p = knip(p, ';');
            }
            if (v.aantal == 0) { regel = volgende; continue; }   // veld zonder keuzes slaan we over
          }
          t.aantal++;
        }
      }
      regel = volgende;
    }

    if (_aantal == 0) { free(_buf); _buf = NULL; return false; }
    _uit_bestand = true;
    return true;
#else
    return false;
#endif
  }

public:
  FireDeckTemplate() : _aantal(0), _tegelaantal(0), _buf(NULL), _uit_bestand(false) { }

  void begin() {
    if (!leesBestand()) ingebouwd();
  }

  uint8_t aantal() const { return _aantal; }
  uint8_t tegelAantal() const { return _tegelaantal; }
  const FdTegel& tegel(uint8_t i) const { return _tegels[i]; }
  /* Geen enkele tegel in het bestand? Dan bepaalt het toestel zijn eigen
     raster -- beter een standaardscherm dan een leeg scherm. */
  bool eigenDashboard() const { return _tegelaantal > 0; }
  int typeVoorCode(const char* code) const {
    for (uint8_t i = 0; i < _aantal; i++) {
      if (strcmp(_lijst[i].code, code) == 0) return i;
    }
    return -1;
  }
  const FdType& type(uint8_t i) const { return _lijst[i < _aantal ? i : 0]; }
  bool uitBestand() const { return _uit_bestand; }

  /* Wat een volledig ingevuld bericht van dit type maximaal kost. Handig op
   * het toestel zelf: zendtijd is het enige wat op een gedeelde band echt
   * schaars is. */
  uint16_t maxBytes(uint8_t i) const {
    if (i >= _aantal) return 0;
    const FdType& t = _lijst[i];
    uint16_t n = 2;
    for (uint8_t k = 0; k < t.aantal; k++) {
      n += t.velden[k].vrij ? (2 + t.velden[k].maxlen) : 1;
    }
    return n;
  }
};

/* ------------------------------------------------------------------ terugval
 * Een toestel dat nog niet geprovisioneerd is moet toch werken. Deze lijst is
 * bewust kort: ze is bedoeld om mee te kunnen testen, niet om een korps mee te
 * laten draaien. */
inline void FireDeckTemplate::ingebouwd() {
  static const char* UIT_V[] = {"Autopomp 1","Autopomp 2","Ladderwagen","Tankwagen"};
  static const char* UIT_B[] = {"2","4","6","8"};
  static const char* TPL_S[] = {"Niets te zien","Rookontwikkeling","Vlammen zichtbaar","Loos alarm"};
  static const char* ASS_W[] = {"2e autopomp","Ademluchtploeg","Ziekenwagen","Politie"};
  static const char* RET_B[] = {"Direct inzetbaar","Na reiniging","Buiten dienst"};

  _aantal = 0;
  _tegelaantal = 0;          // geen tegels: het toestel gebruikt zijn eigen raster
  FdType& a = _lijst[_aantal++];
  a.code = "UIT"; a.label = "Uitgerukt"; a.icoon = "pomp"; a.aantal = 2;
  a.ontvangers = 0;
  a.velden[0].label = "Voertuig";  a.velden[0].aantal = 4; a.velden[0].vrij = false; a.velden[0].maxlen = 0;
  for (int i = 0; i < 4; i++) a.velden[0].opties[i] = UIT_V[i];
  a.velden[1].label = "Bemanning"; a.velden[1].aantal = 4; a.velden[1].vrij = false; a.velden[1].maxlen = 0;
  for (int i = 0; i < 4; i++) a.velden[1].opties[i] = UIT_B[i];

  FdType& b = _lijst[_aantal++];
  b.code = "TPL"; b.label = "Ter plaatse"; b.icoon = "brandgebouw"; b.aantal = 1;
  b.ontvangers = 0;
  b.velden[0].label = "Wat zie je"; b.velden[0].aantal = 4; b.velden[0].vrij = false; b.velden[0].maxlen = 0;
  for (int i = 0; i < 4; i++) b.velden[0].opties[i] = TPL_S[i];

  FdType& c = _lijst[_aantal++];
  c.code = "ASS"; c.label = "Assistentie"; c.icoon = "bijlen"; c.aantal = 1;
  c.ontvangers = 0;
  c.velden[0].label = "Wat"; c.velden[0].aantal = 4; c.velden[0].vrij = false; c.velden[0].maxlen = 0;
  for (int i = 0; i < 4; i++) c.velden[0].opties[i] = ASS_W[i];

  FdType& d = _lijst[_aantal++];
  d.code = "RET"; d.label = "Retour post"; d.icoon = "kazerne"; d.aantal = 1;
  d.ontvangers = 0;
  d.velden[0].label = "Beschikbaar"; d.velden[0].aantal = 3; d.velden[0].vrij = false; d.velden[0].maxlen = 0;
  for (int i = 0; i < 3; i++) d.velden[0].opties[i] = RET_B[i];

  _uit_bestand = false;
}
