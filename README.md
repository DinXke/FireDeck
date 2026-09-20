# FireDeck

Een bedienbare **LilyGo T-Deck Pro** in de browser, op ware grootte, met daarin
een berichtflow voor een brandweerploeg: kies een meldingstype, kies op de
volgende schermen de standaardwaarden, klaar. Plus room servers, directe
berichten, contacten, instelbare bestemmingen en een dekkingsscherm dat op een
echte MeshCore-mesh kijkt.

Het bestaat omdat het ontwerpen van zo'n flow op echte hardware traag is. E-ink
ververst langzaam, flashen kost minuten, en je ziet pas na afloop of een regel
tekst past. Hier zie je dat meteen — het scherm is **exact 240×320**, dezelfde
maat als het GDEQ031T10-paneel dat de T-Deck Pro geroteerd gebruikt.

> Dit is **geen firmware**. Het is het gereedschap om te beslissen wat de
> firmware moet doen, vóór je die schrijft.

## Wat er in zit

| | |
|---|---|
| `index.html` | het toestel: schermen, iconen, toetsenbord, mesh-verloop |
| `firedeck.py` | webserver, login en de brug naar openHop |
| `login.html` | de toegangspagina |

Eén HTML-bestand draagt het hele toestel. Geen bouwstap, geen bundler, geen
afhankelijkheden buiten twee lettertypes van Google Fonts.

## Twee manieren om het te draaien

**Als los bestand.** Open `index.html` in een browser. Alles werkt, behalve
echt verzenden; de dekkingsapp toont dan een ingebakken momentopname van echte
meshgegevens in plaats van live cijfers.

**Achter de brug.** Draai `firedeck.py` naast of naast een
[openHop-repeater](https://github.com/openhop-dev/openhop_repeater). Dan komt
`/mesh` van de echte mesh en gaat `/send` werkelijk de lucht in.

```bash
python3 firedeck.py
```

Instellen gaat via omgevingsvariabelen:

| variabele | standaard | betekenis |
|---|---|---|
| `FIREDECK_PORT` | `8090` | luisterpoort |
| `FIREDECK_PASS` | _verplicht_ | gedeeld wachtwoord; niet gezet = hij verzint er een en drukt die af |
| `FIREDECK_ADMIN_PASS` | leeg | tweede wachtwoord dat ook `/mesh` vrijgeeft |
| `FIREDECK_MESH` | `beheer` | `open` geeft `/mesh` aan elke ingelogde bezoeker |
| `FIREDECK_SEND` | `echt` | `sim` bouwt het bericht wel op maar zendt niet |
| `OPENHOP_URL` | `http://127.0.0.1:8000` | waar de openHop-API luistert |
| `OPENHOP_PASS` | — | adminwachtwoord van openHop; anders uit `config.yaml` |

Het openHop-wachtwoord hoort **nooit** in de browser. Daarom de brug: de pagina
kent alleen `/send` en `/mesh`, en de sleutel blijft aan de serverkant.

## Het slot

Eén gedeeld wachtwoord met een eigen inlogpagina, geen basic auth — die geeft
je een grauwe browserpopup en geen manier om af te melden. De sessie is een
ondertekende cookie met alleen een soort en een vervaltijd erin; er is geen
sessietabel, dus een herstart logt niemand uit.

Twee niveaus, één invoerveld. Het gedeelde wachtwoord geeft het toestel; het
beheerderswachtwoord geeft daarbovenop `/mesh`. De **soort zit in de
ondertekening**, niet ernaast — anders kon een gast zijn cookie omdopen tot
beheerder.

Het gedeelde wachtwoord wordt hoofdletterongevoelig vergeleken. Dat is met
opzet: het is een demowachtwoord dat je doorgeeft, en een telefoontoetsenbord
maakt er graag `Firemesh` van. Het beheerderswachtwoord is dat níet.

## Achter cloudflared

Drie dingen gaan anders dan bij een dienst op je eigen netwerk, en alle drie
zijn ze in `firedeck.py` afgehandeld:

- De `Secure`-vlag op de cookie komt uit `X-Forwarded-Proto`, niet uit onze
  eigen socket — de tunnel praat plat HTTP met ons terwijl de bezoeker HTTPS
  ziet.
- De vertraging na foute pogingen kijkt naar `CF-Connecting-IP`. Anders is
  iedereen `127.0.0.1` en vertraag je de hele wereld tegelijk.
- De pagina wordt in een volledig HTML-document verpakt. `index.html` is
  geschreven als artifact-body en heeft geen `<head>`; zonder viewport-meta
  krijgt een telefoon een desktoppagina uitgezoomd.

## De berichtencatalogus

Acht types met elk hun eigen vervolgschermen staan boven in `index.html` in
`CATALOG`. Vervang die door jullie eigen meldingslijst; de rest van het toestel
past zich aan.

Elke standaardwaarde is **één byte**. Dat is de hele reden om te laten kiezen in
plaats van typen: het vrije-tekstveld in `SIT` staat er met opzet in zodat je
het verschil ziet — 5 bytes tegenover 31.

De zendtijd is een rechte lijn door twee gemeten pakketten uit een echt
repeaterlog (28 B → 443 ms, 44 B → 575 ms), met 12 B aangenomen MeshCore-kop.
Een schatting uit metingen dus, geen uitkomst van de LoRa-formule.

## Waarom letters en geen cijfers

Op het BlackBerry-toetsenbord van de T-Deck Pro (TCA8418) liggen de cijfers op
de alt-laag: `W`=1, `E`=2, `S`=4, `Z`=7. Een cijfer kiezen kost dus twee
aanslagen, een letter één. Met handschoenen aan telt dat.

## Naar echte firmware

Het scherm is op de millimeter het paneel dat **[Meck](https://github.com/pelgraine/Meck)**
via GxEPD2 aanstuurt. Wat hier past, past daar. Ripple UI is gesloten — geen
bron, eigen fonts, geen LVGL — dus eigen firmware bouw je op Meck of op
[MeshCore](https://github.com/meshcore-dev/MeshCore) upstream.

## Licentie

MIT, zie [LICENSE](LICENSE).
