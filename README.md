# YtGst

YtGst är en liten YouTube-spelare för Linux som bygger på tre byggstenar:

- **yt-dlp** hämtar information och ström-länkar från YouTube.
- **GStreamer** avkodar och spelar upp ljud och bild.
- **Qt 6 / QML** ritar hela gränssnittet och videon på skärmen.

Målet är en enkel, snabb spelare som lägger så lite arbete som möjligt på
processorn (CPU) genom att använda **grafikkortet (GPU)** både för att avkoda
och för att visa videon. Appen är gjord för vanliga skrivbordsdatorer med
Linux, ett fungerande grafikkort och drivrutiner.

Appen består av två fönster:

1. **YtGst** – sökfönstret. Här söker du och får en lista med videokort.
2. **YtGst Player** – spelfönstret. Det öppnas som en **egen process** när du
   klickar på en video, så att sökfönstret kan ligga kvar öppet samtidigt.

---

## Innehåll

- [Funktioner](#funktioner)
- [Så funkar det – yt-dlp → GStreamer → Qt](#så-funkar-det--yt-dlp--gstreamer--qt)
- [Rendering, GPU och prestanda](#rendering-gpu-och-prestanda)
- [Beroenden](#beroenden)
- [Installera yt-dlp](#installera-yt-dlp)
- [Var hamnar nedladdade filer?](#var-hamnar-nedladdade-filer)
- [Bygg och kör](#bygg-och-kör)
- [Kompileringsflaggor](#kompileringsflaggor)
- [Kodstruktur](#kodstruktur)
- [Undertexter](#undertexter)
- [Tangentbord och mus](#tangentbord-och-mus)
- [Felsökning](#felsökning)
- [Kända begränsningar](#kända-begränsningar)

---

## Funktioner

- Sökning direkt mot YouTube (via YouTubes egna interna API, "Innertube").
- Oändlig listning (fler resultat laddas när du scrollar).
- Videokort med miniatyr, titel, kanal, visningar, ålder och längd.
- Gilla-markeringar hämtas i bakgrunden för de kort du ser.
- Uppspelning i eget fönster med:
  - play/paus, seekbar med tidsbubbla och "scrub",
  - uppspelningshastighet (0,25x–2x),
  - upplösningsval (Auto + tillgängliga nivåer),
  - undertexter (manuella och automatiskt översatta),
  - volymreglage och mute,
  - fullskärm,
  - nedladdning av videon till disk.
- Styrningen tonas ut automatiskt efter 5 sekunder utan musrörelse.

---

## Så funkar det – yt-dlp → GStreamer → Qt

YtGst blandar tre program. Den enklaste beskrivningen är att **yt-dlp är
"letaren"**, **GStreamer är "motorn"** och **Qt är "ritaren"**.

### 1. Sökfönstret (YtGst)

`src/youtube.cpp` skickar en sökfråga till YouTubes interna söktjänst med
Qt:s nätverksklass `QNetworkAccessManager`. Svaret är JSON som plockas isär och
läggs in i en lista (`VideoListModel`). QML ritar listan som videokort.

### 2. Spelaren (YtGst Player)

När du klickar på ett kort startas en ny YtGst-process med argumentet
`--play <video-id>`. Spelarprocessen gör då följande:

1. **yt-dlp** körs med flaggan `-j` ("dump JSON"). Den returnerar all metadata
   om videon, inklusive färdiga **ström-länkar** för bild och ljud, samt
   undertextspår. YtGst föredrar YouTubes **HLS-strömmar** (`.m3u8`) eftersom
   GStreamer kan spola (seeka) i dem.
2. **GStreamer** bygger en pipeline av två `playbin`-element:
   - en för **bild** och en för **ljud**. De delar samma klocka, vilket ger
     synkron ljud och bild,
   - bildströmmen skickas genom
     `glupload → glcolorconvert → capsfilter (RGBA) → qml6glsink`.
     `qml6glsink` lämnar över varje bildruta till ett QML-element av typen
     `GstGLQt6VideoItem` som ligger i spelarfönstret.
3. **Qt / QML** ritar videoytan och alla knappar, menyer och undertexter ovanpå.

Förenklat flöde:

```text
Sökfönstret "YtGst"
   |  klick på video
   v
Nytt "YtGst Player"-fönster (egen process)
   |
   +- yt-dlp -j  ------------>  metadata + HLS-länkar (bild/ljud/undertext)
   |
   +- GStreamer-pipeline
         +- playbin "vplay"  ->  glupload -> glcolorconvert -> RGBA -> qml6glsink --+
         |                                                                          |
         +- playbin "aplay"  ->  autoaudiosink                                     |
                                                                                    v
                                                          QML: GstGLQt6VideoItem (bilden)
                                                          QML: knappar, seekbar, undertext
```

---

## Rendering, GPU och prestanda

YtGst är byggt för att **använda grafikkortet och undvika mjukvarurendering**.
Det ger lägre CPU-belastning och jämnare uppspelning.

- **Hårdvaruavkodning (decode):** När det finns en fungerande VA-API-driver
  väljer GStreamer en hårdvarudekoder (t.ex. `vah264dec`) i stället för en
  mjukvarudekoder (`avdec_h264`). YtGst höjer den valda dekoderns "rank", så
  den väljs först. Se [Kompileringsflaggor](#kompileringsflaggor).
- **GPU-uppladdning och rendering:** Bildrutorna läggs i **GPU-minne**
  (`GLMemory`) och ritas av Qt:s grafikkortsbaserade scen. Bilden lämnar i
  princip aldrig grafikkortet i onödan.
- **Ingen CPU-färgkonvertering:** Färgkonverteringen sker på GPU:n
  (`glcolorconvert`) i stället för på CPU:n.

### OpenGL, OpenGL ES och Vulkan

Ritningen sker via **OpenGL / OpenGL ES**. Anledningen är att GStreamers
`qml6glsink` kräver en **OpenGL-kontext** för att kunna lämna bildrutor till
Qt. YtGst låser därför Qt till OpenGL i `src/main.cpp`.

Qt 6 kan i sig använda flera grafikkorts-API:er (så kallade RHI-backends),
inklusive **Vulkan**, OpenGL ES och OpenGL, med automatisk fallback. Det gäller
dock Qt:s egen ritning – **GStreamer 1.24 har ingen Vulkan-sink för Qt**, så
just videorenderingen kan inte köras på Vulkan i dag. Därför är appen knuten
till OpenGL/OpenGL ES. Skulle Vulkan bli aktuellt krävs en annan GStreamer-sink
än `qml6glsink`.

> **Viktigt:** Ett fungerande grafikkortsdrivrutin krävs. Saknas den kan Qt
> falla tillbaka på *software OpenGL* (t.ex. `llvmpipe`), vilket innebär att
> CPU:n får göra jobbet. Det motsäger hela poängen med YtGst. Kontrollera att
> hårdvaruavkodning och GL fungerar (se [Felsökning](#felsökning)).

---

## Beroenden

YtGst kräver följande. Paketnamnen nedan gäller Debian/Ubuntu-liknande system.

### Byggverktyg

| Verktyg | Varför |
| --- | --- |
| `cmake` (>= 3.16) | Byggsystem |
| `g++` (C++17) | Kompilator |
| `pkg-config` | Hittar GStreamer |

### Qt 6 (>= 6.2)

| Paket | Varför |
| --- | --- |
| `qt6-base-dev` | Qt-grunder, nätverk |
| `qt6-declarative-dev` | Qt Quick / QML |
| `qml6-module-qtquick` | QML-modulen `QtQuick` |
| `qml6-module-qtquick-controls` | QML-modulen `QtQuick.Controls` |

### GStreamer 1.0

| Paket | Vad det ger |
| --- | --- |
| `libgstreamer1.0-dev` | Utvecklingshuvuden för GStreamer |
| `libgstreamer-plugins-base1.0-dev` | Huvuden för bas-plugins |
| `libgstreamer-gl1.0-dev` | Huvuden för GL-plugins (`gstreamer-gl-1.0`) |
| `gstreamer1.0-plugins-base` | Grundplugins (t.ex. `playbin`) |
| `gstreamer1.0-plugins-good` | Bl.a. `souphttpsrc` (HTTP) |
| `gstreamer1.0-plugins-bad` | Bl.a. `hlsdemux` (HLS) och `vah264dec` (VA-API) |
| `gstreamer1.0-libav` | Mjukvarudekodrar (`avdec_h264`) som reserv |
| `gstreamer1.0-gl` | `glupload`, `glcolorconvert`, GL-sinks |
| `gstreamer1.0-qt6` | `qml6glsink` – bryggan mellan GStreamer och Qt |

### Grafikkort / videoavkodning (VA-API)

Välj det som matchar ditt kort:

| Paket | För |
| --- | --- |
| `i965-va-driver` | Äldre Intel (Haswell/Broadwell m.fl.) |
| `intel-media-va-driver` | Nyare Intel (`iHD`) |
| `mesa-va-drivers` | AMD / Mesa |
| NVIDIA | NVIDIA:s egen drivrutin (NVDEC) |

### yt-dlp

Se nästa avsnitt.

---

## Installera yt-dlp

yt-dlp är ett fristående program (Python) som YtGst startar som en
underprocess. Det ingår **inte** i YtGst och måste installeras separat.

### Var ska yt-dlp ligga?

YtGst letar efter yt-dlp i denna ordning:

1. **Sökvägen som anges vid bygget** via CMake-flaggan `YTGST_YTDLP_PATH`.
   Den kompileras in i programmet och används om filen finns.
2. **`PATH`** – om ingen sökväg angetts (eller om den inte finns) söker YtGst
   i systemets `PATH`, precis som ett vanligt kommando.

Praktiskt innebär det att yt-dlp kan ligga nästan var som helst, så länge den
hittas. Vanliga platser:

- `/usr/bin/yt-dlp` eller `/usr/local/bin/yt-dlp` (systeminstallation),
- `~/.local/bin/yt-dlp` (användarinstallation, t.ex. via pip/pipx),
- en godtycklig sökväg som du pekar ut med `YTGST_YTDLP_PATH`.

### Installera

Rekommenderat (senaste versionen som fristående binär, ingen Python-miljö):

```bash
mkdir -p ~/.local/bin
curl -L https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp \
  -o ~/.local/bin/yt-dlp
chmod +x ~/.local/bin/yt-dlp
# se till att ~/.local/bin finns i PATH, annars:
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc
```

Alternativ via pakethanterare:

```bash
sudo apt install yt-dlp        # kan vara en äldre version
# eller
pipx install yt-dlp
```

Uppdatera gärna yt-dlp då och då, eftersom YouTube ändrar sig ofta:

```bash
yt-dlp -U          # för den fristående binären
pipx upgrade yt-dlp
```

---

## Var hamnar nedladdade filer?

När du klickar på **nedladdningsknappen** (eller trycker **D**) startar YtGst
yt-dlp för att ladda ner videon till disk.

- **Mapp:** systemets nedladdningsmapp, normalt **`~/Downloads`**.
  Om den inte kan hittas används filmmappen, och i sista hand hemkatalogen.
- **Filnamn:** `%(title)s [%(id)s].%(ext)s`, t.ex.
  `Never Gonna Give You Up [dQw4w9WgXcQ].mkv`.
- **Format:** bästa tillgängliga bild + ljud, sammanslagna till en **MKV-fil**
  (`--merge-output-format mkv`).
- **Förlopp:** en procentsiffra visas i en liten ruta ovanför knappen.
- **Avbryt:** klicka på knappen igen (eller tryck **D** igen) medan den laddar.

YtGst kör yt-dlp med ungefär dessa argument:

```text
--newline --no-warnings --no-playlist
-P <nedladdningsmapp>
-o "%(title)s [%(id)s].%(ext)s"
-f bestvideo+bestaudio/best
--merge-output-format mkv
<videons webbadress>
```

---

## Bygg och kör

```bash
git clone <repo-url> ytgst
cd ytgst
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/ytgst
```

Vill du installera systemet:

```bash
sudo cmake --install build
```

---

## Kompileringsflaggor

Vissa saker bestäms redan när programmet byggs. Det gör att YtGst kan anpassas
till olika datorer utan att koden ändras.

| Flagga | Standard | Betydelse |
| --- | --- | --- |
| `YTGST_YTDLP_PATH` | tom (hittas automatiskt) | Sökväg till yt-dlp. Tom = auto via `find_program` och sedan `PATH`. |
| `YTGST_VIDEO_DECODER` | `vah264dec` | Vilken GStreamer-videodekoder som ska prioriteras. |
| `YTGST_VAAPI_DRIVER` | tom (auto `i965`) | Värde till `LIBVA_DRIVER_NAME`, t.ex. `i965` eller `iHD`. |

Exempel – peka ut yt-dlp och välj en annan dekoder:

```bash
cmake -S . -B build \
  -DYTGST_YTDLP_PATH=/home/din-anvandare/.local/bin/yt-dlp \
  -DYTGST_VIDEO_DECODER=vah265dec \
  -DYTGST_VAAPI_DRIVER=iHD
cmake --build build -j
```

Vanliga dekoder-värden:

| Värde | Typ |
| --- | --- |
| `vah264dec` / `vah265dec` | Intel/AMD VA-API (H.264 / H.265) |
| `nvdec_h264` / `nvh264dec` | NVIDIA NVDEC |
| `avdec_h264` | Mjukvarudekoder (CPU) – används bara som reserv |

Bygget skriver ut vad som valdes:

```text
-- yt-dlp: /home/din-anvandare/.local/bin/yt-dlp
-- Videodekoder: vah264dec
```

---

## Kodstruktur

```text
ytgst/
├── CMakeLists.txt          Byggregler, beroenden och kompileringsflaggor
├── qml/
│   ├── Main.qml            Sökfönstret "YtGst"
│   ├── PlayerWindow.qml    Spelfönstret "YtGst Player" med alla kontroller
│   ├── SearchBar.qml       Sökfältet
│   ├── VideoCard.qml       Ett videokort i listan
│   └── Spinner.qml         Laddningsindikator
└── src/
    ├── main.cpp            Startpunkt, val av grafikkorts-API, start av spelarprocess
    ├── player.cpp/.h       Spelaren: yt-dlp, GStreamer-pipeline, undertext, nedladdning
    ├── youtube.cpp/.h      Sökning mot YouTube (Innertube)
    ├── videomodel.cpp/.h   Listmodell med videokort
    └── ytdlp.h             Hittar yt-dlp (kompilerad sökväg eller PATH)
```

### Kort om varje fil

- **`src/main.cpp`** – Programstart. Initierar GStreamer, väljer VA-API-driver
  och prioriterad videodekoder, låser Qt till OpenGL, och skapar antingen
  sökfönstret eller (vid `--play`) ett spelarfönster. Klassen `Launcher`
  startar spelaren som en egen process.
- **`src/player.cpp` / `player.h`** – Hjärtat i uppspelningen. Hämtar metadata
  med yt-dlp, bygger GStreamer-pipelinen, sköter play/paus, seek, hastighet,
  upplösning, volym, undertexter och nedladdning. Allt exponeras till QML via
  `Q_PROPERTY` och `Q_INVOKABLE`-metoder.
- **`src/youtube.cpp` / `youtube.h`** – Skickar sökbegäranden och
  " continuation"-begäranden (fler sidor) till YouTubes interna API och tolkar
  JSON-svaret. Hämtar även gilla-markeringar i bakgrunden med yt-dlp.
- **`src/videomodel.cpp` / `videomodel.h`** – En `QAbstractListModel` som håller
  listan av videor (id, titel, kanal, visningar, längd, miniatyr, m.m.).
- **`src/ytdlp.h`** – Liten hjälpare som returnerar sökvägen till yt-dlp.
- **`qml/Main.qml`** – Sökfönstret: sökfält, lista och felmeddelanden.
- **`qml/PlayerWindow.qml`** – Spelfönstret: videoyta, seekbar, play/paus,
  hastighet, upplösning, undertexter, fullskärm, nedladdning och volym.
  Kontrollerna tonas ut efter 5 sekunder.

---

## Undertexter

YtGst stödjer både **manuella** undertexter (som skapats av kanalen) och
**automatiskt genererade/översatta** undertexter.

1. När videon läses in listas alla tillgängliga spår i CC-menyn.
2. När du väljer ett språk hämtas undertexten och sparas i en **cache per
   språk**, så att växling fram och tillbaka går snabbt.
3. Undertexten tolkas till tidsstämplade rader (cues) och visas i en ruta
   längst ner. Formatet är i första hand YouTubes `json3`, annars VTT.

### Cookies från webbläsaren

YouTube kräver numera att förfrågan är autentiserad för att lämna ut
automatiskt översatta undertexter (annars svarar servern `HTTP 429`). YtGst
löser det genom att låta yt-dlp hämta **cookies från din webbläsare** i
samband med att videon läses in.

- YtGst letar efter en installerad webbläsare (Firefox, Chromium, Chrome,
  Brave, Edge, Vivaldi, Opera) och använder dess cookies.
- Cookies skrivs till en **tillfällig fil** som läses in och **raderas direkt**
  efteråt. Själva cookien skickas bara till `youtube.com`.
- Är du inloggad på YouTube i webbläsaren fungerar undertexterna som bäst.
  Saknas webbläsare fungerar fortfarande manuella undertexter.

---

## Tangentbord och mus

| Tangent / handling | Funktion |
| --- | --- |
| Klick på video (sökfönstret) | Öppnar videon i ett nytt spelarfönster |
| Klick på videoytan | Play/paus (via play-knappen) |
| `M` | Mute av/på |
| `Upp` / `Ner` | Höj / sänk volymen |
| `D` | Starta / avbryt nedladdning |
| `F` eller `F11` | Fullskärm av/på |
| `Esc` | Lämna fullskärm, annars stäng fönstret |
| Dra i seekbaren | Spola till valfri tid |

---

## Felsökning

**"qml6glsink saknas – installation av gstreamer1.0-qt6 krävs"**

```bash
sudo apt install gstreamer1.0-qt6
```

**"yt-dlp hittades inte"**

Installera yt-dlp och se till att den finns i `PATH`, eller bygg med
`-DYTGST_YTDLP_PATH=/sökväg/till/yt-dlp`.

**Svart bild / ingen video**

Kontrollera att grafikkortet och OpenGL fungerar:

```bash
glxinfo | grep "OpenGL renderer"     # ska visa ditt grafikkort, inte llvmpipe
vainfo                                # ska visa VA-API-profiler
```

**Hackig uppspelning / hög CPU**

- Kontrollera att hårdvaruavkodning används: spela upp med `GST_DEBUG=3` och
  leta efter `vah264dec`, eller testa `gst-inspect-1.0 vah264dec`.
- Testa en annan dekoder/VA-driver via kompileringsflaggorna.
- Se till att du inte kör på software OpenGL (`llvmpipe`).

**Undertexter fungerar inte (t.ex. bara tyska/spanska)**

Logga in på YouTube i din webbläsare och se till att webbläsaren finns
installerad. Automatiskt översatta undertexter kräver cookies.

**Felsök med GStreamer**

```bash
GST_DEBUG=3 ./build/ytgst            # mycket information
GST_DEBUG=*:4 ./build/ytgst 2> gst.log
```

---

## Kända begränsningar

- **Enbart GPU-rendering.** Saknas fungerande grafikkortsdrivrutin kan Qt
  falla tillbaka på software OpenGL och då blir CPU-belastningen hög.
- **Ingen Vulkan för videon.** GStreamer 1.24 har ingen Vulkan-sink för Qt;
  videon ritas därför med OpenGL/OpenGL ES (se
  [Rendering, GPU och prestanda](#rendering-gpu-och-prestanda)).
- **HLS föredras.** YtGst väljer YouTubes HLS-strömmar eftersom de går att
  spola i. Vissa höga upplösningar kan saknas i HLS och då används bästa
  tillgängliga alternativ.
- **Undertexter kräver ibland inloggning** (cookies) på YouTube.
- **Nätverk krävs alltid.** Varken sökning eller uppspelning fungerar offline.
- **yt-dlp måste vara någorlunda uppdaterad.** YouTube ändrar sitt upplägg
  med jämna mellanrum.

