#!/usr/bin/env python3
"""FireDeck -- de T-Deck-simulator met een slot erop en een brug naar openHop.

WAT DIT DOET. Het serveert de simulator, en geeft hem twee dingen die een
webpagina zelf niet mag: /mesh haalt de echte dekking bij openHop op, en /send
zet een bericht werkelijk de mesh in. Het adminwachtwoord van openHop blijft
hier; de pagina kent alleen deze twee paden.

HET SLOT. Eén gedeeld wachtwoord, een nette loginpagina, en een ondertekende
cookie. Geen basic auth: die geeft je een grauwe browserpopup en geen manier om
uit te loggen. De cookie draagt alleen een vervaltijd plus een HMAC met een
sleutel die hier op schijf staat -- geen sessietabel, en na een herstart blijven
bestaande sessies dus gewoon geldig.

ACHTER CLOUDFLARED. De tunnel praat plat HTTP met ons, terwijl de bezoeker
HTTPS ziet. Daarom komt de Secure-vlag uit X-Forwarded-Proto en niet uit onze
eigen socket, en kijkt de vertraging bij foute pogingen naar CF-Connecting-IP
-- anders is iedereen 127.0.0.1 en vertraag je de hele wereld tegelijk.

ZENDEN. FIREDECK_SEND bepaalt of /send echt de lucht in gaat ('echt') of
beleefd doet alsof ('sim'). Staat de site publiek, dan is 'sim' het verstandige
antwoord: een tester die de knop indrukt, kost anders echte zendtijd op een
gedeelde band.
"""

import hashlib
import hmac
import json
import os
import secrets
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = os.path.dirname(os.path.abspath(__file__))
POORT = int(os.environ.get("FIREDECK_PORT", "8090"))
OPENHOP = os.environ.get("OPENHOP_URL", "http://127.0.0.1:8000")
CONFIG = os.environ.get("OPENHOP_CONFIG", "/etc/openhop_repeater/config.yaml")
# Geen ingebakken standaard: een wachtwoord dat in de broncode staat is geen
# wachtwoord. Is er niets gezet, dan verzint hij er een en zegt welke --
# dan staat de deur nooit per ongeluk open.
TOEGANG = os.environ.get("FIREDECK_PASS", "")
ZENDEN = os.environ.get("FIREDECK_SEND", "echt").lower()
# Leeg = niemand komt aan /mesh behalve wie het beheerderswachtwoord kent.
BEHEER = os.environ.get("FIREDECK_ADMIN_PASS", "")
# open = /mesh voor elke ingelogde bezoeker; beheer = alleen met de tweede sleutel.
MESH_STAND = os.environ.get("FIREDECK_MESH", "beheer").lower()
SESSIE_UREN = int(os.environ.get("FIREDECK_SESSION_HOURS", "72"))
GEHEIM_PAD = os.path.join(ROOT, "sessiesleutel")

_slot = {"token": None, "tot": 0.0}
_slot_lock = threading.Lock()
_pogingen = {}
_pog_lock = threading.Lock()


# --------------------------------------------------------------- sessies
def sessiesleutel():
    """Blijft op schijf staan, zodat een herstart niet iedereen uitlogt."""
    try:
        with open(GEHEIM_PAD, "rb") as f:
            k = f.read().strip()
            if len(k) >= 32:
                return k
    except OSError:
        pass
    k = secrets.token_hex(32).encode()
    with open(GEHEIM_PAD, "wb") as f:
        f.write(k)
    os.chmod(GEHEIM_PAD, 0o600)
    return k


SLEUTEL = sessiesleutel()


def maak_cookie(soort="gast"):
    """De soort zit IN de ondertekening, niet ernaast.

    Anders kon iemand zijn gastcookie omdopen tot beheerder door er een ander
    woord voor te plakken -- de handtekening moet dekken wat ze beweert.
    """
    tot = str(int(time.time()) + SESSIE_UREN * 3600)
    boodschap = soort + ":" + tot
    mac = hmac.new(SLEUTEL, boodschap.encode(), hashlib.sha256).hexdigest()[:32]
    return soort + "." + tot + "." + mac


def cookie_geldig(waarde, soort="gast"):
    if not waarde or waarde.count(".") != 2:
        return False
    gezegd, tot, mac = waarde.split(".")
    if gezegd != soort:
        return False
    verwacht = hmac.new(SLEUTEL, (gezegd + ":" + tot).encode(),
                        hashlib.sha256).hexdigest()[:32]
    if not hmac.compare_digest(mac, verwacht):
        return False
    try:
        return int(tot) > time.time()
    except ValueError:
        return False


# --------------------------------------------------------------- openHop
def admin_wachtwoord():
    uit_env = os.environ.get("OPENHOP_PASS")
    if uit_env:
        return uit_env
    import yaml  # alleen nodig als we naast openHop draaien
    with open(CONFIG) as f:
        cfg = yaml.safe_load(f) or {}
    return (cfg.get("repeater", {}).get("security", {}) or {}).get("admin_password", "")


def api(pad, data=None, token_=None, timeout=20, methode=None):
    req = urllib.request.Request(
        OPENHOP + pad, method=methode or ("POST" if data is not None else "GET"))
    req.add_header("Content-Type", "application/json")
    if token_:
        req.add_header("Authorization", "Bearer " + token_)
    body = json.dumps(data).encode() if data is not None else None
    with urllib.request.urlopen(req, body, timeout=timeout) as r:
        return json.loads(r.read().decode("utf-8", "replace") or "{}")


def token(vernieuw=False):
    with _slot_lock:
        if not vernieuw and _slot["token"] and time.time() < _slot["tot"]:
            return _slot["token"]
        res = api("/auth/login", {"username": "admin",
                                  "password": admin_wachtwoord(),
                                  "client_id": "firedeck"})
        t = res.get("token") or (res.get("data") or {}).get("token")
        if not t:
            raise RuntimeError("geen token in het antwoord van openHop")
        _slot["token"] = t
        _slot["tot"] = time.time() + 55 * 60   # JWT leeft een uur; marge
        return t


def verstuur(pub_key, tekst, txt_type=0):
    lading = {"pub_key": pub_key, "text": tekst, "txt_type": int(txt_type)}
    try:
        return api("/api/companion/send_text", lading, token())
    except urllib.error.HTTPError as e:
        if e.code != 401:
            raise
        return api("/api/companion/send_text", lading, token(vernieuw=True))


TYPEN = {0: "REQ", 1: "RESPONSE", 2: "TXT", 3: "ACK", 4: "ADVERT", 5: "GRP_TXT",
         6: "GRP_DATA", 7: "ANON_REQ", 8: "PATH", 9: "TRACE"}
ROUTES = {0: "transport flood", 1: "flood", 2: "direct", 3: "transport direct"}


def dekking():
    """Buren, recent verkeer en de contacten, in één antwoord.

    Drie bronnen bij elkaar: wie we rechtstreeks horen (neighbor_links), wat er
    voorbijkwam (recent_packets, met het pad erin) en de verhouding
    flood/direct over een dag.
    """
    t = token()
    uit = {"buren": [], "pakketten": [], "verkeer": {}, "contacten": []}

    def haal(pad):
        req = urllib.request.Request(OPENHOP + pad, method="GET")
        req.add_header("Authorization", "Bearer " + t)
        with urllib.request.urlopen(req, timeout=15) as r:
            return json.loads(r.read().decode("utf-8", "replace") or "{}")

    try:
        d = haal("/api/neighbor_links?active_within_seconds=3600").get("data") or {}
        for lk in (d.get("links") or [])[:12]:
            uit["buren"].append({
                "hash": lk.get("peer_hash"), "rssi": lk.get("last_rssi"),
                "snr": lk.get("last_snr"),
                "score": lk.get("ewma_score") or lk.get("last_score"),
                "leeftijd": int(lk.get("age_seconds") or 0),
                "monsters": lk.get("sample_count"), "actief": bool(lk.get("active")),
            })
        uit["buren"].sort(key=lambda x: -(x["score"] or 0))
    except Exception as exc:
        uit["buren_fout"] = str(exc)

    try:
        for pk in (haal("/api/recent_packets?limit=40").get("data") or []):
            pad_ = str(pk.get("path_hash") or "").strip("[]")
            hops = len([x for x in pad_.split(",") if x.strip()])
            uit["pakketten"].append({
                "type": TYPEN.get(pk.get("type"), str(pk.get("type"))),
                "route": ROUTES.get(pk.get("route"), str(pk.get("route"))),
                "len": pk.get("length"), "rssi": pk.get("rssi"), "snr": pk.get("snr"),
                "hops": hops, "pad": pad_, "via": pk.get("upstream_hash"),
                "door": bool(pk.get("transmitted")), "weg": pk.get("drop_reason"),
            })
    except Exception as exc:
        uit["pakketten_fout"] = str(exc)

    try:
        uit["verkeer"] = haal("/api/route_stats?hours=24").get("data") or {}
    except Exception as exc:
        uit["verkeer_fout"] = str(exc)

    try:
        for c in (haal("/api/companion/contacts").get("data") or []):
            uit["contacten"].append({"naam": c.get("name"), "key": c.get("public_key"),
                                     "pad_len": c.get("out_path_len")})
    except Exception as exc:
        uit["contacten_fout"] = str(exc)

    return uit


# --------------------------------------------------------------- webserver
def lees(naam):
    with open(os.path.join(ROOT, naam), "rb") as f:
        return f.read()


# De pagina is geschreven als artifact-body: geen doctype, geen head, en dus
# geen viewport-meta. Claude's artifact-omgeving zet daar zelf een omhulsel
# omheen; serveren wij hem rechtstreeks, dan krijgt een telefoon een
# desktoppagina uitgezoomd. Hetzelfde bestand blijft zo op beide plaatsen goed.
OMHULSEL = """<!doctype html>
<html lang="nl">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<meta name="color-scheme" content="light dark">
<style>
:root{padding-top:env(safe-area-inset-top,0px);padding-bottom:env(safe-area-inset-bottom,0px)}
html,body{margin:0}
img{max-width:100%}
[hidden]{display:none !important}
</style>
</head>
<body>
<!--INHOUD-->
</body>
</html>
"""


def pagina():
    rauw = lees("index.html").decode("utf-8")
    if rauw.lstrip()[:9].lower().startswith("<!doctype"):
        return rauw.encode("utf-8")
    return OMHULSEL.replace("<!--INHOUD-->", rauw).encode("utf-8")


class FireDeck(BaseHTTPRequestHandler):
    server_version = "FireDeck/1.0"
    protocol_version = "HTTP/1.1"

    # ---------------------------------------------------------- hulpjes
    def _https(self):
        return (self.headers.get("X-Forwarded-Proto", "").lower() == "https")

    def _ip(self):
        # Achter cloudflared is de socket altijd de tunnel; de bezoeker staat
        # in CF-Connecting-IP. Zonder tunnel valt hij terug op de socket.
        return (self.headers.get("CF-Connecting-IP")
                or (self.headers.get("X-Forwarded-For", "").split(",")[0].strip())
                or self.client_address[0])

    def _stuur(self, code, blob, ctype, extra=None):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(blob)))
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Referrer-Policy", "no-referrer")
        for k, v in (extra or []):
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(blob)

    def _json(self, code, obj, extra=None):
        self._stuur(code, json.dumps(obj).encode(), "application/json", extra)

    def _koekje(self, naam):
        for stuk in self.headers.get("Cookie", "").split(";"):
            if "=" in stuk:
                k, v = stuk.split("=", 1)
                if k.strip() == naam:
                    return v.strip()
        return None

    def _ingelogd(self):
        return cookie_geldig(self._koekje("firedeck"), "gast")

    def _beheerder(self):
        if MESH_STAND == "open":
            return self._ingelogd()
        return cookie_geldig(self._koekje("firedeck_b"), "beheer")

    def _loginpagina(self, fout=None):
        html = lees("login.html").decode("utf-8")
        if fout:
            html = html.replace("<!--FOUT-->", '<p class="fout">' + fout + "</p>")
        self._stuur(401 if fout else 200, html.encode("utf-8"),
                    "text/html; charset=utf-8", [("Cache-Control", "no-store")])

    # ---------------------------------------------------------- GET
    def do_GET(self):
        pad = self.path.split("?", 1)[0]

        if pad == "/favicon.ico":
            return self._stuur(204, b"", "image/x-icon")

        if pad == "/health":          # voor cloudflared en uptime-checks
            return self._json(200, {"app": "FireDeck", "ok": True})

        if pad == "/login":
            if self._ingelogd():
                return self._stuur(302, b"", "text/plain", [("Location", "/")])
            return self._loginpagina()

        if pad == "/logout":
            return self._stuur(302, b"", "text/plain", [
                ("Location", "/login"),
                ("Set-Cookie", "firedeck=; Path=/; Max-Age=0; HttpOnly; SameSite=Lax"),
                ("Set-Cookie", "firedeck_b=; Path=/; Max-Age=0; HttpOnly; SameSite=Lax")])

        if not self._ingelogd():
            if pad in ("/", "/index.html"):
                return self._loginpagina()
            return self._json(401, {"error": "niet ingelogd"})

        if pad in ("/", "/index.html"):
            return self._stuur(200, pagina(), "text/html; charset=utf-8",
                               [("Cache-Control", "no-store")])

        if pad == "/bridge":
            return self._json(200, {"bridge": True, "zenden": ZENDEN,
                                    "mesh": bool(self._beheerder())})

        if pad == "/mesh":
            if not self._beheerder():
                # Geen 401: de bezoeker is wél ingelogd, hij mag dit alleen niet.
                # De pagina valt hierop terug op haar eigen momentopname.
                return self._json(403, {"error": "alleen voor beheer"})
            try:
                return self._json(200, dekking())
            except Exception as exc:
                self.log_message("dekking mislukt: %s", exc)
                return self._json(502, {"error": str(exc)})

        return self._json(404, {"error": "onbekend pad"})

    # ---------------------------------------------------------- POST
    def do_POST(self):
        pad = self.path.split("?", 1)[0]

        if pad == "/login":
            n = int(self.headers.get("Content-Length") or 0)
            veld = urllib.parse.parse_qs(self.rfile.read(n).decode("utf-8", "replace"))
            gegeven = (veld.get("ww") or [""])[0].strip()
            ip = self._ip()
            with _pog_lock:
                mislukt = _pogingen.get(ip, 0)
            if mislukt >= 3:
                # Geen blokkade, wel traag: dat stopt raden zonder iemand die
                # zich vertypt buiten te sluiten.
                time.sleep(min(4.0, 0.5 * mislukt))
            # Hoofdletterongevoelig, met opzet: dit is een gedeeld demowachtwoord
            # en een telefoontoetsenbord maakt er graag 'Firemesh' van. Wil je het
            # strikt, vergelijk dan gegeven met TOEGANG zonder casefold.
            is_gast = hmac.compare_digest(gegeven.casefold(), TOEGANG.casefold())
            # Het beheerderswachtwoord blijft hoofdlettergevoelig: dat is geen
            # gedeeld demowachtwoord maar een echte sleutel.
            is_beheer = bool(BEHEER) and hmac.compare_digest(gegeven, BEHEER)
            if is_gast or is_beheer:
                with _pog_lock:
                    _pogingen.pop(ip, None)
                vlag = "; Secure" if self._https() else ""
                staart = ("; Path=/; Max-Age=" + str(SESSIE_UREN * 3600) +
                          "; HttpOnly; SameSite=Lax" + vlag)
                kop = [("Location", "/"),
                       ("Set-Cookie", "firedeck=" + maak_cookie("gast") + staart)]
                if is_beheer:
                    kop.append(("Set-Cookie",
                                "firedeck_b=" + maak_cookie("beheer") + staart))
                    self.log_message("beheerder ingelogd vanaf %s", ip)
                return self._stuur(302, b"", "text/plain", kop)
            with _pog_lock:
                _pogingen[ip] = mislukt + 1
            self.log_message("mislukte login van %s", ip)
            return self._loginpagina(
                "Wachtwoord klopt niet. Let op: hoofdletters tellen mee.")

        if not self._ingelogd():
            return self._json(401, {"error": "niet ingelogd"})

        if pad != "/send":
            return self._json(404, {"error": "onbekend pad"})

        try:
            n = int(self.headers.get("Content-Length") or 0)
            body = json.loads(self.rfile.read(n) or b"{}")
        except Exception:
            return self._json(400, {"error": "onleesbare JSON"})

        sleutel = (body.get("pub_key") or "").strip()
        tekst = (body.get("text") or "").strip()
        if len(sleutel) != 64:
            return self._json(400, {"error": "pub_key moet 64 hextekens zijn"})
        if not tekst:
            return self._json(400, {"error": "text is leeg"})

        if ZENDEN != "echt":
            self.log_message("NIET verzonden (stand %s) naar %s: %r",
                             ZENDEN, sleutel[:8], tekst[:40])
            return self._json(200, {"sent": True, "gesimuleerd": True, "is_flood": True})

        try:
            res = verstuur(sleutel, tekst, body.get("txt_type", 0))
        except Exception as exc:
            self.log_message("verzenden mislukt: %s", exc)
            return self._json(502, {"sent": False, "error": str(exc)})

        d = res.get("data") or res
        self.log_message("verstuurd naar %s: %r -> %s", sleutel[:8], tekst[:40], d)
        return self._json(200, {"sent": bool(d.get("sent")), "is_flood": d.get("is_flood"),
                                "expected_ack": d.get("expected_ack")})

    def log_message(self, fmt, *args):
        print("[firedeck] " + (fmt % args), flush=True)


if __name__ == "__main__":
    if not TOEGANG:
        TOEGANG = secrets.token_urlsafe(9)
        print("[firedeck] GEEN FIREDECK_PASS gezet; dit keer is het: " + TOEGANG,
              flush=True)

    print("[firedeck] poort %d, openHop %s, zenden=%s, mesh=%s"
          % (POORT, OPENHOP, ZENDEN,
             MESH_STAND if MESH_STAND == "open" else
             ("beheer" if BEHEER else "dicht (geen beheerderswachtwoord gezet)")),
          flush=True)
    ThreadingHTTPServer(("0.0.0.0", POORT), FireDeck).serve_forever()
