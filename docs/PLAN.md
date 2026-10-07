# Plan: Minecraft-inventory op het tweede scherm van Eden Duo

Datum: 2026-10-05. Status: onderzoek afgerond, plan ter review. Nog niets gebouwd.

## 1. Wat het onderzoek opleverde

### Wat Eden Duo is

Eden Duo is een fork van de Switch-emulator Eden voor Android-handhelds met twee schermen
(ontwikkeld op de AYN Thor; ook gebruikt op de Retroid Pocket 6 met dual-screen add-on).
De game draait op het bovenste scherm. Het onderste scherm toont een **companion**: een
live, aanraakbare pagina (kaart, inventory, status) die elke frame uit het geheugen van de
draaiende game wordt opgebouwd. Laatste release: 1.1.0 van 3 oktober 2026, runtime-versie 18.

Belangrijk gevolg: wat jij "een mod" noemt, is in Eden Duo-termen een **companion-package**.
Je past de game zelf niet aan en je hoeft de emulator niet te herbouwen. Het package wordt via
het Add-ons-menu van Eden Duo geïnstalleerd als `0100D71004694000.dsmod.zip`.

### Hoe een companion in elkaar zit

| Onderdeel | Taal/formaat | Rol |
|---|---|---|
| `package.json` | JSON | Identiteit: title ID, naam, versie, `min_runtime` |
| `dualscreen/manifest.json` | JSON | Pagina's, widgets (`rect`, `label`, `value`, `bar`, `button`, `pips`, `image`, `map`, `chart`), acties, afgeleide waarden |
| `dualscreen/<BUILD16>.json` | JSON | Per game-build: pointer chains naar geheugenadressen, symbolen, patches |
| `dualscreen/modules/<platform>/<TITLEID>.so` | C of C++ tegen een stabiele C-ABI | Optioneel. Leest complexe game-state, publiceert waarden, decodeert plaatjes uit de romfs |
| Tooling | Python 3, CMake, Ninja, clang, Android NDK | Package bouwen, module compileren voor Linux en Android arm64 |

De companion-ontwikkelaar werkt op Linux met een desktop-build van Eden Duo (`eden-cli`) die
een tweede venster of een headless tweede scherm heeft, screenshots van beide schermen maakt,
scriptbare knopdrukken accepteert en een geheugenconsole biedt (zoeken, pointer-chains vinden,
hexdumps). Er is ook een GDB-stub. Dit is precies de workflow die de bestaande acht companions
(Persona 5 Royal, Metroid Dread, Link's Awakening, Mario Kart 8, Mario Wonder, Animal Crossing,
Fire Emblem, Isaac) gebruikt hebben.

Spelregel van het project: het package bevat **geen game-assets**. Iconen, fonts en teksten
worden tijdens het spelen uit de eigen romfs-dump van de speler gelezen. Een inventory-icoon
komt dus uit de PNG-texturen in de romfs van Minecraft, niet uit ons zip-bestand.

### Wat Minecraft op de Switch is

- Minecraft op Switch is **Bedrock Edition**: een C++-engine zonder officiële mod-API buiten
  add-ons. Title ID `0100D71004694000`.
- Versienummering is sinds 2026 jaargebonden (26.x), met een update om de paar weken. Elke
  update geeft een nieuw build-ID en kan geheugenlay-outs verschuiven. Dit is het grootste
  onderhoudsrisico van dit project.
- De binary is gestript (geen symbolen). Maar de klassenlay-out is goed gedocumenteerd via de
  Bedrock Dedicated Server-symbolen die de LeviLamina-community in headers heeft omgezet.
  Daaruit is de datastructuur bekend die we moeten volgen:

```
Player
 └─ unique_ptr<PlayerInventory> mInventory
      ├─ int mSelected                       (geselecteerd hotbar-slot 0..8)
      ├─ ItemStack mInfiniteItem
      ├─ unique_ptr<Inventory> mInventory    (Inventory : FillingContainer : Container)
      │    ├─ vector<ItemStack> mItems       (36 slots: 0..8 hotbar, 9..35 hoofdinventory)
      │    └─ Player* mPlayer                (terugwijzer, handig voor validatie)
      └─ vector<ItemStack> mComplexItems

ItemStack : ItemStackBase
 ├─ WeakPtr<Item> mItem        → Item: HashedString mFullName ("minecraft:diamond_sword"),
 │                                      string mIconName, short mMaxDamage, uchar mMaxStackSize
 ├─ unique_ptr<CompoundTag> mUserData   (NBT, o.a. "Damage" voor durability)
 ├─ Block const* mBlock
 ├─ short mAuxValue
 └─ uchar mCount
```

De exacte byte-offsets gelden voor de Windows-server (MSVC) en zullen op de Switch (clang,
libc++, ARM64) anders zijn. De **vorm** is wel gelijk, dus het reverse-engineeren wordt "offsets
meten", niet "structuur ontdekken".

## 2. Architectuurkeuze

| Optie | Oordeel |
|---|---|
| **A. Eden Duo companion-package** (manifest + native module) | **Gekozen.** Dit is het mechanisme waarvoor het tweede scherm bedoeld is. Geen game-patch, geen emulator-rebuild, installeerbaar via het Add-ons-menu, bruikbaar door anderen. |
| B. Code-mod in de game zelf (exlaunch) die via de `dsm:u`-service van Eden Duo naar het tweede scherm tekent | Afgewezen. Vereist hooks in de Bedrock-renderer plus een eigen tekenlaag in de game. Veel meer werk, veel fragieler bij updates. |
| C. Externe app die via de GDB-stub geheugen leest | Afgewezen. Werkt niet op een Android-handheld zonder losse pc. |

Binnen optie A: **manifest plus native C++-module**, niet alleen JSON. Reden: slotaantallen
zijn met pointer chains in de JSON te doen, maar "welk item is dit en welk icoon hoort erbij"
vraagt het lezen van strings uit de `Item`-klasse, het opzoeken van texture-paden in de
resource-pack-JSON van de game en het decoderen van PNG's. Dat is modulewerk. De module volgt
de projectregels: begrensde reads, globals via de accessor-instructies van de game zelf
vinden, torn reads verwerpen, per domein "fail closed".

Een goedkope tussenstap bestaat ook: een manifest-pagina met `mirror` toont een uitsnede van het
gameframe (bijvoorbeeld de hotbar) op het onderste scherm. Nuttig als rooktest van de pipeline
in fase 1, niet als eindproduct.

## 3. Functioneel ontwerp

Canvas 1240×1080 (de standaard van de bestaande companions; Eden Duo schaalt naar het paneel).

**v1 (doel van dit plan)**
- Raster van 9×4 slots zoals in het inventory-scherm van de game: onderaan de hotbar (slot 0..8),
  daarboven drie rijen hoofdinventory (slot 9..35).
- Per slot: icoon uit de romfs, aantal rechtsonder, leeg slot leeg.
- Geselecteerd hotbar-slot gemarkeerd (uit `mSelected`).
- Statuspagina als de build niet ondersteund wordt of de speler niet in een wereld is
  (gates `inv.ready`, `build_match`, `module_ready`).

**v2**
- Armor (4) en offhand (1).
- Durability-balk per item (uit NBT `Damage` en `Item::mMaxDamage`).
- Itemnaam bij tap, gelokaliseerd via de `.lang`-bestanden in de romfs
  (`texts/nl_NL.lang`, `en_US.lang`).
- Tap op een hotbar-slot selecteert het in de game (`write`-actie op `mSelected`, eerst
  verifiëren dat de HUD dit volgt zonder packet of extra state).

**Bewust buiten scope**: inventory verplaatsen via drag-and-drop op het tweede scherm (vereist
reproduceren van `Inventory`-mutaties met alle neveneffecten), containers (kisten) en crafting.

Blokken worden in de game als isometrisch kubusje getekend; wij tonen de zijtextuur plat. Dat
is een acceptabele versimpeling voor v1.

## 4. Technisch ontwerp

### Datapad

1. **Statische wortel vinden.** Bedrock heeft weinig globals. Twee routes, in volgorde:
   - pointer chain vanaf `main+X` (MinecraftGame → ClientInstance → LocalPlayer → PlayerInventory),
     gevonden met de console (`findi` op slotaantallen, `ptrto` terug naar `main`), bevestigd uit
     de disassembly en getest op een tweede boot;
   - terugvaloptie: heap-scan in de module op de vtable van `Inventory`/`FillingContainer`
     (`main+vtable`), gevalideerd via `mPlayer`-terugwijzer en de vector-invarianten
     (`begin <= end <= cap`, 36 elementen). Onder NCE op Android verschuift de heap; daarvoor
     bestaan `get_heap_begin/end` en `__relocation_delta`.
2. **Per slot** `mCount`, `mAuxValue`, `mItem` → `Item` → `mFullName`/`mIconName`,
   `mMaxDamage`; optioneel `mUserData` → NBT `Damage`.
3. **Torn-read-bescherming.** Snapshot van alle 36 ItemStacks tweemaal lezen en vergelijken;
   bij verschil "niet klaar" publiceren.
4. **Kosten.** Alleen samplen terwijl de inventory-pagina zichtbaar is. Per slot een hash van de
   ruwe ItemStack-bytes bijhouden en `Item`-strings alleen herlezen als die hash verandert.

### Iconen

- De module leest uit de romfs (verwachte paden, te bevestigen in fase 2):
  `data/resource_packs/vanilla*/textures/item_texture.json` (itemnaam → texturepad),
  `textures/terrain_texture.json` en `blocks.json` (blok → zijtextuur), en de PNG's zelf.
- De module serveert elk icoon als `module:mc:icon/<fullname>` via `load_image`
  (PNG-decodering met `stb_image.h`, header-only). Het manifest gebruikt per slot een
  `image`-widget met `src_bind` op de door de module gepubliceerde tekst `inv.slot{i}.icon`.
- Onbekend item (bijvoorbeeld uit een marketplace-pack): een neutraal vraagteken-icoon dat wij
  zelf tekenen (`file:`-bron, eigen werk, dus toegestaan).

### Gepubliceerde waarden (module → manifest)

| Naam | Type | Betekenis |
|---|---|---|
| `inv.ready` | int | 1 als een geldige, niet-torn snapshot gelezen is |
| `inv.selected` | int | Geselecteerd hotbar-slot 0..8 |
| `inv.slot{i}.count` | int | Aantal, 0 = leeg (i = 0..35) |
| `inv.slot{i}.icon` | text | `module:mc:icon/...` of leeg |
| `inv.slot{i}.name` | text | Gelokaliseerde naam (v2) |
| `inv.slot{i}.dur` | int | Durability in procent, -1 = n.v.t. (v2) |
| `inv.diag` | text | Waarom een domein gesloten is (voor debug-pagina) |

### Manifest

- Eén `image`-widget met `repeat: 36`, `repeat_cols: 9` voor de iconen; één `value`-widget met
  dezelfde repeat voor de aantallen (`hide_eq: 0`); een `rect` met `x_bind` op `inv.selected`
  voor de markering. De hotbar-rij krijgt een eigen offset met `repeat_row_dy`.
- `min_runtime: 17` (we gebruiken `{i}` in alle repeat-velden en `expr`).
- Lettertype: het ingebouwde blokfont van de runtime. Bedrock's font is een PNG-glyphvel; een
  `decode_font`-extensie is mogelijk maar niet nodig voor v1.

### Build-pinning en updates

- `module.build_ids` en `supports_build()` accepteren alleen builds die geverifieerd zijn.
- Alle code-offsets (accessor, vtables) en struct-offsets staan in één tabel per build-ID in de
  module. Een nieuwe Minecraft-update betekent: tabel aanvullen, 36 slots opnieuw valideren,
  package herbouwen. De fingerprint-aanpak (opcodes van de getter controleren) maakt dat
  routinewerk in plaats van een nieuwe zoektocht.

## 5. Stappenplan

| Fase | Inhoud | Resultaat / controle |
|---|---|---|
| 0. Voorwaarden (jij) | Toestel met Eden Duo 1.1.0; eigen NSP-dump van Minecraft inclusief update, `prod.keys`; versie noteren | Zie §7 |
| 1. Toolchain | Eden Duo desktop bouwen met `-DEDEN_DSMOD_BUILD_DEV_TOOLS=ON`; companions-repo gekloond (al gebeurd); een bestaande module (bijv. Wonder) compileren als rooktest; Minecraft starten in `eden-cli --aux-window` met een minimaal manifest (één label plus `mirror` van de hotbar) | Tweede venster toont onze pagina terwijl Minecraft draait |
| 2. Dump en statische analyse | `eden-cli --dump-romfs`; build-ID uit `main` (offset 0x40); bevestigen dat de texturen PNG zijn en welke JSON-mappings er zijn; NSO decomprimeren en in Ghidra (Switch-loader) laden; via strings als `"Damage"`, `"minecraft:"` en de item-registry de klassen `ItemStack`, `Item`, `Inventory` en hun vtables lokaliseren; `sizeof(ItemStack)` en veld-offsets meten | Offset-tabel voor deze build, met bewijs uit de disassembly |
| 3. Live analyse | Testwereld met herkenbare stapels (7, 13, 21 stuks); console `findi`/`cluster` → adres van `mItems`; `ptrto` → chain naar `main+X`; tweede boot met andere wereld bevestigt de chain; anders terugval op vtable-scan | Chain of scan die op twee boots klopt |
| 4. Proof of concept (alleen JSON) | Datafile met `inv.slot{i}.count` als pointer chain met stride `sizeof(ItemStack)`; manifest met 36 `value`-widgets | Aantallen op scherm 2 identiek aan het inventory-scherm van de game; sentinel-test met `writeb` |
| 5. Native module | `native/modules/0100D71004694000.cpp`: resolver, snapshot, torn-check, icon-lookup, `load_image`; CMake-target `dsmod-mc`; Linux- en Android-build; package met SHA-256-pins | Iconen en aantallen live; `sample`-kosten gemeten in µs |
| 6. UI | Layout 9×4, markering, lege-slot-stijl, statuspagina, debug-pagina; daarna v2-items | Screenshots scherm 1 naast scherm 2 voor elk veld |
| 7. Toestel | Package installeren via Add-ons; NCE-verschillen (heap, relocatie-delta) controleren; vastleggen met scrcpy | Werkt op het echte toestel |
| 8. Oplevering | `tools/build_dualscreen_package.py`; README met ondersteunde build; optioneel PR naar het companions-repo | Installeerbare `0100D71004694000.dsmod.zip` |

Fase 2 en 3 zijn de onzekere fases; de rest is bekend terrein dat de bestaande companions al
hebben gelopen. Fase 7 kan alleen jij uitvoeren, met het toestel in de hand.

## 6. Risico's en maatregelen

| Risico | Maatregel |
|---|---|
| Minecraft-updates om de paar weken verschuiven offsets | Build-pinning; fingerprinted accessors; offset-tabel per build; één vaste versie kiezen en in de emulator niet updaten tot de companion bijgewerkt is |
| Geen statische wortel naar `LocalPlayer` | Vtable-heap-scan in de module, met page-table-heapgrenzen onder NCE; scan verspreid over ticks |
| Texturen in de Switch-romfs in een ander formaat dan PNG, of verspreid over meerdere `vanilla_*`-packs | Fase 2 bevestigt dit vóór het ontwerp vastligt; de runtime kent PNG, DDS en de Nintendo-formaten; mappings over packs heen samenvoegen |
| Items uit marketplace-packs zonder icoon in de vanilla-romfs | Fallback-icoon plus naam |
| Prestaties op de handheld (Minecraft is al zwaar) | Alleen samplen als pagina zichtbaar; per-slot caching; `sample`-tijd meten per de projectregel |
| Desktop-build van Eden Duo is groot (Vulkan, SDL3, clang; tientallen minuten compileren) | Eenmalig; daarna alleen de module herbouwen |
| Juridisch | Alleen eigen dump; package zonder assets, conform de regels van het companions-repo |

## 7. Wat ik van jou nodig heb voordat de uitvoering kan starten

1. **Toestel**: welk dual-screen toestel heb je (AYN Thor, Retroid Pocket 6 met add-on, iets
   anders) en welke Eden Duo-versie staat erop?
2. **Game**: heb je een eigen dump van Minecraft (NSP, inclusief de update die je speelt) en je
   `prod.keys`? Welke Minecraft-versie toont het hoofdmenu? De build die jij speelt is de build
   die ik ondersteun.
3. **Ontwikkelmachine**: mag ik Eden Duo op deze Arch-machine bouwen (enkele GB aan bronnen en
   build, Vulkan-GPU nodig om de game te draaien)? Waar staan de dump en de keys?
4. **Scope**: is v1 (alleen tonen: 36 slots, iconen, aantallen, selectie) de juiste eerste
   oplevering, met armor, durability, namen en tap-selectie als v2?
5. **Taal** van de labels op het tweede scherm: Nederlands of Engels?

## 8. Bronnen

- Eden Duo: https://github.com/igawa6/eden-duo en releases https://github.com/igawa6/eden-duo/releases
- Companions-repo, docs en voorbeeldpackages: https://github.com/igawa6/eden-duo-companions
  (lokaal gekloond in `eden-duo-companions/`; lees `docs/CONTRIBUTE.md`, `docs/PORTING_A_GAME.md`,
  `docs/PACKAGE_FORMAT.md`, `docs/MODULE_GUIDE.md`)
- Bedrock-klassenlay-outs (uit BDS-symbolen): https://github.com/LiteLDev/LeviLamina
  (`src/mc/world/item/ItemStack.h`, `ItemStackBase.h`, `Item.h`,
  `src/mc/world/inventory/FillingContainer.h`, `src/mc/world/actor/player/PlayerInventory.h`, `Player.h`)
- Bedrock client-modding ter referentie: https://github.com/frederoxdev/amethyst
- Minecraft Switch title ID: https://ec.nintendo.com/apps/0100d71004694000/AU?lang=en
- Bedrock-versiegeschiedenis: https://minecraft.wiki/w/Pocket_Edition_version_history
- Alternatieve route (afgewezen), exlaunch: https://github.com/Coxxs/exlaunch

## 9. Voortgang (bijgewerkt 2026-10-05, avond)

Antwoorden van Erik: AYN Thor via USB (Eden Duo 1.1.0, niet 1.0.0), game en keys op het toestel, v1-scope akkoord, Engelse labels.

Gedaan:
- Toolchain in user-space (adb, cmake, ninja, hactool, NDK r28c, Ghidra 12.1.2); Eden Duo desktop gebouwd met GCC 16 (`eden-duo/build/bin/eden-cli`).
- Minecraft base + update v1.26.13 (build `53E6D516A4DA5CD0`) van de Thor gehaald, exefs/romfs uitgepakt; vtables van `Inventory`, `PlayerInventory`, `ItemStack`, `Item`, `LocalPlayer` gevonden via RTTI (`device/minecraft/analysis/rtti.json`).
- Romfs bevestigd: PNG-texturen, `item_texture.json`/`blocks.json`/`terrain_texture.json`; icoon-lookup geprototypet (`tools/minecraft/icon_resolver.py`) en in C++ gezet (`native/modules/mc_assets.*`).
- Package-skelet (`packages/Minecraft`), module-skelet (`0100D71004694000.cpp`, `mc_reader.*`), CMake-target `dsmod-mc`; module compileert voor Linux én Android arm64 en wordt door de runtime geladen.
- Desktop-emulator liet Minecraft crashen; drie lokale patches in `eden-duo/` (IPv6-socket weigeren, DeviceShared-attribuut tolereren bij transfer memory, diagnostische logging). Op de Thor is dit niet nodig: de Thor-log toont dat de game daar na het inschakelen van airplane mode gewoon opstart.

Open: game tot in de wereld krijgen op de desktop, live offsets meten (fase 3), reader invullen, UI afronden, test op de Thor.

Aanvulling (23:00): Minecraft start nu wél op de desktop (drie lokale Eden-patches), de module laadt en scant het geheugen, maar de Intel-iGPU levert alleen een zwart beeld (OpenGL crasht), zodat ik de wereld niet blind kan laden en de laatste offsets niet kan verifiëren. Verificatie verschuift naar de Thor: package `dist/0100D71004694000.dsmod.zip` (Linux + Android-module), onderzoeksconsole in de module (`tools/minecraft/thor.sh dbg ...`, uitvoer in de Eden-log). Statisch bepaald: ItemStack-stride 0x98, count +0x22, aux +0x20, item-handle +0x08; mItems +0x140; Item: mIconName +0x58, mIconFrame +0x48, mFullName +0xE8 (vtable-adrespunten = vtable + 0x10).
