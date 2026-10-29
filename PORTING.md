# Porting MCP Server ke SimulIDE 1.1.0 — Log & Peta Kerja

Misi: porting addon MCP dari simulide2-mcp-server (JamesClarke7283, untuk SimulIDE 2)
ke SimulIDE 1.x yang dipakai user di laptop (format file `.sim1`).

## Status ringkas

- [x] Tahap 1 — Bedah source v1 + banding arsitektur v2 (selesai, lihat di bawah)
- [x] Tahap 2 — Tulis kode addon `src/remote/` untuk v1 — 22 tool (21 target + `remove_wire`)
- [x] Tahap 3 — Build (Qt5/qmake, -j1) — SELESAI, binary di
  `simulide-1.1.0/build_XX/executables/SimulIDE_1.1.0-SR1/simulide` (10,8 MB, nol error)
- [x] Tahap 4 — Smoke test DC (battery + resistor, hukum Ohm) — LULUS (lihat Hasil)
- [x] Tahap 5 — Rangkaian BJT CE + ukur VE/VC/IC — SELESAI (lihat Hasil)
- [x] Tahap 6 — Uji kompatibilitas file `.sim1` user (SR2 rev 260312) — LULUS (lihat Hasil)
- Versi user TERKONFIRMASI dari folder Drive-nya: **SimulIDE_1.1.0-SR2_Win64**
  (SimulIDE 1.1.0 Service Release 2, Windows 64-bit) — seri yang sama dengan target. ✓
- **Catatan selisih SR (risiko kompatibilitas, dicatat eksplisit sesuai instruksi):**
  source yang dipakai di sini adalah pohon git Launchpad seri 1.1.0 (HEAD `f24d903`),
  BUKAN tarball source bertanda persis "SR2". Lebih tepatnya `SimulIDE.pro` pohon ini
  menamai dirinya `VERSION="1.1.0"`, `RELEASE="-SR1"` — jadi basisnya **SR1**, satu
  service release di bawah biner user (**SR2**), walau HEAD git-nya adalah commit
  lanjutan seri 1.1.0. Dampak praktis: format `.sim1` satu seri (`type="simulide_1.0"`),
  sehingga risiko ketidakcocokan format dinilai rendah; perbedaan yang mungkin muncul
  hanya perilaku/bugfix komponen antara SR1-basis (hasil build ini) vs SR2 (biner user).
  Uji file `.sim1` asli milik user (pertemuan5simulator.sim1) tetap dijadwalkan begitu sampel tiba.
- File `.sim1` sampel user SUDAH TIBA: `~/workspace/simulide1-mcp/samples/pertemuan5simulator.sim1`
  (9.904 byte, salinan file asli user). Header terverifikasi:
  `<circuit version="1.1.0-SR2" rev="260312" ...>` — ditulis oleh **1.1.0-SR2 rev 260312**.
  Isi (terverifikasi dengan grep): 2 Battery (5 V), 2 BJT, 7 Resistor, 2 Capacitor,
  2 Voltimeter, 12 Node, 34 Connector (sebagai `<item itemtype="Connector">`).
  **UJI KOMPATIBILITAS WAJIB:** hasil build porting harus bisa `open_circuit` file ini
  tanpa error dan membaca komponennya dengan benar; hasil uji dilaporkan eksplisit.
- Selisih rev source vs target (dicatat): pohon source yang dipakai ter-build dengan
  define `APP_VERSION="1.1.0-SR1"` dan `REVNO="261029"` (label .pro masih SR1, rev build
  261029), sedangkan target persis user adalah SR2 rev 260312. Satu seri 1.1.0 yang sama;
  pembuktian kompatibilitas = uji buka file sampel di atas, bukan klaim.

## Sumber SimulIDE 1 yang dipakai

- Pohon source: `~/workspace/simulide1-mcp/simulide-1.1.0/` — salinan dari checkout yang
  sudah ada di sandbox (`~/workspace/simulide2-mcp/build/lp-simulide`).
- Provenance: clone git resmi **https://git.launchpad.net/simulide**, HEAD `f24d903`
  ("Cleaning and minor issues."). Folder asli tidak diubah; semua kerja di salinan.
- Versi terverifikasi dari isi source: `SimulIDE.pro` baris 2 → `VERSION = "1.1.0"`.
- Build system: qmake, `SimulIDE.pro` mengambil SEMUA `src/*.cpp`/`src/*.h` secara rekursif
  (glob), jadi file addon di `src/remote/` otomatis ikut ter-build. Modul Qt yang diminta:
  svg, xml, widgets, concurrent, serialport, multimedia (+ `network` yang DITAMBAHKAN patch ini).

## Arsitektur addon v2 (yang diporting)

Tiga lapisan di `addon/src/remote/` (repo v2):
1. `mcpserver.{h,cpp}` — HTTP server MCP (JSON-RPC, POST /mcp, bind 127.0.0.1).
   Praktis tidak tergantung API SimulIDE kecuali memanggil `RemoteServer::self()->call()`
   dan teks serverInfo/instructions. → **pakai ulang hampir utuh** (ganti nama/versi/teks).
2. `mcptools.cpp` — tabel tool MCP (nama → perintah internal + skema JSON).
   → **pakai ulang, dipangkas** ke subset yang diporting.
3. `remoteserver.{h,cpp}` — server socket lokal (NDJSON via QLocalServer) + implementasi
   semua perintah terhadap kelas GUI (Circuit, Component, Simulator, ...).
   → **tulis ulang handler** mengikuti API v1 (ternyata sangat mirip, lihat tabel).
- Integrasi v2: objek dibuat di konstruktor `CircuitWidget`; aktif via env
  `SIMULIDE_MCP=1`/`SIMULIDE_MCP_PORT` (default port 8723) atau tombol toolbar.
  Untuk v1: hook yang sama di konstruktor `CircuitWidget`, aktivasi env saja
  (tombol toolbar ditunda — dicatat sebagai perbedaan sadar).

## Peta API: v1 (1.1.0) vs v2 (yang diasumsikan addon)

| Kebutuhan | v2 (addon asli) | v1 (1.1.0) | Dampak |
|---|---|---|---|
| Singleton Circuit | `Circuit::self()` | sama (`circuit.h`) | nihil |
| Daftar komponen | `compList()` | sama | nihil |
| Cari komponen | `getCompById(id)` | sama | nihil |
| Buat komponen | `createComponent(type,name,pos)` | `createItem(type, id)` + `addComponent(comp)` + `setPos()`; id dibuat sendiri: `name+"-"+newSceneId()` (pola sama seperti CircuitView v1) | handler ditulis ulang (kecil) |
| Properti | `setPropStr/getPropStr`, `properties()` → grup `propList` | identik (`compbase.h`: CompBase sama-sama leluhur) | nihil |
| Pin komponen | `getPins()` → QList | `getPins()` → std::vector | adaptasi sepele |
| Cari pin by id | `Circuit::getPin(id)` (registry) | tidak ada getter registry publik; `findPin(x,y,id)` berbasis posisi | implementasi sendiri: scan compList × getPins, cocokkan `pinId()` |
| Format pin id | `<CircId>-lPin` dsb. | sama persis (cek resistor/bjt: `-lPin`, `-rPin`, `-base`, `-collector`, `-emiter`) | nihil |
| Tegangan pin | `pin->getVoltage()` | sama (ePin) | nihil |
| Pin terhubung | `pin->isConnected()` | sama (ePin) | nihil |
| Wiring | `newconnector(pin,save)` + `closeconnector(pin,save)` | nama & tanda tangan sama (`circuit.h`) | nihil |
| Connector | `getUid/startPinId/endPinId/pointList/getVoltage/isBus` | semua ada (Connector : CompBase) | nihil, kecuali **tidak ada `getCurrent()`** → `read_nets` v1 tanpa arus |
| Power | `CircuitWidget::powerCircOn/Off()` | sama | nihil |
| Simulator | `isRunning/isPaused/simState/circTime/pauseSim/resumeSim/psPerSec/realSpeed` | semua ada (`simulator.h`) | nihil |
| Jeda presisi | `pauseAt()`/`sim_run_for` (hasil patch v2) | tidak ada | `sim_run_for` & `capture_waveform` **tidak diporting** (pengukuran DC cukup: power on → tunggu → baca → power off) |
| Simpan | `saveCircuit(path)` → bool, ekstensi `.sim2` | sama, ekstensi `.sim1` | ganti ekstensi |
| Buka/baru | `CircuitWidget::loadCirc/newCircuit` | sama; dialog modal "save changes?" dicegah dengan trik judul `*` yang sama (`suppressModifiedDialog`) | nihil |
| XML rangkaian | `Circuit::circuitToString()` publik | **private** di v1 | `get_circuit_xml` via simpan ke file sementara lalu baca isinya |
| Library | `ItemLibrary::self()->items()` (name/category/type) | identik (`itemlibrary.h`) | nihil |
| Screenshot | `Circuit::render(...)` ke QImage | sama (QGraphicsScene::render) | nihil |
| Qt Network | `QT += network` sudah ada di .pri v2 | **belum ada** di SimulIDE.pro v1 | patch 1 baris: `QT += network` |
| Versi app | `APP_VERSION` + `REVNO` | hanya `APP_VERSION` (dari .pro) | `simulide_status` tanpa rev |
| Format file | `.sim2`, `<circuit type="simulide_2...">` | `.sim1`, `<circuit type="simulide_1.0" ...>`, properti = atribut XML | hanya relevan utk open/save |

Kesimpulan bedah: kedua seri berbagi leluhur kode yang sama; mayoritas handler v2
bisa dipindahkan dengan perubahan kecil. Risiko utama bukan API, melainkan build
(source besar, RAM sandbox sempit → wajib -j1) dan perbedaan perilaku runtime kecil.

## Tool yang diporting (target tahap 2)

`simulide_status`, `new_circuit`, `open_circuit`, `save_circuit`, `get_circuit_xml`,
`list_library`, `list_components`, `list_wires`, `add_component`, `remove_component`,
`set_property`, `get_property`, `get_properties`, `get_pins`, `connect_pins`,
`set_power`, `set_pause`, `get_sim_state`, `read_voltage`, `read_nets`, `screenshot`
(21 tool — termasuk `list_wires`, `get_property`, `set_pause` yang murah karena API-nya ada).

Belum diporting (sengaja, catat sebagai sisa pekerjaan): semua tool debug MCU
(`debug_*`, `read_ram`, `write_ram`), `load_firmware`, `capture_waveform`, `sim_run_for`,
`read_current` (v1 tak punya arus per-connector), `read_bus`, serial
(`serial_send/read/set_serial_port_open`), input (`press_button`, `set_switch`, `send_key`),
`duplicate_component`, `import_circuit`, `set_wire_points`, `set_wire_bus`,
`circuit_settings`, zoom/app_screenshot/undo/redo/quit (sebagian trivial, ditunda dulu).

## Hasil verifikasi (nyata, dari output tool)

- **Smoke test DC** (Battery 5 V + Resistor 100 Ω + Ground, cabang ground via Node):
  tegangan di resistor **4,99995 V**, sisi ground ≈ 0 V. Selisih 50 µV dari 5 V persis
  jatuh pada resistansi internal Battery 1 mΩ pada arus 49,9995 mA — hukum Ohm
  konsisten penuh. Pelajaran runtime: (1) satu pin hanya menerima SATU konektor,
  percabangan wajib lewat komponen **Node** (addon kini mendukung `add_component`
  type `"Node"` secara khusus karena Node tidak ada di item library; pin Node =
  `<id>-0/1/2`); (2) polaritas Battery v1: **lPin = +, rPin = −**.
  Bukti: `smoke_results.json`, `smoke-screenshot.png`, skrip `smoke_test.py`.
- **Uji kompatibilitas sampel user** (`samples/pertemuan5simulator.sim1`, ditulis
  SR2 rev 260312): `open_circuit` berhasil tanpa error; terbaca 27 komponen
  (2 Battery, 2 BJT, 7 Resistor, 2 Capacitor, 2 Voltimeter, 12 Node) + 34 wire —
  persis isi file. Battery "5 V", BJT Gain "100" terbaca; power on dan tegangan
  pin BJT-9 terukur (base −4,95 V dsb). Bukti: `compat_results.json`,
  `compat-screenshot.png`, skrip `compat_test.py`.
- **BJT CE** (Vcc 12 V, R1 20k, R2 3,6k, RC 1,2k, RE 220, CE 100 µF, C1/C2 10 µF
  dengan simbol µ — "10 u" ter-parse 10 F sama seperti di v2, BJT Gain 100,
  WaveGen Sine Freq "1 kHz" (satuan properti v1 = kHz), Semi_Ampli 10 mV,
  Always_On; 125 sampel): VB 1,685 V; VE 1,052 V; VC rata-rata 6,38 V
  (ayunan 4,05–8,21 V); IC ≈ 4,68 mA (jatuh RC) / 4,73 mA (IE×β/(β+1));
  IE 4,78 mA; VBE 0,634 V; **penguatan tegangan ≈ 207×** (Vout dibaca lewat
  komponen Probe — pin bebas tanpa terminal terbaca 0). Teori: VE ≈ 1 V,
  IC ≈ 4,58 mA, VC ≈ 6,5 V, gain ≈ 200× — cocok, dan praktis identik dengan
  hasil SimulIDE 2 sesi sebelumnya (VE 1,051 V, IC 4,75 mA, gain ≈ −205×).
  Bukti: `bjt_results.json`, `bjt-ce-amp.sim1` (header `version="1.1.0-SR1"
  rev="261029"`; dibuka ulang OK: 21 komponen, 22 wire), `bjt-ce-amp.png`,
  skrip `bjt_test.py`.

## Cara menjalankan

```bash
cd ~/workspace/simulide1-mcp/simulide-1.1.0/build_XX/executables/SimulIDE_1.1.0-SR1
DISPLAY=:99 SIMULIDE_MCP=1 SIMULIDE_MCP_PORT=8723 \
  SIMULIDE_REMOTE_SOCKET=/tmp/simulide1-remote-user.sock ./simulide
# MCP HTTP: http://127.0.0.1:8723/mcp — initialize -> serverInfo "simulide1"/"1.1.0"
```
Klien uji: `mcp_client.py` (env `MCP_PORT`, default 8723) + ketiga skrip di atas.

## Sisa pekerjaan

- Tool v2 yang belum diporting (daftar di bagian "Tool yang diporting" di atas)
  + tombol toolbar untuk menyalakan MCP (saat ini aktivasi hanya lewat env)
  + jembatan stdio Python ala v2 (klien HTTP langsung terbukti cukup;
  pola jembatan v2 bisa disalin bila diperlukan klien MCP standar).
- Build Windows belum diuji (user memakai Windows; hasil ini terverifikasi
  di Linux sandbox saja — source & .pro sama, jalur build qmake identik).
- Source persis SR2 rev 260312 tidak tersedia publik dalam jangkauan sesi ini;
  yang dipakai rev 261029 berlabel SR1. Kompatibilitas format `.sim1` terbukti
  dua arah dalam seri 1.1.0 (file SR2 user terbuka; file hasil build ini
  format headernya sekeluarga dan terbuka ulang dengan bersih).

## Catatan build

- Qt5 dev belum terpasang di instance VM ini (qmake tidak ada) — perlu
  `apt install qtbase5-dev libqt5svg5-dev libqt5serialport5-dev qtmultimedia5-dev qttools5-dev-tools`.
  Saat mulai, `apt-get update` milik sistem sandbox sedang berjalan memegang lock; tunggu selesai.
- Build WAJIB `-j1` (RAM available sering < 1 GB). Trik shim `nproc`→1 seperti sesi v2 tersedia.
- Jalankan hasil build lewat Xvfb; working directory harus root instalasi agar `data/` ketemu.
