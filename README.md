# Minecraft inventory companion voor Eden Duo

[![CI](https://github.com/erikvanhoolst/minecraft-companion/actions/workflows/ci.yml/badge.svg)](https://github.com/erikvanhoolst/minecraft-companion/actions/workflows/ci.yml)

**[Download de nieuwste release](https://github.com/erikvanhoolst/minecraft-companion/releases/latest)** · [Directe download van het installatiepakket](https://github.com/erikvanhoolst/minecraft-companion/releases/latest/download/0100D71004694000.dsmod.zip)

Een companion voor [Eden Duo](https://github.com/igawa6/eden-duo), de Switch-emulator voor Android-handhelds met twee schermen zoals de AYN Thor. Terwijl Minecraft op het bovenste scherm draait, toont het onderste scherm live je inventory: bovenaan armor, health, lucht en honger zoals de HUD van de game ze tekent, daaronder de 27 vakken van de hoofdinventory en de 9 hotbar-vakken met het geselecteerde vak, de iconen, de aantallen en de naam van het geselecteerde item zoals de game die toont, in het lettertype van de game.

Een tweede tab, **Map**, toont de wereld rond de speler van bovenaf, zoals de kaart in de game ze tekent: elk blok in de kaartkleur die de game er zelf aan geeft, met schaduw naar hoogte en waterdiepte, op het kaartpapier van de game met de spelermarker in de kijkrichting. Ernaast staan de coördinaten (zoals "Show Coordinates" in de game ze toont) en de kijkrichting; met + en - zoom je tussen 64, 128 en 256 blokken breed. Wisselen tussen de tabs kan met de knoppen bovenaan of door over het scherm te vegen.

Het is geen aanpassing van de game. Eden Duo laadt het als companion-package (`.dsmod.zip`) met een pagina-indeling in JSON en een kleine native module in C++. Die module leest het geheugen van de draaiende game. Het package bevat geen spelbestanden: de iconen, de HUD-plaatjes, het lettertype, de itemnamen, de kaartkleuren en het kaartpapier komen uit je eigen game.

## Ondersteunde versies

| Minecraft | Build | Status |
|---|---|---|
| 1.2.12 (basisgame zonder update) | `D8B7E605E809E80C` | Werkt. Inventory getest op de AYN Thor en de pc; armor, health, lucht, honger, itemnamen en de kaart getest op de pc |
| 1.26.13 (update v148) | `53E6D516A4DA5CD0` | Niet getest: de game draait niet in Eden Duo 1.1.0. Zonder de balken bovenaan en zonder kaart |

Eden Duo 1.1.0 (runtime 18) of nieuwer.

## Installeren

1. Download **`0100D71004694000.dsmod.zip`** van [GitHub Releases](https://github.com/erikvanhoolst/minecraft-companion/releases/latest), of bouw het package zelf (zie hieronder). Kies de installatiezip bij de release-assets.
2. In Eden Duo: houd Minecraft ingedrukt, kies **Add-ons**, **Install**, **Dual screen mods** en kies de zip. Met de Thor via USB aangesloten kan het ook met `scripts/thor.sh install`.
3. Zet de Minecraft-update uit in Add-ons zolang 1.26.13 niet draait.
4. Start Minecraft en open een wereld. Het onderste scherm toont eerst "WAITING FOR A WORLD" en daarna je inventory.

## Bouwen

Nodig: CMake, Ninja, een C++20-compiler, Python 3 en voor Android de NDK r28c.

```sh
cp local.env.example local.env   # paden aanpassen
scripts/build.sh                 # Linux- en Android-module + dist/0100D71004694000.dsmod.zip
```

De ABI-headers van Eden Duo, nlohmann/json en stb_image zitten in `native/`, dus een Eden-checkout is niet nodig.

## CI en releases

GitHub Actions bouwt en controleert het Linux- en Android-package bij pull requests en pushes naar `main`. Een versietag (bijvoorbeeld `v0.3.0`) bouwt en publiceert automatisch een GitHub Release met de installatiezip en `SHA256SUMS`. Zie [docs/RELEASING.md](docs/RELEASING.md) voor de controles en het publiceren van volgende versies.

## Mappen

| Map | Inhoud |
|---|---|
| `native/` | De module: lezer (`mc_reader`), kaart (`mc_map`), iconen en lettertype (`mc_assets`, `mc_zip`), itemnamen (`mc_names`), onderzoeksconsole (`mc_debug`) |
| `package/` | `package.json`, `dualscreen/manifest.json` (de pagina's op het onderste scherm) en `dualscreen/mc_font.txt` (verwijst naar het lettertype van de game) |
| `scripts/` | Bouwen, installeren, testen op de pc en op de Thor |
| `research/` | Analysescripts voor de game-executable en de resource packs |
| `patches/` | Patches voor de desktopversie van Eden Duo om Minecraft op de pc te testen |
| `docs/` | `NOTES.md` (geheugenindeling, iconen), `TOOLS.md` (tools en testen), `PLAN.md` (oorspronkelijk plan) |

## Licentie

GPL-3.0-or-later, zoals Eden Duo en de Eden Duo companions waaruit de ABI-headers en de package-builder komen.
