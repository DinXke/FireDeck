#pragma once

/* Diepe slaap: de processor uit, de radio blijft luisteren.
 *
 * WAT HET OPLEVERT. In diepe slaap trekt de ESP32-S3 microampères in plaats
 * van tientallen milliampères. De SX1262 blijft in ontvangst en trekt DIO1
 * hoog zodra er een pakket binnenkomt; dat is de wekker. Je duty-cyclet dus
 * niet de ontvanger -- die blijft luisteren -- maar de processor die ernaast
 * niets staat te doen.
 *
 * WAT HET KOST, EN WAAROM HIJ STANDAARD UIT STAAT.
 *
 *  - Diepe slaap is geen pauze maar een herstart: setup() draait opnieuw. Het
 *    pakket dat de wekker overhaalde is daarmee vrijwel zeker weg -- je wordt
 *    wakker en luistert weer, maar dat ene bericht heb je gemist.
 *  - Het scherm is uit en opstarten kost seconden. Wie het toestel oppakt ziet
 *    even niets.
 *  - De klok kan verlopen zonder GPS-fix.
 *
 * Voor een toestel dat in een noodgeval moet werken is dat een echte ruil, en
 * die hoort iemand bewust te maken. Vandaar: standaard uit, en de vraag op het
 * scherm zegt wat het kost in plaats van alleen wat het oplevert.
 *
 * HET VANGNET. Naast DIO1 zetten we een tijdwekker. Zonder die wekker zou een
 * toestel waar niemand naar zendt er dood uitzien tot er toevallig verkeer is,
 * en op de BOOT-knop kunnen we niet wekken: die is actief-laag, terwijl de
 * slaaphelper op ANY_HIGH wekt. Dat laatste is op hardware nog na te meten.
 */

#include <Arduino.h>
#if defined(ESP32)
  #include <SPIFFS.h>
#endif

#define FD_SLAAP_PAD      "/firedeck.slaap"
#define FD_SLAAP_MIN_MIN  2       // minder dan twee minuten is onbruikbaar
#define FD_SLAAP_MAX_MIN  120
#define FD_SLAAP_WEKKER_S 900     // elke 15 minuten toch even wakker

class FireDeckSlaap {
  bool     _aan;
  uint16_t _minuten;
  bool     _geladen;

public:
  FireDeckSlaap() : _aan(false), _minuten(10), _geladen(false) { }

  /* Standaard UIT. Een toestel dat uit de doos in slaap valt terwijl niemand
   * daarom vroeg, is een defect toestel in de ogen van wie het draagt. */
  void laad() {
#if defined(ESP32)
    _aan = false;
    _minuten = 10;
    if (SPIFFS.exists(FD_SLAAP_PAD)) {
      File f = SPIFFS.open(FD_SLAAP_PAD, "r");
      if (f) {
        String r = f.readStringUntil('\n');
        f.close();
        r.trim();
        int komma = r.indexOf(',');
        if (komma > 0) {
          _aan = (r.charAt(0) == '1');
          long m = r.substring(komma + 1).toInt();
          if (m >= FD_SLAAP_MIN_MIN && m <= FD_SLAAP_MAX_MIN) _minuten = (uint16_t) m;
        }
      }
    }
#endif
    _geladen = true;
  }

  void bewaar() {
#if defined(ESP32)
    File f = SPIFFS.open(FD_SLAAP_PAD, "w", true);
    if (!f) return;
    f.printf("%d,%u\n", _aan ? 1 : 0, (unsigned) _minuten);
    f.close();
#endif
  }

  bool aan() { if (!_geladen) laad(); return _aan; }
  uint16_t minuten() { if (!_geladen) laad(); return _minuten; }

  void zet(bool aan) { if (!_geladen) laad(); _aan = aan; bewaar(); }

  /* Rondlopen door een handvol waarden in plaats van vrij instellen: op een
   * toestel met handschoenen is kiezen uit vijf sneller dan een getal typen. */
  void volgendeTijd() {
    if (!_geladen) laad();
    static const uint16_t trap[] = { 2, 5, 10, 30, 60, 120 };
    for (unsigned i = 0; i < sizeof(trap)/sizeof(trap[0]); i++) {
      if (_minuten == trap[i]) {
        _minuten = trap[(i + 1) % (sizeof(trap)/sizeof(trap[0]))];
        bewaar();
        return;
      }
    }
    _minuten = 10;
    bewaar();
  }
};
