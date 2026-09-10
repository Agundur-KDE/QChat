# QChat

Lokale Qt-6-Anwendung zum Ver- und Entschlüsseln kurzer Textnachrichten mit OpenPGP.
**Kein Transport, kein Server, kein Benutzerkonto, kein Nachrichtenverlauf.**

Status: funktionsfähiger experimenteller Prototyp, **nicht unabhängig sicherheitsgeprüft**.
Nicht als geprüften Schutz gegen staatliche Gerätebeschlagnahme einsetzen.

## Starten

Auf dem Entwicklungsrechner:

```sh
/home/alec/projects/QChat/build/qchat --language de
```

Sprache: `en`, `de` oder `fa`. Persisch ist ein vollständiger **Übersetzungsentwurf**,
der vor sensibler Nutzung von einem Muttersprachler geprüft werden muss. Die
Oberfläche wird bei Farsi von rechts nach links aufgebaut; Prüfcodes und
verschlüsselte Blöcke bleiben links nach rechts. Die Sprache ist oben wählbar
und wird nach einem Neustart angewendet.

## Ablauf für zwei Personen

1. Beide richten QChat ein. GnuPG fragt in einem separaten Pinentry-Fenster nach
   einer Passphrase. Keine E-Mail-Adresse, kein Klarname nötig. Die Passphrase
   schützt die private Identität; QChat selbst erhält sie nicht.
2. Beide kopieren **Meine Kontaktkarte** und schicken sie über ihren vorhandenen
   Kanal. Kontaktkarten sind öffentlich, können aber Kontakte zuordnen lassen.
3. Unter **Kontakt hinzufügen** einen lokalen Spitznamen vergeben und die Karte
   einfügen. Eine Kontaktkarte enthält ausschließlich den öffentlichen Schlüssel.
4. Unter **Kontakt prüfen** den vollständigen Prüfcode unabhängig vergleichen,
   z. B. über eine bereits authentisch bekannte HTTPS-Website. Beide Seiten
   müssen ihre jeweilige Zuordnung prüfen. Der gleiche ungesicherte Chat allein
   ist kein unabhängiger Prüfweg. Ein Häkchen ersetzt die tatsächliche Prüfung nicht.
5. Kontakt wählen, Text schreiben, **Nachricht verschlüsseln**, verschlüsselten
   Block kopieren und im bisherigen Kanal versenden. QChat signiert automatisch.
   Auch die eigene Identität wird als Empfänger eingetragen, um die gesendete
   verschlüsselte Kopie später lesen zu können.
6. Zum Lesen den gesamten Block auf **Lesen** einfügen und **Nachricht
   entschlüsseln** wählen. Entschlüsselter Inhalt wird angezeigt; eine gültige
   Signatur einer geprüften Kontaktkarte wird zusätzlich als authentifiziert
   markiert. Der gewählte Kontakt im Schreibbereich bestimmt **nicht** den
   erkannten Absender.

QChat verlangt GnuPG-Passphrasen gegebenenfalls mehrmals: beim Öffnen der App,
Signieren und Entschlüsseln. Der Agent darf nur sehr kurz zwischenspeichern.
Die Kontakte werden lokal als OpenPGP-Nachricht verschlüsselt gespeichert und
erst nach dem Entsperren geladen. Klartexte werden von der App nicht auf
Datenträger geschrieben. Editor/Qt/Betriebssystem können dennoch Speicherkopien
halten. Die Zwischenablage kann externe Verlaufsmanager haben.

## Sperre

- **Esc / Jetzt sperren** leert die Anzeigen und beendet bestmöglich ausschließlich
  den GnuPG-Agenten des eigenen QChat-Verzeichnisses.
- Eine feste Sitzung endet nach **3 Minuten**, auch bei Mausbewegungen oder Tippen.
  Ungesendete Texte gehen dabei verloren. Der Timer ist kein Inaktivitätstimer.
- Ergebnisse laufender Vorgänge werden nach einer Sperre nicht mehr angezeigt.
- Unter Linux lösen neue USB-Geräte bei verfügbarer udev-Überwachung eine Sperre
  aus. Das ist nachträgliche Erkennung, **keine vorbeugende Gerätesperre**. Docks
  und Neuverbindungen können ebenfalls sperren. Die Ereigniserkennung kann in
  Sandboxes fehlen und schützt nicht gegen bereits vorhandene Eingabegeräte.
- Übliche GNOME/MATE/freedesktop-Sperrsignale und logind-Ruhezustandsmeldungen
  werden ebenfalls ausgewertet, soweit der Desktop sie anbietet.
- Es gibt **keinen Löschknopf und keine Zusicherung forensischer Spurenfreiheit**.
  Für eine vorbeugende USB-Gerätepolitik wäre separat USBGuard erforderlich.

## Bauen auf Ubuntu / Trisquel

Benötigt Qt >= 6.2, CMake >= 3.21, GPGME, GnuPG 2.2+, grafisches Pinentry,
libudev sowie einen C++17-Compiler. Bei Distributionen mit diesen Paketen:

```sh
sudo apt install build-essential cmake pkg-config qt6-base-dev qt6-tools-dev \
  qt6-tools-dev-tools libgpgme-dev libudev-dev gnupg pinentry-qt
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure
./build/qchat
```

Die konkrete Ubuntu-/Trisquel-Version wurde hier noch **nicht** getestet.
Getestet wurde nativ mit Qt 6.11.2, GPGME 2.2.0 und GnuPG 2.5.22 auf openSUSE.
Die hier erzeugte Binärdatei ist kein distributionsübergreifendes Paket.
Ein Flatpak ist noch nicht erstellt: GnuPG/Pinentry, eigener Agent und die
optionalen Systemschnittstellen müssen dort eigens integriert und getestet werden.

## Daten und Grenzen

QChat nutzt `QStandardPaths::AppLocalDataLocation`, unter üblichen Linux-Einstellungen
`~/.local/share/QChat/QChat/`. Es verwendet **nicht** das normale `~/.gnupg`.

- `gnupg/`: öffentliche Schlüssel, passphrasengeschützte private Schlüssel,
  GnuPG-Metadaten und lokale Konfiguration.
- `contacts.json`: lokal verschlüsselte Spitznamen, öffentliche Fingerabdrücke
  und Prüfstatus. Dateiname, Existenz und Größe bleiben als Metadaten sichtbar.
- `preferences.ini`: Sprache; `qchat.lock`: Sperrdatei gegen parallele Instanzen.

Verzeichnisrechte sind auf den eigenen Benutzer beschränkt. Dies ersetzt keine
Datenträgerverschlüsselung. Siehe [SECURITY.md](SECURITY.md) für das Bedrohungsmodell.

Kein automatisches Backup, kein Export privater Schlüssel, keine Schlüsselrotation
oder Wiederherstellung in der Oberfläche. Verlust von Identität/Passphrase kann
alte Nachrichten unlesbar machen. Erzeugte Identitäten laufen nach einem Jahr ab.
Ein neuer Schlüssel wird als neue, ungeprüfte Kontaktkarte behandelt.

## Entwicklung und Tests

Die Tests benutzen ausschließlich temporäre Wegwerf-Identitäten. Deren leere
Passphrasen gehören zur Testumgebung und werden nicht zur normalen Einrichtung
verwendet. GnuPG braucht lokale Agent-Sockets; stark eingeschränkte Sandboxes
können die Tests verhindern.

```sh
QT_QPA_PLATFORM=offscreen QCHAT_TEST_ARTIFACTS="$PWD/artifacts" ./build/window_test
./build/qchat --smoke-test --language fa
lupdate6 src -ts i18n/qchat_de.ts i18n/qchat_fa.ts
```

Auf manchen Distributionen heißt das Übersetzungswerkzeug `lupdate` oder liegt
unter `/usr/lib/qt6/bin/`. Die Tests decken kryptografischen Austausch, falsche
Empfänger, beschädigte Nachrichten, fehlende/unbekannte Signaturen, verweigerte
Privatschlüsselimporte, Kontaktprüfung, Sperren während laufender Vorgänge und
Deutsch/Farsi-Darstellung ab. Sie ersetzen kein Sicherheitsaudit.
