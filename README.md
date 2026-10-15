# SIMLIB Simulation Examples

Repositori ini berisi contoh simulasi kejadian diskrit menggunakan bahasa C dan SIMLIB. Program utama untuk tugas sistem shuttle rental mobil adalah `car_rental.c`.

## Isi repositori

- `car_rental.c` — simulasi shuttle antara Terminal 1, Terminal 2, dan rental mobil.
- `car_rental.out` — contoh hasil simulasi dari program `car_rental.c`.
- `deskripsi_masalah.md` — interpretasi dan asumsi model untuk soal shuttle.
- `simlib.c`, `simlib.h`, `simlibdefs.h` — implementasi dan definisi library SIMLIB yang dibutuhkan untuk kompilasi.
- Berkas `.c`, `.in`, dan `.out` lainnya — contoh program SIMLIB tambahan, seperti antrean dan job shop.

## Kebutuhan

- Compiler C, misalnya GCC dari MinGW-w64/MSYS2 di Windows atau GCC di Linux.
- Berkas SIMLIB yang ada di repositori ini.

## Kompilasi dan menjalankan simulasi shuttle

Buka PowerShell atau terminal pada folder repositori, lalu jalankan:

```powershell
gcc -std=c99 -O2 car_rental.c simlib.c -lm -o car_rental.exe
```

Penjelasan opsi kompilasi:

- `gcc` menjalankan compiler C.
- `-std=c99` memilih standar bahasa C99.
- `-O2` mengaktifkan optimasi compiler.
- `car_rental.c` adalah kode simulasi.
- `simlib.c` adalah implementasi SIMLIB yang dikompilasi bersama program.
- `-lm` menautkan library matematika yang dipakai SIMLIB.
- `-o car_rental.exe` memberi nama `car_rental.exe` kepada hasil kompilasi.

Pada Linux, nama executable dapat diganti, misalnya `-o car_rental`, kemudian jalankan dengan `./car_rental`.

Jalankan program di PowerShell:

```powershell
.\car_rental.exe
```

Program menjalankan simulasi selama 80 jam (4.800 menit) dan menulis laporan ke `car_rental.out` pada folder kerja saat program dijalankan. Jalankan dari folder repo agar output berada di folder repo tersebut. Program menggunakan seed bilangan acak tetap (stream *s* memakai seed 123456789 + 97 × *s*; seed dicetak di bagian atas laporan) supaya hasil dapat diulang.

## Model shuttle

Bus mengikuti rute berulang:

```text
Rental mobil (3) -> Terminal 1 -> Terminal 2 -> Rental mobil (3)
```

Jarak antarlokasi adalah 4,5 mil (rental ke Terminal 1), 1 mil (Terminal 1 ke Terminal 2), dan 4,5 mil (Terminal 2 ke rental). Model memakai kapasitas bus 20 orang, kecepatan 30 mil/jam, waktu henti minimum 5 menit pada tiap lokasi, penumpang turun dan naik satu per satu dengan waktu acak masing-masing, dan antrean FIFO yang tidak dibatasi panjangnya. Laju kedatangan penumpang adalah 14 orang/jam di Terminal 1, 10 orang/jam di Terminal 2, dan 24 orang/jam di rental. Penumpang di terminal menuju rental; penumpang di rental memilih Terminal 1 (0,583) atau Terminal 2 (0,417). Rincian asumsi jarak, waktu naik/turun, dan interpretasi soal ada di [deskripsi_masalah.md](deskripsi_masalah.md).

## Statistik output

Laporan `car_rental.out` menampilkan:

- jumlah kedatangan dan rata-rata/maksimum panjang antrean tiap lokasi;
- rata-rata/maksimum waktu tunggu di antrean tiap lokasi;
- rata-rata/maksimum jumlah penumpang di bus;
- rata-rata, minimum, dan maksimum waktu berhenti bus per lokasi;
- rata-rata, minimum, dan maksimum waktu dari keberangkatan di rental ke keberangkatan berikutnya dari rental;
- rata-rata, minimum, dan maksimum waktu penumpang di dalam sistem (dari datang di antrean sampai selesai turun), dikelompokkan menurut lokasi asal.

Waktu penumpang di dalam sistem hanya dihitung untuk perjalanan yang selesai sebelum batas simulasi 80 jam.
