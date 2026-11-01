# Schaltknüppel ESP

Ein rundes Touch-Display im Schaltknauf: zeigt den eingelegten Gang, Tempo und Tempolimit, Musik, Navigation, Anrufe und
Benachrichtigungen vom Android-Handy. Diese Anleitung erklärt, wie du es selbst baust und einrichtest.
App und Firmware sind kostenlos.

> **Nachbau auf eigene Gefahr.** Das ist ein privates Bastelprojekt, kein geprüftes Produkt. Der Knauf muss fest sitzen,
> darf dich beim Schalten nicht behindern und keine Airbags oder Bedienelemente verdecken. Lies vor der ersten Fahrt
> den Abschnitt [Sicherheit](#9-sicherheit).

In diesem Repository liegen:

- **[Releases](../../releases)** mit der Firmware (`schaltknueppel-X.Y.Z.bin` für Updates, `schaltknueppel-full-X.Y.Z.bin`
  für die erste Installation) und der Android-App (Release **„app“**)
- **[Schaltknueppel_Installer](Schaltknueppel_Installer)**: ein kleiner Arduino-Sketch, der ein neues Board für die erste
  Installation vorbereitet

---

## 1. Was du brauchst

| Teil | Hinweis |
|---|---|
| **Waveshare ESP32-S3-Touch-LCD-1.46B** | Rundes 1,46-Zoll-Touch-Display (412 × 412) mit Lagesensor, Uhr-Chip, microSD und Akku-Anschluss. Genau dieses Modell. |
| LiPo-Akku 3,7 V, 3000 mAh | Stecker MX1.25, 2-polig. **Polung vor dem Einstecken prüfen.** |
| microSD-Karte | 8 bis 32 GB, FAT32, für Bilder und GIFs |
| USB-C-Datenkabel | Zum Laden und für die erste Installation (kein reines Ladekabel) |
| 3D-gedrucktes Gehäuse | siehe Abschnitt 2 |
| Android-Handy | Android 7.0 oder neuer mit Bluetooth LE; für Tempo und Tempolimit GPS und mobile Daten |
| PC | nur einmal, für die erste Installation |

## 2. Gehäuse drucken und einbauen

- Druckdateien: *folgen*
- Material: PLA geht, wird im Sommer im geparkten Auto aber weich. Besser PETG oder ASA.

1. Akku an das Board stecken (Polung prüfen) und im Gehäuse befestigen.
2. microSD-Karte einstecken.
3. Board ins Gehäuse setzen und über seine Befestigungslöcher verschrauben. Der USB-C-Anschluss muss zugänglich bleiben.
4. Den alten Schaltknauf abschrauben und das Gehäuse auf das Gewinde des Schalthebels schrauben. Fest anziehen – der Knauf
   darf sich beim Schalten nicht drehen oder lösen.

Wie herum das Board sitzt, ist egal: Bei der Einrichtung misst es seine Lage und dreht das Bild passend.

## 3. App installieren

1. Auf dem Handy die neueste APK aus dem Release **[app](../../releases/tag/app)** laden
   (`schaltknueppel-app-X.Y.Z.apk`).
2. Öffnen. Android fragt einmal, ob die App installiert werden darf, die du dafür benutzt (Browser oder Dateien) – erlauben.
3. Die App aktualisiert sich danach selbst, sobald es eine neue Version gibt.

## 4. Firmware aufs Board bringen

Ab Werk ist auf dem Board eine Demo von Waveshare. Die Schaltknüppel-Firmware kommt **einmal** vom PC; danach kommen alle
Updates über die App. Drei Wege, such dir einen aus:

### Weg A: Arduino IDE + Installer (empfohlen für Arduino-Nutzer)

1. Den Ordner **[Schaltknueppel_Installer](Schaltknueppel_Installer)** herunterladen und
   `Schaltknueppel_Installer.ino` in der Arduino IDE öffnen.
2. Boardpaket **esp32 von Espressif** (3.x) und die Bibliothek **Adafruit GFX Library** installieren.
3. Einstellungen im Menü „Werkzeuge“:

   | Einstellung | Wert |
   |---|---|
   | Board | ESP32S3 Dev Module |
   | Flash Mode | DIO 80MHz |
   | Flash Size | 16MB (128Mb) |
   | PSRAM | OPI PSRAM |
   | Partition Scheme | Custom |
   | USB CDC On Boot | Enabled |
   | Erase All Flash Before Sketch Upload | Enabled |

4. **Hochladen.** Das Display zeigt „Bereit zum Update“.
5. Die App öffnen. Sie findet das Board als **Schaltknueppel Setup** und bietet die neueste Firmware an:
   **Installieren** antippen (1 bis 2 Minuten). Danach startet das Board in die Schaltknüppel-Firmware.

Der Installer nimmt nur in den ersten 15 Minuten nach dem Einschalten ein Update an. Zeigt er „Zeit abgelaufen“, das Board
einmal neu starten.

### Weg B: im Browser

1. Aus dem [neuesten Firmware-Release](../../releases/latest) die Datei **`schaltknueppel-full-X.Y.Z.bin`** laden
   (die mit „full“ im Namen).
2. In Chrome oder Edge <https://espressif.github.io/esptool-js/> öffnen, **Connect** und das Board wählen.
3. Bei **Flash Address** `0x0` eintragen, die full-Datei wählen, **Program**. Danach das Board kurz vom USB trennen.

### Weg C: mit esptool

```
pip install esptool
esptool --chip esp32s3 write_flash 0x0 schaltknueppel-full-X.Y.Z.bin
```

Findet der PC das Board nicht: **BOOT**-Taste gedrückt halten, USB einstecken, Taste loslassen, nochmal versuchen.

## 5. Einrichtung am Board

Beim ersten Start führt das Board dich durch die Einrichtung:

1. **Sprache** wählen (Deutsch oder Englisch).
2. **Ausrichtung:** Leerlauf einlegen, dann den 4. Gang. Das Board erkennt daraus, wo vorne ist, und dreht das Bild gradgenau.
3. **Handy koppeln:** Am Handy unter Bluetooth **„Schaltknueppel“** wählen. Handy und Board zeigen denselben sechsstelligen
   Code – auf beiden bestätigen.
4. **Bedienung:** wischen nach links (Anzeigen), oben (Menü), unten (Schnellzugriff), rechts (zurück), dazu die Taste.
5. **Gänge einlernen:** Zahl der Gänge wählen, antippen wo der R liegt, dann jeden Gang einmal einlegen.

Danach verbindet sich die App von selbst.

## 6. Freigaben in der App

| Freigabe | Wofür |
|---|---|
| Geräte in der Nähe (Bluetooth) | Verbindung zum Board |
| Benachrichtigungszugriff | Nachrichten, Anrufer, Navigation und Musik aufs Display |
| Standort, am besten „Immer erlauben“ | Tempo, Tempolimit, Blitzerwarnung – auch bei gesperrtem Handy |
| Anrufe annehmen | Annehmen und Ablehnen am Display (optional) |
| Akku-Optimierung aus | sonst friert Android die App bei gesperrtem Handy ein |

Unter *Einstellungen › App & Updates › Im Hintergrund* zeigt die App, was noch fehlt.

## 7. Updates

- **Firmware:** Die App prüft beim Verbinden auf neue Firmware und installiert sie nach deiner Bestätigung per Bluetooth.
  Startet eine neue Version nicht richtig, geht das Board von selbst auf die vorherige zurück.
- **App:** aktualisiert sich selbst.
- Automatische Suche und „nur im WLAN“: *Einstellungen › App & Updates*.

## 8. Wenn etwas nicht geht

| Problem | Lösung |
|---|---|
| PC findet das Board nicht | anderes USB-Kabel (Datenkabel), BOOT-Taste beim Einstecken halten |
| App findet das Board nicht | Bluetooth und Standort am Handy an; Handy entsperren; App einmal öffnen |
| Bild steht schief | am Board *Menü › Einstellungen › Anleitung* und die Ausrichtung neu messen |
| Handy verbindet sich nicht mehr | am Handy die Kopplung „Schaltknueppel“ entfernen, am Board *Handy verbinden* öffnen, neu koppeln |
| Keine Benachrichtigungen | Benachrichtigungszugriff erlauben und die Apps in der App unter „Benachrichtigungen“ auswählen |
| Tempo fällt nach einer Weile aus | Akku-Optimierung aus, Standort „Immer erlauben“ |
| Gang wird falsch erkannt | Gänge in der App neu einlernen, Knauf auf festen Sitz prüfen |

## 9. Sicherheit

- **Ablenkung:** Display und App nur bedienen, wenn es die Verkehrslage erlaubt; Einstellungen am besten im Stand.
  Spiele sind während der Fahrt gesperrt.
- **Blitzerwarnung:** ab Werk aus. In Deutschland ist es dem Fahrer verboten, während der Fahrt ein Gerät zu nutzen, das vor
  Blitzern warnt (§ 23 Abs. 1c StVO) – ebenso in Österreich, der Schweiz, Frankreich und weiteren Ländern.
- **Akku und Hitze:** LiPo-Akkus können bei Hitze, Beschädigung oder falscher Polung aufblähen oder brennen. Knauf nicht in der
  prallen Sonne liegen lassen, einen aufgeblähten Akku sofort ausbauen.
- **Tempolimits** kommen aus OpenStreetMap und können fehlen oder veraltet sein. Es gelten immer die echten Schilder.

## 10. Datenschutz und Lizenzen

Kein Konto, keine Werbung, kein Tracking. Nachrichten und Musikinfos gehen nur per Bluetooth an dein Board. Die vollständige
Datenschutzerklärung steht in der App unter *Einstellungen › Datenschutz*.

Kartendaten © OpenStreetMap-Mitwirkende ([ODbL](https://www.openstreetmap.org/copyright)). App und Firmware enthalten
Open-Source-Bestandteile (u. a. ESP-IDF, NimBLE, LVGL, Montserrat, Android Jetpack); die Liste steht in der App unter
*Einstellungen › Lizenzen*. „Waveshare“ ist ein Name des Herstellers des Boards; dieses Projekt steht in keiner Verbindung zu ihm.

Kontakt: Jacob Richter · jacob.richter@gmx.net · privates, kostenloses Projekt
