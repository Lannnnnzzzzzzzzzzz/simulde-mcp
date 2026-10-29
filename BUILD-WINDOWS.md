# Build di Windows — SimulIDE 1.1.0 + MCP Server

Panduan membangun SimulIDE 1.1.0 yang sudah ditempeli server MCP (folder
`simulide-1.1.0/` di paket ini) menjadi aplikasi Windows portable
(`simulide.exe` + folder `data/`), lalu menjalankannya dengan MCP aktif dan
menghubungkannya ke Claude Code atau klien MCP lain.

> **Status kejujuran:** build Windows ini **BELUM diuji**. Yang sudah
> terverifikasi adalah build **Linux** (Qt 5.15.13, qmake) — lihat
> `PORTING.md`. Jalur build-nya sama persis (file proyek qmake yang sama,
> dan `SimulIDE.pro` sendiri memang memuat blok khusus `win32`), tetapi
> langkah di bawah yang bertanda *(pengetahuan umum Qt)* adalah praktik
> standar Qt/qmake di Windows, bukan hasil uji di mesin ini. Kalau ada
> langkah yang mentok, catat pesan errornya — kemungkinan besar hanya perlu
> penyesuaian kecil (path kit, jumlah job paralel, atau DLL).

---

## 1. Prasyarat: install Qt 5.15 + MinGW 64-bit

1. Unduh **Qt Online Installer** dari qt.io (kategori open source) dan
   jalankan. *(pengetahuan umum Qt: versi 5.15 terakhir yang tersedia untuk
   open source adalah 5.15.2.)*
2. Pada layar pemilihan komponen, centang:
   - **Qt → 5.15.2 → MinGW 64-bit** — komponen ini yang memuat compiler
     MinGW dan semua modul Qt yang dibutuhkan proyek ini.
   - **Developer and Designer Tools → Qt Creator** (biasanya sudah
     tercentang) dan toolchain **MinGW 64-bit** bila ditawarkan terpisah.
3. Modul Qt yang benar-benar diminta file proyek (terverifikasi dari
   `simulide-1.1.0/SimulIDE.pro`) adalah:

   `core, gui` (bawaan default qmake) + `svg, xml, widgets, concurrent,
   serialport, multimedia, network`

   Jadi **tidak** perlu mencentang komponen tambahan seperti Qt Charts,
   Qt WebEngine, atau MSVC — cukup kit MinGW 64-bit di atas.
4. Catatan: proyek ini menjalankan `lrelease` secara otomatis saat build
   (target `runLrelease` di `.pro`, untuk mengompilasi file terjemahan).
   `lrelease` ikut terpasang bersama kit Qt, jadi tidak ada langkah
   tambahan — cukup tahu supaya tidak kaget melihatnya di log build.

## 2. File proyek yang dibuka: `build_XX/SimulIDE_Build.pro`

Di dalam folder `simulide-1.1.0/` ada dua file `.pro`:

- `SimulIDE.pro` (di root) — **jangan dibuka langsung**. File ini hanya
  disertakan (`include(...)`) oleh file satunya; ia memakai path relatif
  `../src` dan variabel `BUILD_DIR` yang tidak terdefinisi bila dibuka
  sendirian.
- `simulide-1.1.0/build_XX/SimulIDE_Build.pro` — **ini yang dibuka**.
  Isinya menetapkan `BUILD_DIR = $$PWD` lalu meng-`include` `SimulIDE.pro`.
  Inilah pintu masuk yang sama dengan build Linux yang terverifikasi.

Kode addon MCP di `simulide-1.1.0/src/remote/` tidak perlu didaftarkan
manual: `SimulIDE.pro` mengambil semua `src/*.cpp` secara rekursif (glob),
jadi addon otomatis ikut ter-build dan hanya aktif bila environment
variable `SIMULIDE_MCP` diset saat aplikasi dijalankan.

## 3. Cara build

### Pilihan A — Qt Creator (paling gampang)

1. Buka Qt Creator → **File → Open File or Project…** → pilih
   `simulide-1.1.0/build_XX/SimulIDE_Build.pro`.
2. Pada layar *Configure Project*, pilih kit **Desktop Qt 5.15.2 MinGW
   64-bit** dan mode **Release**, lalu *Configure*.
3. Klik **Build → Build Project** (atau tombol palu). Build pertama cukup
   lama karena source-nya besar (di Linux: 376 file objek).
4. Hasil build **tidak** jatuh di folder shadow-build Qt Creator, melainkan
   mengikuti `DESTDIR` di file `.pro`:

   ```
   simulide-1.1.0\build_XX\executables\SimulIDE_1.1.0-SR1\
   ```

### Pilihan B — Baris perintah (Qt MinGW prompt)

1. Buka shortcut **"Qt 5.15.2 (MinGW 64-bit)"** dari Start Menu (jendela
   cmd yang PATH-nya sudah diarahkan ke kit Qt). *(pengetahuan umum Qt)*
2. Jalankan:

   ```bat
   cd /d <folder-paket>\simulide-1.1.0\build_XX
   qmake SimulIDE_Build.pro CONFIG+=release
   mingw32-make -j4
   ```

   `-j4` = 4 job paralel; turunkan (mis. `-j2`) bila RAM laptop pas-pasan.
   (Di sandbox Linux build dijalankan `-j1` karena RAM sangat sempit.)

## 4. Hasil build: folder portable

Folder hasil (`build_XX\executables\SimulIDE_1.1.0-SR1\`) berisi:

- `simulide.exe`
- folder `data\` dan `examples\` — disalin otomatis oleh target
  `copy2dest` di file `.pro` dari `resources\`

Bentuknya portable persis seperti paket `SimulIDE_1.1.0-SR2_Win64` dari
dosen: tidak ada installer, folder ini boleh dipindah/dibagikan utuh dan
exe dijalankan dari dalamnya. **Jangan menjalankan exe dari folder lain**
tanpa folder `data\` di sebelahnya — aplikasi mencari datanya relatif
terhadap lokasi exe.

Bila exe gagal jalan karena DLL Qt tidak ketemu (gejala: pesan
"…dll was not found" saat folder dijalankan di luar lingkungan Qt
Creator), jalankan `windeployqt` dari jendela Qt MinGW prompt:

```bat
windeployqt <folder-hasil>\simulide.exe
```

*(pengetahuan umum Qt: `windeployqt` menyalin semua DLL Qt + runtime
MinGW yang dibutuhkan ke sebelah exe.)*

## 5. Menjalankan dengan MCP aktif

Server MCP menyala **hanya** bila environment variable `SIMULIDE_MCP`
diset **sebelum** aplikasi dijalankan. Cara tercepat (cmd):

```bat
cd /d <folder-hasil>\SimulIDE_1.1.0-SR1
set SIMULIDE_MCP=1
simulide.exe
```

Port bawaan adalah **8723**. Untuk mengganti port:

```bat
set SIMULIDE_MCP_PORT=8724
```

Perintah `set` hanya berlaku di jendela cmd yang sama — jalankan
`simulide.exe` dari jendela cmd itu juga. Alternatif permanen:
**System Properties → Environment Variables → User variables → New…**
(`SIMULIDE_MCP` = `1`), setelah itu tutup dan buka ulang aplikasi
(terminal/aplikasi yang sudah terlanjur jalan tidak akan melihat nilai
baru).

Server hanya mengikat `127.0.0.1` (localhost) — tidak terbuka ke jaringan,
sesuai desain addon aslinya.

## 6. Tes cepat koneksi

Dengan SimulIDE berjalan dan MCP aktif, jalankan dari cmd lain
(Windows 10/11 sudah membawa `curl.exe` bawaan — *pengetahuan umum*):

```bat
curl -X POST http://127.0.0.1:8723/mcp -H "Content-Type: application/json" -d "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-03-26\",\"capabilities\":{},\"clientInfo\":{\"name\":\"test\",\"version\":\"1.0\"}}}"
```

Jawaban yang benar berupa JSON dengan `serverInfo` bernama **`simulide1`**
versi **`1.1.0`**. Bila port diganti di langkah 5, sesuaikan angka port di
URL ini dan di konfigurasi klien.

## 7. Menghubungkan klien MCP

### Claude Code

```bat
claude mcp add --transport http simulide1 http://127.0.0.1:8723/mcp
```

### Klien lain (konfigurasi JSON generik)

```json
{
  "mcpServers": {
    "simulide1": {
      "url": "http://127.0.0.1:8723/mcp"
    }
  }
}
```

Syaratnya: klien MCP dan SimulIDE berjalan di **mesin yang sama**
(server hanya mendengar di localhost), dan SimulIDE-nya harus build
porting ini — SimulIDE bawaan tidak mempunyai server MCP di dalamnya.

## 8. Troubleshooting singkat

| Gejala | Penyebab paling mungkin | Obat |
|---|---|---|
| Klien MCP *connection refused* | SimulIDE belum jalan, atau jalan tanpa `SIMULIDE_MCP=1` | Tutup SimulIDE, jalankan ulang persis seperti langkah 5 (satu jendela cmd yang sama untuk `set` + exe) |
| Port 8723 sudah dipakai aplikasi lain | Bentrok port | Set `SIMULIDE_MCP_PORT` ke angka lain sebelum menjalankan exe, dan samakan angka itu di URL klien |
| Env sudah diset permanen tapi MCP tetap mati | Aplikasi/terminal dibuka sebelum env diubah | Tutup total aplikasi (dan terminal klien), buka ulang setelah mengubah Environment Variables |
| `…dll was not found` saat menjalankan exe | DLL Qt/runtime belum ikut | Jalankan `windeployqt` pada exe (langkah 4) |
| Build berhenti di tengah / `mingw32-make` error memori | Job paralel terlalu banyak untuk RAM | Turunkan `-j` (mis. `-j2` atau `-j1`), tutup aplikasi berat lain |
| Qt Creator membuka proyek yang salah/aneh | Yang dibuka `SimulIDE.pro` root | Tutup, buka `build_XX/SimulIDE_Build.pro` (langkah 2) |

Detail teknis porting (peta API v1↔v2, hasil uji lengkap, sisa pekerjaan)
ada di `PORTING.md`.
