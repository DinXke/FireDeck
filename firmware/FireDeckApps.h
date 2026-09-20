#pragma once

/* De kleine apps achter de dashboardtegels: accu, dekking, positie, alarm,
 * logboek.
 *
 * WAAROM EEN SCHERM EN GEEN VIJF. Ze doen hetzelfde: een handvol regels tonen
 * die het toestel al weet, en soms een knop. Vijf klassen die elk een kop en
 * een lijstje tekenen is vijf keer dezelfde fout kunnen maken. Welke app het
 * is, staat in een veld.
 *
 * WAT ZE TONEN KOMT UIT ECHTE BRONNEN. De accu uit de BQ27220 op het bord, de
 * positie uit de GPS-manager, het laatste signaal uit de radiodriver. Waar een
 * bron niets heeft -- geen fix, geen pakket gehoord -- staat dat er, in plaats
 * van een nul die eruitziet als een meting.
 *
 * ALARM EN POSITIE VERZENDEN ECHT, en wel naar de ontvangers van een melding
 * uit de template. Zo hoeft een korps niet op twee plaatsen in te stellen waar
 * een noodoproep heen moet.
 */

#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include <MeshCore.h>
#include <target.h>
#include "FireDeckTemplate.h"
#include "FireDeckScreen.h"

class UITask;
class MyMesh;
extern MyMesh the_mesh;
extern FireDeckTemplate fd_template;

#define FD_LOG_MAX  8

/* Een kort logboek van wat dit toestel verstuurde. Het leeft in RAM: na een
 * herstart is het weg, en dat is eerlijker dan het op flash bijhouden en de
 * slijtage daarvan verzwijgen voor iets wat niemand achteraf nakijkt. */
struct FdLogRegel {
  char     tekst[48];
  uint32_t tijd;
  uint8_t  weg;
  uint8_t  gemist;
};

class FdLogboek {
  FdLogRegel _r[FD_LOG_MAX];
  uint8_t    _aantal;
public:
  FdLogboek() : _aantal(0) { }
  void voegToe(const char* tekst, uint32_t tijd, uint8_t weg, uint8_t gemist) {
    if (_aantal == FD_LOG_MAX) {
      for (uint8_t i = 1; i < FD_LOG_MAX; i++) _r[i-1] = _r[i];
      _aantal--;
    }
    FdLogRegel& n = _r[_aantal++];
    StrHelper::strncpy(n.tekst, tekst ? tekst : "", sizeof(n.tekst));
    n.tijd = tijd; n.weg = weg; n.gemist = gemist;
  }
  uint8_t aantal() const { return _aantal; }
  const FdLogRegel& regel(uint8_t i) const { return _r[i]; }
};

extern FdLogboek fd_logboek;


class FireDeckApps : public UIScreen {
public:
  enum Soort { ACCU, DEKKING, POSITIE, ALARM, LOGBOEK, GPS, RADIO, KLOK, TOESTEL };

private:
  UITask* _task;
  Soort   _soort;
  bool    _vraagt;       // bevestiging bij alarm/positie
  bool    _gedaan;
  uint8_t _weg, _gemist;

  void regel(DisplayDriver& d, int y, const char* sleutel, const char* waarde) {
    d.setCursor(4, y);
    d.print(sleutel);
    d.setCursor(96, y);
    d.print(waarde);
  }

  /* De ontvangers van een meldingstype gebruiken we ook voor alarm en
   * positie: een korps stelt dan op een plaats in waar dat heen moet. Bestaat
   * het type niet, dan valt hij terug op het eerste. */
  int typeVoor(const char* code) {
    int i = fd_template.typeVoorCode(code);
    return (i >= 0) ? i : 0;
  }

public:
  FireDeckApps(UITask* task, Soort s)
    : _task(task), _soort(s), _vraagt(false), _gedaan(false), _weg(0), _gemist(0) { }

  void opnieuw() { _vraagt = false; _gedaan = false; _weg = _gemist = 0; }
  void zetSoort(Soort s) { _soort = s; opnieuw(); }

  int render(DisplayDriver& display) override {
    display.startFrame();
    display.setColor(DisplayDriver::LIGHT);
    display.setTextSize(1);

    const char* titel = "";
    switch (_soort) {
      case ACCU:    titel = "ACCU"; break;
      case DEKKING: titel = "DEKKING"; break;
      case POSITIE: titel = "POSITIE STUREN"; break;
      case ALARM:   titel = "ALARM"; break;
      case LOGBOEK: titel = "LOGBOEK"; break;
      case GPS:     titel = "GPS"; break;
      case RADIO:   titel = "RADIO"; break;
      case KLOK:    titel = "TIJD"; break;
      case TOESTEL: titel = "TOESTEL"; break;
    }
    display.setCursor(4, 2);
    display.print(titel);
    display.drawRect(0, 14, display.width(), 1);

    char w[48];
    int y = 24;

    if (_gedaan) {
      display.setTextSize(2);
      display.setCursor(4, 40);
      display.print(_weg > 0 ? "verstuurd" : "mislukt");
      display.setTextSize(1);
      snprintf(w, sizeof(w), "%u ontvanger(s), %u onbereikbaar",
               (unsigned) _weg, (unsigned) _gemist);
      display.setCursor(4, 70);
      display.print(w);
      display.setCursor(4, display.height() - 12);
      display.print("Enter = terug");

    } else if (_vraagt) {
      display.setCursor(4, 24);
      display.printWordWrap(_soort == ALARM
        ? "Noodoproep versturen naar de ontvangers van deze melding?"
        : "Je positie versturen?", display.width() - 8);
      display.setCursor(4, display.height() - 24);
      display.print("J = versturen");
      display.setCursor(4, display.height() - 12);
      display.print("elke andere toets = annuleren");

    } else switch (_soort) {

      case ACCU: {
        uint16_t mv = board.getBattMilliVolts();
        snprintf(w, sizeof(w), "%u,%02u V", mv / 1000, (mv % 1000) / 10);
        regel(display, y, "Spanning", w); y += 14;

        int16_t stroom = board.getAvgCurrent();
        if (stroom == 0) StrHelper::strncpy(w, "onbekend", sizeof(w));
        else snprintf(w, sizeof(w), "%d mA", (int) stroom);
        regel(display, y, "Stroom", w); y += 14;

        uint16_t rest = board.getTimeToEmpty();
        /* Een brandstofmeter geeft 65535 als hij het niet weet. Dat als
         * "1092 uur" tonen is erger dan eerlijk zeggen dat het onbekend is. */
        if (rest == 0 || rest == 0xFFFF) StrHelper::strncpy(w, "onbekend", sizeof(w));
        else snprintf(w, sizeof(w), "%u u %02u min", rest / 60, rest % 60);
        regel(display, y, "Nog", w); y += 14;
        break;
      }

      case DEKKING: {
        snprintf(w, sizeof(w), "%d", the_mesh.getNumContacts());
        regel(display, y, "Contacten", w); y += 14;

        int gehoord = (int) radio_driver.getLastRSSI();
        if (gehoord == 0) StrHelper::strncpy(w, "nog niets gehoord", sizeof(w));
        else snprintf(w, sizeof(w), "%d dBm", gehoord);
        regel(display, y, "Laatste", w); y += 14;

        snprintf(w, sizeof(w), "%d dB", (int) radio_driver.getLastSNR());
        regel(display, y, "SNR", w); y += 14;

        display.setCursor(4, y + 6);
        display.printWordWrap("Dit is wat dit toestel zelf hoorde, niet de dekking van de "
                              "hele mesh.", display.width() - 8);
        break;
      }

      case GPS:
      case POSITIE: {
        bool fix = (sensors.node_lat != 0 || sensors.node_lon != 0);
        if (!fix) {
          display.setCursor(4, y);
          display.printWordWrap("Nog geen positie. Wacht op een GPS-fix, of zet GPS aan "
                                "in de instellingen.", display.width() - 8);
        } else {
          snprintf(w, sizeof(w), "%.5f", sensors.node_lat);
          regel(display, y, "Breedte", w); y += 14;
          snprintf(w, sizeof(w), "%.5f", sensors.node_lon);
          regel(display, y, "Lengte", w); y += 14;
          if (_soort == POSITIE) {
            display.setCursor(4, display.height() - 12);
            display.print("Enter = versturen");
          }
        }
        break;
      }

      case ALARM: {
        display.setTextSize(2);
        display.setCursor(4, 30);
        display.print("MAYDAY");
        display.setTextSize(1);
        display.setCursor(4, 60);
        display.printWordWrap("Stuurt onmiddellijk een noodoproep met je positie, zonder "
                              "verdere schermen.", display.width() - 8);
        display.setCursor(4, display.height() - 12);
        display.print("Enter = versturen");
        break;
      }

      case LOGBOEK: {
        if (fd_logboek.aantal() == 0) {
          display.setCursor(4, y);
          display.print("Nog niets verstuurd.");
        } else {
          for (int i = fd_logboek.aantal() - 1; i >= 0 && y < display.height() - 16; i--) {
            const FdLogRegel& r = fd_logboek.regel((uint8_t) i);
            snprintf(w, sizeof(w), "%02u:%02u  %s",
                     (unsigned)((r.tijd / 3600) % 24), (unsigned)((r.tijd / 60) % 60),
                     r.gemist ? "deels" : "ok");
            display.setCursor(4, y);
            display.print(w);
            display.setCursor(4, y + 11);
            display.drawTextEllipsized(4, y + 11, display.width() - 8, r.tekst);
            y += 26;
          }
        }
        break;
      }

      case RADIO: {
        NodePrefs* p = the_mesh.getNodePrefs();
        if (p) {
          snprintf(w, sizeof(w), "%.3f MHz", p->freq);
          regel(display, y, "Frequentie", w); y += 14;
          snprintf(w, sizeof(w), "%.1f kHz", p->bw);
          regel(display, y, "Bandbreedte", w); y += 14;
          snprintf(w, sizeof(w), "%u", (unsigned) p->sf);
          regel(display, y, "Spreiding", w); y += 14;
          snprintf(w, sizeof(w), "%u dBm", (unsigned) p->tx_power_dbm);
          regel(display, y, "Vermogen", w); y += 14;
        }
        break;
      }

      case KLOK: {
        uint32_t nu = the_mesh.getRTCClock()->getCurrentTime();
        if (nu == 0) {
          display.setCursor(4, y);
          display.print("Klok niet gezet.");
        } else {
          snprintf(w, sizeof(w), "%02u:%02u:%02u",
                   (unsigned)((nu / 3600) % 24), (unsigned)((nu / 60) % 60),
                   (unsigned)(nu % 60));
          regel(display, y, "Tijd (UTC)", w); y += 14;
        }
        break;
      }

      case TOESTEL: {
        regel(display, y, "Bord", "T-Deck Pro"); y += 14;
        regel(display, y, "Scherm", "240x320 e-ink"); y += 14;
        snprintf(w, sizeof(w), "%u", (unsigned) fd_template.aantal());
        regel(display, y, "Meldingen", w); y += 14;
        snprintf(w, sizeof(w), "%u", (unsigned) fd_template.tegelAantal());
        regel(display, y, "Tegels", w); y += 14;
        break;
      }
    }

    display.endFrame();
    return 4000;
  }

  bool handleInput(char c) override {
    if (_gedaan) {
      if (c == KEY_ENTER || c == KEY_SELECT || c == KEY_CANCEL) { opnieuw(); return false; }
      return true;
    }
    if (_vraagt) {
      if (c == 'j' || c == 'J') {
        char tekst[80];
        bool fix = (sensors.node_lat != 0 || sensors.node_lon != 0);
        if (_soort == ALARM) {
          if (fix) snprintf(tekst, sizeof(tekst), "MAYDAY %.5f,%.5f",
                            sensors.node_lat, sensors.node_lon);
          else     StrHelper::strncpy(tekst, "MAYDAY (geen positie)", sizeof(tekst));
        } else {
          snprintf(tekst, sizeof(tekst), "POS %.5f,%.5f", sensors.node_lat, sensors.node_lon);
        }
        extern FireDeckScreen* fd_scherm;
        if (fd_scherm != NULL) {
          _weg = fd_scherm->stuurTekst(tekst, (uint8_t) typeVoor(_soort == ALARM ? "ALM" : "POS"),
                                       &_gemist);
        } else {
          _weg = 0; _gemist = 1;
        }
        fd_logboek.voegToe(tekst, the_mesh.getRTCClock()->getCurrentTime(), _weg, _gemist);
        _vraagt = false;
        _gedaan = true;
        return true;
      }
      _vraagt = false;
      return true;
    }

    if ((_soort == ALARM || _soort == POSITIE) && (c == KEY_ENTER || c == KEY_SELECT)) {
      _vraagt = true;
      return true;
    }
    return false;
  }
};
