#pragma once

/* FireDeck -- voorgebouwde ploegberichten op de T-Deck Pro.
 *
 * WAAROM DIT SCHERM BESTAAT. Een ploeg met handschoenen aan en een helm op
 * typt geen zinnen. Ze kiest een meldingstype en daarna een handvol
 * standaardwaarden, en dat is het. Elke keuze is precies EEN byte op de lucht
 * -- het verschil met een getypte zin is een factor zes in zendtijd, en op een
 * gedeelde band is zendtijd het enige wat echt schaars is.
 *
 * WAAROM LETTERS EN GEEN CIJFERS. Op het BlackBerry-toetsenbord van dit bord
 * (TCA8418) liggen de cijfers op de alt-laag: W=1, E=2, S=4, Z=7. Een cijfer
 * kiezen kost twee aanslagen, een letter een. Daarom draagt elke regel een
 * letter en niet een nummer.
 *
 * WAAROM ZO WEINIG SCHERMWISSELS. E-ink ververst traag en volledig. Elke stap
 * die je toevoegt is een flits van een halve seconde waarin niemand iets kan
 * lezen. Vandaar: een lijst per veld, geen scrollende formulieren, en na het
 * laatste veld meteen het bevestigingsscherm.
 */

#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include <MeshCore.h>

class UITask;
class MyMesh;
extern MyMesh the_mesh;

#include "FireDeckTemplate.h"

/* De meldingslijst komt van het bestandssysteem; zie FireDeckTemplate.h.
 * Zo kost een nieuwe lijst geen hercompilatie en geen herflash. */
extern FireDeckTemplate fd_template;

#define FD_TEKST_MAX 140

class FireDeckScreen : public UIScreen {
  UITask*  _task;
  mesh::RTCClock* _rtc;      // voor $tijd en $datum
  uint8_t  _stap;          // 0 = typelijst, 1..n = veld, 100 = bevestigen, 101 = verstuurd
  uint8_t  _type;
  uint8_t  _veld;
  uint8_t  _keuze[FD_MAX_VELDEN];
  int      _bestemming;    // contact-index; -1 = nog niet ingesteld
  bool     _gelukt;
  uint8_t  _weg;        // hoeveel ontvangers het werkelijk haalden
  uint8_t  _gemist;     // en hoeveel niet
  uint8_t  _open;       // bitmasker: welke ontvangers het NIET haalden
  uint8_t  _poging;     // hoeveelste poging voor dit bericht
  char     _regel[FD_TEKST_MAX];

  static const uint8_t STAP_TYPES = 0;
  static const uint8_t STAP_OK    = 100;
  static const uint8_t STAP_WEG   = 101;

  /* Voegt tekst toe aan _regel en vult variabelen in.
   *
   * De invulling gebeurt hier, op het moment van verzenden, en niet in de
   * template: zo blijft een template klein en toch actueel. Let op het
   * verschil in kosten -- "$tijd" is vijf tekens in de template en vijf in de
   * lucht, maar de ontvanger weet er wel het uur door.
   *
   * Een onbekende variabele laten we staan zoals ze is. Stil weglaten zou
   * betekenen dat een tikfout in de editor pas opvalt als er een melding
   * binnenkomt waar iets in ontbreekt. */
  void voegToe(const char* tekst) {
    if (tekst == NULL) return;
    size_t over = sizeof(_regel) - strlen(_regel) - 1;
    char* uit = _regel + strlen(_regel);

    while (*tekst && over > 0) {
      if (*tekst != '$') { *uit++ = *tekst++; over--; continue; }

      char waarde[24];
      waarde[0] = 0;
      int lengte = 0;

      if (strncmp(tekst, "$tijd", 5) == 0) {
        lengte = 5;
        uint32_t nu = _rtc ? _rtc->getCurrentTime() : 0;
        if (nu > 0) snprintf(waarde, sizeof(waarde), "%02u:%02u",
                             (unsigned)((nu / 3600) % 24), (unsigned)((nu / 60) % 60));
      } else if (strncmp(tekst, "$datum", 6) == 0) {
        lengte = 6;
        uint32_t nu = _rtc ? _rtc->getCurrentTime() : 0;
        if (nu > 0) {
          /* Dagen sinds 1970 omrekenen naar dag en maand. Geen tm-structuur:
           * die trekt op deze bouw localtime en tijdzones mee naar binnen voor
           * twee getallen. */
          uint32_t d = nu / 86400UL;
          int jaar = 1970;
          while (true) {
            bool schrikkel = (jaar % 4 == 0 && (jaar % 100 != 0 || jaar % 400 == 0));
            uint32_t in_jaar = schrikkel ? 366 : 365;
            if (d < in_jaar) break;
            d -= in_jaar; jaar++;
          }
          bool schrikkel = (jaar % 4 == 0 && (jaar % 100 != 0 || jaar % 400 == 0));
          static const uint8_t lengtes[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
          int maand = 0;
          while (maand < 12) {
            uint32_t n = lengtes[maand] + ((maand == 1 && schrikkel) ? 1 : 0);
            if (d < n) break;
            d -= n; maand++;
          }
          snprintf(waarde, sizeof(waarde), "%02u/%02u",
                   (unsigned)(d + 1), (unsigned)(maand + 1));
        }
      } else if (strncmp(tekst, "$naam", 5) == 0) {
        lengte = 5;
        NodePrefs* p = the_mesh.getNodePrefs();
        if (p) snprintf(waarde, sizeof(waarde), "%s", p->node_name);
      } else if (strncmp(tekst, "$id", 3) == 0) {
        lengte = 3;
        uint8_t* pk = the_mesh.self_id.pub_key;
        snprintf(waarde, sizeof(waarde), "%02x%02x%02x", pk[0], pk[1], pk[2]);
      }

      if (lengte == 0) {            // geen bekende variabele: laten staan
        *uit++ = *tekst++; over--;
        continue;
      }
      tekst += lengte;
      for (const char* w = waarde; *w && over > 0; w++) { *uit++ = *w; over--; }
    }
    *uit = 0;
  }

  /* De menselijke regel die de lucht in gaat. Kort houden: elke byte telt,
   * en aan de andere kant leest een mens dit op een klein scherm. */
  void bouwRegel() {
    const FdType& t = fd_template.type(_type);
    _regel[0] = 0;
    voegToe(t.label);
    for (uint8_t i = 0; i < t.aantal; i++) {
      voegToe(" | ");
      voegToe(t.velden[i].opties[_keuze[i]]);
    }
  }

  /* Een ontvanger staat in de template met zijn publieke sleutel, niet met een
   * volgnummer: de contactenlijst staat op elk toestel in een andere volgorde,
   * en die verschuift bovendien zodra er een nieuwe node gehoord wordt. Een
   * prefix volstaat -- acht bytes is ruim voorbij toeval. */
  static int contactVoorSleutel(const char* hex) {
    if (hex == NULL || hex[0] == 0) return -1;
    uint8_t zoek[PUB_KEY_SIZE];
    int n = 0;
    while (hex[0] && hex[1] && n < PUB_KEY_SIZE) {
      char paar[3] = { hex[0], hex[1], 0 };
      char* eind = NULL;
      long v = strtol(paar, &eind, 16);
      if (eind != paar + 2) break;
      zoek[n++] = (uint8_t) v;
      hex += 2;
    }
    if (n == 0) return -1;

    ContactInfo c;
    int aantal = the_mesh.getNumContacts();
    for (int i = 0; i < aantal; i++) {
      if (!the_mesh.getContactByIdx((uint32_t) i, c)) continue;
      if (memcmp(c.id.pub_key, zoek, n) == 0) return i;
    }
    return -1;
  }

  /* Stuurt naar elke ontvanger van dit type. Meerdere mag: een melding gaat
   * vaak tegelijk naar dispatch en naar de ploegroom. Een room server is voor
   * MeshCore gewoon een contact, dus dezelfde weg.
   *
   * De teller telt wat er ECHT weg is, niet wat er ingesteld staat -- een
   * ontvanger die dit toestel nog nooit gehoord heeft, kan het niet bereiken,
   * en dat hoort op het scherm te staan. */
  uint8_t stuurNaarOntvangers(bool alleen_open = false) {
    const FdType& t = fd_template.type(_type);
    uint8_t weg = 0;
    _gemist = 0;
    uint8_t nog_open = 0;

    if (t.ontvangers == 0) {
      // Niets ingesteld: de bestemming uit provisioning, als die er is.
      if (_bestemming >= 0 && the_mesh.uiSendDirectMessage((uint32_t) _bestemming, _regel)) weg++;
      else { _gemist++; nog_open |= 1; }
      _open = nog_open;
      return weg;
    }

    for (uint8_t i = 0; i < t.ontvangers; i++) {
      /* Bij een nieuwe poging slaan we over wie het al kreeg: nog eens sturen
       * is zendtijd weggooien en de ontvanger twee keer laten piepen. */
      if (alleen_open && !(_open & (1 << i))) continue;

      int idx = contactVoorSleutel(t.ontvanger[i].sleutel);
      if (idx < 0) { _gemist++; nog_open |= (1 << i); continue; }
      if (the_mesh.uiSendDirectMessage((uint32_t) idx, _regel)) weg++;
      else { _gemist++; nog_open |= (1 << i); }
    }
    _open = nog_open;
    return weg;
  }

  void kop(DisplayDriver& d, const char* titel, const char* onder) {
    d.setTextSize(1);
    d.setCursor(4, 2);
    d.print(titel);
    if (onder) {
      d.setCursor(4, 14);
      d.print(onder);
    }
    d.drawRect(0, 24, d.width(), 1);
  }

  /* Een lijst met een letter ervoor. De regelhoogte is met opzet ruim: dit
   * wordt met een handschoen aangeraakt, niet met een muis. */
  void lijst(DisplayDriver& d, const char* const* items, uint8_t n, int8_t gekozen) {
    int y = 30;
    for (uint8_t i = 0; i < n && y < d.height() - 14; i++) {
      char letter[4] = { (char)('A' + i), 0, 0, 0 };
      if (i == gekozen) {
        d.fillRect(0, y - 2, d.width(), 16);
      }
      d.setCursor(4, y);
      d.print(letter);
      d.setCursor(20, y);
      d.print(items[i]);
      y += 18;
    }
  }

public:
  FireDeckScreen(UITask* task, mesh::RTCClock* rtc = NULL) : _task(task), _rtc(rtc) {
    _stap = STAP_TYPES; _type = 0; _veld = 0; _bestemming = -1; _gelukt = false;
    _weg = 0; _gemist = 0; _open = 0; _poging = 0;
    memset(_keuze, 0, sizeof(_keuze));
    _regel[0] = 0;
  }

  void setBestemming(int contact_idx) { _bestemming = contact_idx; }
  void opnieuw() { _stap = STAP_TYPES; _veld = 0; memset(_keuze, 0, sizeof(_keuze)); }
  /* Meteen bij een meldingstype beginnen, voor een tegel die daar
   * rechtstreeks heen springt. */
  void kiesType(uint8_t t) {
    if (t >= fd_template.aantal()) t = 0;
    _type = t; _veld = 0; _stap = 1;
    memset(_keuze, 0, sizeof(_keuze));
  }

  int render(DisplayDriver& display) override {
    display.startFrame();
    display.setColor(DisplayDriver::LIGHT);

    if (_stap == STAP_TYPES) {
      kop(display, "BERICHT KIEZEN", "kies een type");
      const char* namen[FD_MAX_TYPES];
      for (uint8_t i = 0; i < fd_template.aantal(); i++) namen[i] = fd_template.type(i).label;
      lijst(display, namen, fd_template.aantal(), -1);

    } else if (_stap == STAP_OK) {
      bouwRegel();
      const FdType& tc = fd_template.type(_type);
      kop(display, "VERZENDEN?", tc.code);
      display.setCursor(4, 32);
      display.printWordWrap(_regel, display.width() - 8);

      int y = display.height() - 14 - (tc.ontvangers * 12);
      if (tc.ontvangers == 0) {
        display.setCursor(4, display.height() - 26);
        display.print(_bestemming >= 0 ? "naar: ingestelde bestemming"
                                       : "geen ontvanger ingesteld");
      } else {
        for (uint8_t i = 0; i < tc.ontvangers; i++) {
          char r[48];
          snprintf(r, sizeof(r), "%s %s",
                   strcmp(tc.ontvanger[i].soort, "room") == 0 ? "room" : "dm",
                   tc.ontvanger[i].label);
          display.setCursor(4, y);
          display.print(r);
          y += 12;
        }
      }
      display.setCursor(4, display.height() - 12);
      display.print("Enter = verzenden");

    } else if (_stap == STAP_WEG) {
      kop(display, _gelukt ? "VERZONDEN" : "NIET GELUKT", fd_template.type(_type).code);
      display.setCursor(4, 32);
      display.printWordWrap(_regel, display.width() - 8);

      char tel[56];
      if (_gemist == 0) snprintf(tel, sizeof(tel), "%u ontvanger(s)", (unsigned) _weg);
      else              snprintf(tel, sizeof(tel), "%u weg, %u onbereikbaar",
                                 (unsigned) _weg, (unsigned) _gemist);
      display.setCursor(4, display.height() - 26);
      display.print(tel);

      display.setCursor(4, display.height() - 12);
      if (_gemist > 0) {
        char v[40];
        snprintf(v, sizeof(v), "R = opnieuw (poging %u)", (unsigned)(_poging + 1));
        display.print(v);
      } else {
        display.print("Enter = nieuw bericht");
      }

    } else {
      const FdType& t = fd_template.type(_type);
      const FdVeld& v = t.velden[_veld];
      char onder[40];
      snprintf(onder, sizeof(onder), "%s  %u/%u", t.code, (unsigned)(_veld + 1), (unsigned)t.aantal);
      kop(display, v.label, onder);
      lijst(display, v.opties, v.aantal, -1);
    }

    display.endFrame();
    /* E-ink hoeft niet op een klok te tekenen: er verandert alleen iets als er
     * een toets ingedrukt wordt. Een ruime waarde spaart stroom en ghosting. */
    return 2000;
  }

  bool handleInput(char c) override {
    if (c == KEY_CANCEL) {           // terug, stap voor stap
      if (_stap == STAP_TYPES) return false;   // laat de UITask naar het hoofdmenu
      if (_stap == STAP_OK)   { _stap = fd_template.type(_type).aantal; _veld = _stap - 1; return true; }
      if (_stap == STAP_WEG)  { opnieuw(); return true; }
      if (_veld == 0) { _stap = STAP_TYPES; return true; }
      _veld--; _stap = _veld + 1;
      return true;
    }

    if (c == KEY_ENTER || c == KEY_SELECT) {
      if (_stap == STAP_OK) {
        bouwRegel();
        _open = 0xFF;                 // eerste poging: iedereen staat nog open
        _poging = 1;
        _weg = stuurNaarOntvangers();
        _gelukt = (_weg > 0);
        _stap = STAP_WEG;
        return true;
      }
      if (_stap == STAP_WEG) { opnieuw(); return true; }
      return true;
    }

    /* Opnieuw proberen, alleen naar wie het niet haalde. Bewust een aparte
     * toets en geen automatische herhaling: bij een melding hoort een mens te
     * beslissen of ze nog eens de lucht in gaat. */
    if ((c == 'r' || c == 'R') && _stap == STAP_WEG && _gemist > 0) {
      _poging++;
      _weg += stuurNaarOntvangers(true);
      _gelukt = (_weg > 0);
      return true;
    }

    /* Letters kiezen. Hoofdletter of kleine letter maakt niet uit: met een
     * handschoen raak je shift niet bewust aan. */
    int idx = -1;
    if (c >= 'a' && c <= 'h') idx = c - 'a';
    else if (c >= 'A' && c <= 'H') idx = c - 'A';
    if (idx < 0) return false;

    if (_stap == STAP_TYPES) {
      if (idx >= fd_template.aantal()) return true;
      _type = (uint8_t)idx; _veld = 0; _stap = 1;
      memset(_keuze, 0, sizeof(_keuze));
      return true;
    }
    if (_stap >= 1 && _stap <= FD_MAX_VELDEN) {
      const FdType& t = fd_template.type(_type);
      if (idx >= t.velden[_veld].aantal) return true;
      _keuze[_veld] = (uint8_t)idx;
      if (_veld + 1 < t.aantal) { _veld++; _stap = _veld + 1; }
      else                      { _stap = STAP_OK; }
      return true;
    }
    return false;
  }
};
