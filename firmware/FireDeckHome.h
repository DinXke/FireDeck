#pragma once

/* Het startscherm dat de template bepaalt, en de Over-app met de fabrieksreset.
 *
 * WAAROM DE TEGELS UIT DE TEMPLATE KOMEN. Een korps bepaalt niet alleen wat er
 * achter de knoppen zit, maar ook welke knoppen er zijn en in welke volgorde.
 * Een ploeg die "Uitgerukt" tien keer per dienst gebruikt, wil die als eerste
 * tegel -- niet twee schermen diep achter een lijst. Dat is geen voorkeur maar
 * zendtijd en aandacht die je terugwint.
 *
 * DE ICONEN komen uit FireDeckIconen.h: dezelfde SVG's als de beheersite,
 * door een browser op een canvas van 24x24 getekend en per pixel uitgelezen.
 * Wat je in de editor kiest is dus letterlijk wat op het paneel komt. Kent de
 * firmware een iconnaam niet, dan staan er de beginletters van het opschrift --
 * zo blijft een template met een nieuw icoon bruikbaar op oudere firmware.
 *
 * WAAROM DE FABRIEKSRESET TWEE STAPPEN HEEFT. Hij wist de identiteit, en
 * daarmee valt dit toestel uit de contactenlijst van iedereen die hem kende.
 * Dat mag nooit het gevolg zijn van een verkeerde tik met een handschoen aan.
 */

#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include <MeshCore.h>
#include "FireDeckTemplate.h"
#include "FireDeckSlaap.h"
#include "FireDeckIconen.h"

class UITask;
class MyMesh;
extern MyMesh the_mesh;
extern FireDeckTemplate fd_template;
extern FireDeckSlaap fd_slaap;

/* Wat een tegel opent. De UITask vertaalt dit naar zijn eigen schermen; dit
 * scherm weet niet welke dat zijn, en dat hoort ook niet. */
struct FdKeuze {
  const char* actie;    // "app:msg", "type:UIT", ... of NULL
};

class FireDeckHome : public UIScreen {
  UITask*  _task;
  uint8_t  _pagina;
  char     _gekozen[24];     // de actie waarop getikt is, voor de UITask

  /* Twee tekens uit het opschrift. Een woord als "Bestemming" wordt "Be", en
   * dat is genoeg om tegels uit elkaar te houden zolang er geen iconen zijn. */
  static void beginletters(const char* label, char* uit, size_t max) {
    size_t n = 0;
    if (label) {
      for (const char* p = label; *p && n < 2 && n + 1 < max; p++) {
        if (*p == ' ' || *p == '-') continue;
        uit[n++] = (n == 1) ? toupper(*p) : tolower(*p);
      }
    }
    uit[n] = 0;
  }

public:
  FireDeckHome(UITask* task) : _task(task), _pagina(1) { _gekozen[0] = 0; }

  /* De UITask vraagt dit na elke toets: staat er een actie klaar, dan opent hij
   * het bijbehorende scherm en wist hem. */
  const char* neemKeuze() {
    if (_gekozen[0] == 0) return NULL;
    return _gekozen;
  }
  void wisKeuze() { _gekozen[0] = 0; }
  bool heeftTegels() const { return fd_template.eigenDashboard(); }

  int render(DisplayDriver& display) override {
    display.startFrame();
    display.setColor(DisplayDriver::LIGHT);
    display.setTextSize(1);

    /* statusbalk */
    NodePrefs* np = the_mesh.getNodePrefs();
    display.setCursor(3, 2);
    display.print(np ? np->node_name : "FireDeck");
    display.drawRect(0, 14, display.width(), 1);

    /* tegels van deze pagina */
    const int kolom = display.width() / 3;
    const int rijh  = 62;
    int nr = 0, getoond = 0;

    for (uint8_t i = 0; i < fd_template.tegelAantal() && getoond < 9; i++) {
      const FdTegel& g = fd_template.tegel(i);
      if (g.pagina != _pagina) continue;

      int kx = (getoond % 3) * kolom + 4;
      int ky = 20 + (getoond / 3) * rijh;

      display.drawRect(kx + 6, ky, kolom - 16, 38);          // het kader

      const uint8_t* icoon = fdIcoonZoek(g.icoon);
      if (icoon != NULL) {
        display.drawXbm(kx + 6 + ((kolom - 16) - FD_ICOON_PX) / 2, ky + 7,
                        icoon, FD_ICOON_PX, FD_ICOON_PX);
      } else {
        /* Geen bitmap voor deze naam: liever de beginletters dan een verkeerd
         * icoon. Zo blijft een template met een nieuw icoon bruikbaar op oude
         * firmware. */
        char kort[4];
        beginletters(g.label, kort, sizeof(kort));
        display.setTextSize(2);
        display.drawTextCentered(kx + 6 + (kolom - 16) / 2, ky + 11, kort);
      }

      display.setTextSize(1);
      display.drawTextCentered(kx + 6 + (kolom - 16) / 2, ky + 42, g.label);

      /* de sneltoets in de hoek */
      char letter[2] = { (char)('A' + getoond), 0 };
      display.setCursor(kx + kolom - 16, ky - 1);
      display.print(letter);

      getoond++;
      nr++;
    }

    if (getoond == 0) {
      display.setCursor(6, 40);
      display.print("Geen tegels op deze pagina.");
      display.setCursor(6, 54);
      display.print("Stel een template in.");
    }

    /* paginabolletjes, alleen als er een tweede pagina bestaat */
    bool tweede = false;
    for (uint8_t i = 0; i < fd_template.tegelAantal(); i++) {
      if (fd_template.tegel(i).pagina == 2) { tweede = true; break; }
    }
    if (tweede) {
      int mx = display.width() / 2;
      display.fillRect(mx - 7, display.height() - 16, 5, 5);
      display.drawRect(mx + 3, display.height() - 16, 5, 5);
      if (_pagina == 2) {
        display.drawRect(mx - 7, display.height() - 16, 5, 5);
        display.fillRect(mx + 3, display.height() - 16, 5, 5);
      }
    }

    display.drawRect(0, display.height() - 10, display.width(), 1);
    display.setCursor(3, display.height() - 8);
    display.print(tweede ? "A-I kiezen  \x1A volgende pagina" : "A-I kiezen");

    display.endFrame();
    return 4000;     /* e-ink: alleen hertekenen als er iets verandert */
  }

  bool handleInput(char c) override {
    if (c == KEY_RIGHT || c == KEY_NEXT) { _pagina = (_pagina == 1) ? 2 : 1; return true; }
    if (c == KEY_LEFT  || c == KEY_PREV) { _pagina = (_pagina == 1) ? 2 : 1; return true; }

    int idx = -1;
    if (c >= 'a' && c <= 'i') idx = c - 'a';
    else if (c >= 'A' && c <= 'I') idx = c - 'A';
    if (idx < 0) return false;

    /* De zichtbare volgorde telt, niet de volgorde in het bestand: wat de
     * ploeg als derde tegel ziet, hoort achter C te zitten. */
    int getoond = 0;
    for (uint8_t i = 0; i < fd_template.tegelAantal(); i++) {
      const FdTegel& g = fd_template.tegel(i);
      if (g.pagina != _pagina) continue;
      if (getoond == idx) {
        StrHelper::strncpy(_gekozen, g.actie ? g.actie : "", sizeof(_gekozen));
        return true;
      }
      getoond++;
    }
    return true;
  }
};


/* ------------------------------------------------------------------- Over
 * Toestelgegevens plus de fabrieksreset. Twee stappen: eerst vragen, dan pas
 * doen. */
class FireDeckInfo : public UIScreen {
  UITask* _task;
  bool    _vraagt;      // staat de bevestiging op het scherm?

public:
  FireDeckInfo(UITask* task) : _task(task), _vraagt(false) { }
  void opnieuw() { _vraagt = false; }

  int render(DisplayDriver& display) override {
    display.startFrame();
    display.setColor(DisplayDriver::LIGHT);
    display.setTextSize(1);

    display.setCursor(4, 2);
    display.print(_vraagt ? "FABRIEKSRESET" : "OVER DIT TOESTEL");
    display.drawRect(0, 14, display.width(), 1);

    if (_vraagt) {
      display.setCursor(4, 22);
      display.printWordWrap("Dit wist de identiteit, de contacten, de kanalen en de template.",
                            display.width() - 8);
      display.setCursor(4, 76);
      display.printWordWrap("Dit toestel verdwijnt daarmee uit de contactenlijst van iedereen "
                            "die hem kende.", display.width() - 8);
      display.setCursor(4, display.height() - 24);
      display.print("J = wissen");
      display.setCursor(4, display.height() - 12);
      display.print("elke andere toets = annuleren");
    } else {
      NodePrefs* np = the_mesh.getNodePrefs();
      uint8_t* pk = the_mesh.self_id.pub_key;
      char r[64];

      display.setCursor(4, 22);
      display.print(np ? np->node_name : "(geen naam)");

      snprintf(r, sizeof(r), "sleutel %02x%02x%02x%02x", pk[0], pk[1], pk[2], pk[3]);
      display.setCursor(4, 38);
      display.print(r);

      snprintf(r, sizeof(r), "template %s", fd_template.uitBestand() ? "geladen" : "ingebouwd");
      display.setCursor(4, 52);
      display.print(r);

      snprintf(r, sizeof(r), "%u melding(en), %u tegel(s)",
               (unsigned) fd_template.aantal(), (unsigned) fd_template.tegelAantal());
      display.setCursor(4, 66);
      display.print(r);

      snprintf(r, sizeof(r), "%d contact(en)", the_mesh.getNumContacts());
      display.setCursor(4, 80);
      display.print(r);

      char sl[48];
      snprintf(sl, sizeof(sl), "diepe slaap: %s%s",
               fd_slaap.aan() ? "aan na " : "uit",
               fd_slaap.aan() ? "" : "");
      display.setCursor(4, 96);
      display.print(sl);
      if (fd_slaap.aan()) {
        snprintf(sl, sizeof(sl), "   %u min zonder bediening", (unsigned) fd_slaap.minuten());
        display.setCursor(4, 108);
        display.print(sl);
      }

      display.setCursor(4, display.height() - 24);
      display.print("S = slaap aan/uit   T = tijd");
      display.setCursor(4, display.height() - 12);
      display.print("W = fabrieksreset");
    }

    display.endFrame();
    return 4000;
  }

  bool handleInput(char c) override {
    if (_vraagt) {
      if (c == 'j' || c == 'J') {
        /* Pas hier gebeurt het, en daarna start hij opnieuw op. */
        the_mesh.fabrieksReset();
        return true;
      }
      _vraagt = false;            // elke andere toets is annuleren
      return true;
    }
    if (c == 'w' || c == 'W') { _vraagt = true; return true; }
    if (c == 's' || c == 'S') { fd_slaap.zet(!fd_slaap.aan()); return true; }
    if (c == 't' || c == 'T') { fd_slaap.volgendeTijd(); return true; }
    return false;
  }
};
