# CI en releases

`package/package.json` bevat de releaseversie. De eerste publieke release is **v0.3.0**, aansluitend op de bestaande packageversie.

## GitHub Actions

- **CI** (`ci.yml`): bij elke pull request, elke push naar `main` en handmatig via workflow_dispatch. Bouwt Linux x86_64 en Android arm64 met NDK r28c (`28.2.13676358`) op Ubuntu 24.04.
- **Build package** (`build.yml`): gedeelde workflow voor CI en releases. Draait de Python-regressietests, bouwt beide modules, verifieert metadata, modulehashes en ELF-architecturen, laadt de Linux-module en controleert de drie ABI-entrypoints. Controleert ook dat opnieuw verpakken dezelfde ZIP oplevert. Bewaart de zip en `SHA256SUMS` 14 dagen als Actions-artifact.
- **Release** (`release.yml`): bij een push van een `v*`-tag. Controleert dat de tag exact `v<packageversie>` is, voert dezelfde build uit en publiceert de downloads met de bijbehorende release notes. Publiceert pas wanneer de build geslaagd is en de assets aan een draft zijn toegevoegd.

De build heeft alleen leesrechten. Alleen de publicatiejob krijgt `contents: write`, via het ingebouwde `GITHUB_TOKEN`; extra secrets zijn niet nodig. Actions zijn vastgezet op commit-SHA's en Dependabot stelt maandelijks updates voor. CI annuleert achterhaalde runs; een lopende release wordt niet door een volgende push afgebroken.

## Volgende release

1. Pas `version` in `package/package.json` aan, bijvoorbeeld naar `0.3.1`. Houd de runtime-eis gelijk in beide manifesten.
2. Voeg `docs/releases/v0.3.1.md` toe met functies, installatie-instructies en bekende beperkingen.
3. Commit en push de wijzigingen naar `main`; controleer dat CI slaagt.
4. Maak en push de tag op die commit:

   ```sh
   git tag -a v0.3.1 -m 'Minecraft Companion v0.3.1'
   git push origin v0.3.1
   ```

5. Controleer de Release-run en de downloads onder GitHub Releases. De zip heeft steeds dezelfde bestandsnaam, zodat de latest-downloadlink blijft werken.

Gebruik alleen tags van de vorm `vMAJOR.MINOR.PATCH`. De workflow publiceert normale releases en markeert ze als latest. Verplaats geen gepubliceerde tags. Een mislukte run kan opnieuw worden gestart via GitHub Actions; een onafgemaakte draft kan daarbij worden aangevuld. Gepubliceerde releases worden nooit door een rerun overschreven.

## Lokale controle

```sh
python3 -m unittest discover -s tests -v
CMAKE_BUILD_PARALLEL_LEVEL=2 scripts/build.sh all
python3 scripts/verify_release.py dist/0100D71004694000.dsmod.zip --tag v0.3.0
cd dist
sha256sum 0100D71004694000.dsmod.zip > SHA256SUMS
```

De controle vereist Linux x86_64 omdat hij de Linux-module laadt. Tests in een draaiende game op desktop en handheld blijven nodig wanneer de native lezer of UI verandert; CI bevat geen gamebestanden of emulator.
