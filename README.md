# SimulIDE 1 MCP Server — Port

Server MCP (Model Context Protocol) untuk **SimulIDE 1.1.0**: hasil porting
addon MCP dari proyek [simulide2-mcp-server](https://github.com/JamesClarke7283/simulide2-mcp-server)
karya JamesClarke7283 (yang dibuat untuk SimulIDE 2) ke seri SimulIDE 1 —
seri yang masih dipakai banyak bahan ajar dan paket perkuliahan
(`.sim1`, contohnya paket `SimulIDE_1.1.0-SR2_Win64`).

Servernya berjalan **di dalam proses SimulIDE itu sendiri** (in-process,
bukan plugin untuk biner bawaan — SimulIDE memang tidak menyediakan API
plugin), membuka endpoint MCP HTTP di **`http://127.0.0.1:8723/mcp`**, dan
menyediakan **22 tool** agar AI agent (Claude Code, dan klien MCP lain)
bisa membangun rangkaian, menyambung pin, mengatur properti komponen,
menjalankan/menjeda simulasi, membaca tegangan, dan mengambil screenshot
kanvas langsung di simulator.

> **English summary:** An unofficial port of the MCP server addon from
> JamesClarke7283's `simulide2-mcp-server` (written for SimulIDE 2) to
> **SimulIDE 1.1.0**. The patched SimulIDE hosts an in-process MCP HTTP
> server on `127.0.0.1:8723` exposing 22 tools to create circuits, wire
> components, run the simulation and read voltages. Verified on Linux;
> the Windows build path is documented but not yet tested.

---

## Status

- ✅ **Build Linux terverifikasi** (Qt 5.15.13, qmake, `make -j1`) —
  binary jalan, MCP menjawab, semua uji di bawah lulus.
- ⚠️ **Build Windows BELUM diuji** — jalur qmake-nya sama dan file
  proyeknya memang memuat blok `win32`, tetapi belum pernah dijalankan
  di mesin Windows. Panduan langkah demi langkah: **BUILD-WINDOWS.md**.
- Basis source: pohon git resmi SimulIDE seri 1.1.0 (label `.pro`:
  `1.1.0-SR1`, rev build 261029). Target aslinya adalah biner user
  `1.1.0-SR2` rev 260312; kompatibilitas format `.sim1` antar-keduanya
  **terbukti lewat uji buka file**, bukan sekadar klaim (lihat Hasil uji).

## Hasil uji (Linux, angka aktual terukur)

| Uji | Hasil |
|---|---|
| Smoke test DC (Battery 5 V + Resistor 100 Ω + Ground) | Tegangan resistor **4,99995 V** — selisih 50 µV persis jatuh pada resistansi internal Battery 1 mΩ; hukum Ohm konsisten |
| Kompatibilitas file user | File `.sim1` asli bertanda `1.1.0-SR2 rev 260312` **terbuka tanpa error**: 27 komponen (2 Battery, 2 BJT, 7 Resistor, 2 Capacitor, 2 Voltimeter, 12 Node) + 34 wire terbaca persis |
| Penguat BJT CE (Vcc 12 V, β=100) | **VE 1,051 V** (teori ≈ 1 V), **IC ±4,73 mA** (teori 4,58 mA), **penguatan ±207×** (input 10 mV) |
| Catu daya trafo (soal ujian: trafo multi-tap + jembatan dioda + Elco 2200 µF) | Keluaran DC **rata-rata 15,81 V**, ripple ±0,07 V p-p, sekunder tap 12 V terukur ±11,87 V RMS |

Rangkaian hasil uji ada di folder `contoh/` (`bjt-ce-amp.sim1`,
`catu-daya-trafo.sim1`) — silakan dibuka di SimulIDE 1 mana pun.

## 22 tool yang tersedia

| Kategori | Tool |
|---|---|
| Rangkaian & status | `simulide_status`, `new_circuit`, `open_circuit`, `save_circuit`, `get_circuit_xml`, `get_sim_state` |
| Library & komponen | `list_library`, `list_components`, `add_component`, `remove_component`, `get_pins` |
| Properti | `set_property`, `get_property`, `get_properties` |
| Wiring | `connect_pins`, `list_wires`, `remove_wire` |
| Kendali simulasi | `set_power`, `set_pause` |
| Baca & tangkap | `read_voltage`, `read_nets`, `screenshot` |

Belum diporting (sisa pekerjaan, dicatat jujur): tool debug MCU
(`debug_*`, baca/tulis RAM), `load_firmware`, `capture_waveform`,
`sim_run_for`, `read_current` (API v1 tidak menyediakan arus per
konektor), serial, `press_button`/`set_switch`/`send_key`, tombol toolbar
aktivasi MCP (saat ini aktivasi hanya lewat environment variable), dan
jembatan stdio Python ala v2.

## Build ringkas

File proyek yang dibuka adalah
**`simulide-1.1.0/build_XX/SimulIDE_Build.pro`** (bukan `SimulIDE.pro` di
root — file itu hanya di-`include` olehnya). Kode addon di
`simulide-1.1.0/src/remote/` otomatis ikut ter-build karena `.pro`
mengambil semua `src/*.cpp` secara rekursif.

- **Windows:** ikuti **BUILD-WINDOWS.md** (Qt 5.15 + MinGW 64-bit,
  Qt Creator atau qmake, hasil portable `simulide.exe` + `data/`).
- **Linux & detail teknis porting** (peta API v1 ↔ v2, catatan build,
  log uji lengkap): lihat **PORTING.md**.

## Menjalankan & menghubungkan klien

Server hanya menyala bila environment variable diset sebelum aplikasi
dijalankan:

```bat
set SIMULIDE_MCP=1
simulide.exe
```

Port bawaan **8723**, ganti dengan `SIMULIDE_MCP_PORT` bila bentrok.
Server hanya mengikat `127.0.0.1` — klien dan SimulIDE wajib di mesin
yang sama.

Claude Code:

```bat
claude mcp add --transport http simulide1 http://127.0.0.1:8723/mcp
```

Klien lain (JSON generik):

```json
{
  "mcpServers": {
    "simulide1": {
      "url": "http://127.0.0.1:8723/mcp"
    }
  }
}
```

## Isi repo

| Path | Isi |
|---|---|
| `simulide-1.1.0/` | Source SimulIDE 1.1.0 terpatch (addon MCP di `src/remote/`) |
| `BUILD-WINDOWS.md` | Panduan build Windows langkah demi langkah |
| `PORTING.md` | Log & peta teknis porting + hasil uji lengkap |
| `PUSH-GITHUB.md` | Cara mempublikasikan repo ini dari laptop Windows |
| `contoh/` | Rangkaian `.sim1` hasil uji (BJT CE, catu daya trafo) |

## Kredit & lisensi

- **SimulIDE** dibuat oleh **Santiago González** dan para kontributor.
  Source di folder `simulide-1.1.0/` berlisensi **GNU AGPL v3** (file
  karya Santiago González) dan **GNU GPL v3** (kode kontribusi penulis
  lain) — persis seperti tertulis di `simulide-1.1.0/copyright.txt`.
  Teks lisensi penuh ada di file `LICENSE` di repo ini (file `COPYING`
  asli dari pohon source SimulIDE, disalin apa adanya).
- **Addon MCP asli** untuk SimulIDE 2 dibuat oleh **JamesClarke7283**
  di repo `simulide2-mcp-server`; kode di `src/remote/` adalah adaptasi
  dari karyanya ke API SimulIDE 1.
- Port ini adalah proyek **tidak resmi** (unofficial), dikerjakan untuk
  keperluan belajar, dan tidak berafiliasi dengan proyek SimulIDE
  maupun JamesClarke7283.
