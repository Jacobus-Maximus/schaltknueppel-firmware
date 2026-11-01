# Schaltknüppel Installer

Bereitet ein neues **Waveshare ESP32-S3-Touch-LCD-1.46B** für die Schaltknüppel-Firmware vor. Du flashst diesen
Sketch einmal mit der Arduino IDE; danach holt die Schaltknüppel-App die neueste Firmware von GitHub und spielt sie
per Bluetooth auf. Ab dann laufen alle Updates über die App.

Der Installer enthält nur, was dafür nötig ist: Bluetooth, eine Versionsantwort und den Empfang des Updates.
Er nimmt ausschließlich Firmware des Projekts `schaltknueppel` an und nur in den ersten 15 Minuten nach dem Einschalten.

## Was du brauchst

- Arduino IDE 2.x
- Boardpaket **esp32 von Espressif Systems**, Version 3.x (Werkzeuge › Board › Boardverwalter)
- Bibliothek **Adafruit GFX Library** (Werkzeuge › Bibliotheken verwalten; „Adafruit BusIO“ wird mit installiert)
- USB-C-Datenkabel
- Die Schaltknüppel-App auf deinem Android-Handy

## Einstellungen im Menü „Werkzeuge“

| Einstellung | Wert |
|---|---|
| Board | ESP32S3 Dev Module |
| Flash Mode | DIO 80MHz |
| Flash Size | 16MB (128Mb) |
| PSRAM | OPI PSRAM |
| Partition Scheme | Custom (nimmt die `partitions.csv` aus diesem Ordner) |
| USB CDC On Boot | Enabled |
| Erase All Flash Before Sketch Upload | Enabled |

## So geht's

1. Diesen Ordner `Schaltknueppel_Installer` herunterladen und `Schaltknueppel_Installer.ino` öffnen.
2. Einstellungen wie oben wählen, Port des Boards auswählen und **Hochladen**.
   Wird das Board nicht gefunden: BOOT-Taste gedrückt halten, USB einstecken, Taste loslassen.
3. Das Display zeigt **„Bereit zum Update“**.
4. Die Schaltknüppel-App öffnen und verbinden. Sie findet das Board als **Schaltknueppel Setup** und bietet die
   neueste Firmware an. „Installieren“ antippen – das dauert 1 bis 2 Minuten.
5. Das Board startet neu in die Schaltknüppel-Firmware und führt dich durch die Einrichtung
   (Sprache, Ausrichtung, Handy koppeln, Gänge).

Zeigt das Display **„Zeit abgelaufen“**, sind seit dem Einschalten mehr als 15 Minuten vergangen: Board einmal neu
starten (Reset oder USB kurz trennen) und es nochmal versuchen.

Den seriellen Monitor (115200 Baud) kannst du offen lassen; dort steht, was der Installer gerade macht.

## Lizenzen

- `esp_lcd_spd2010.c` / `.h`: © Espressif Systems, Apache License 2.0
- Adafruit GFX Library: © Adafruit Industries, BSD License
- Arduino-ESP32 und ESP-IDF: © Espressif Systems, Apache License 2.0 / LGPL 2.1

---

## English

Prepares a new **Waveshare ESP32-S3-Touch-LCD-1.46B** for the Schaltknüppel firmware. Flash this sketch once with
the Arduino IDE (settings in the table above), open the Schaltknüppel app, connect to **Schaltknueppel Setup** and
install the offered firmware. The installer only accepts firmware of the project `schaltknueppel`, and only within
15 minutes after power-on. Needs the esp32 board package 3.x and the "Adafruit GFX Library".
