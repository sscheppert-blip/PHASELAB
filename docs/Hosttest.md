# PhaseLab – Hosttest P1 (Link zwischen Channel und Bus Analyzer)

Diese Tests klären, was nur ein echter Host beantworten kann: ob alle Spuren dieselbe Timeline-Position
bekommen, wie die Delay-Kompensation (PDC) wirkt und was beim Export, Freeze und in getrennten
Plugin-Prozessen passiert. Die eigenen Tests decken die Technik dahinter schon ab (siehe unten); hier geht
es um das Verhalten der DAWs.

Bitte pro DAW Version, Betriebssystem, Format (VST3/CLAP/AU) und Samplerate notieren.

## Aufbau

1. Plugins installieren (VST3 nach `Common Files\VST3`, CLAP nach `%LOCALAPPDATA%\Programs\Common\CLAP`).
2. Zwei Audiospuren mit **demselben** Clip anlegen (z. B. ein Drumloop oder Rauschen), beide auf denselben
   Bus oder den Master.
3. Spur 1: **PhaseLab Channel Analyzer**, Betriebsart *Analyse + Senden*, *Slot A*, *Gruppe 1*.
4. Spur 2: **PhaseLab Channel Analyzer**, *Analyse + Senden*, *Slot B*, *Gruppe 1*.
5. Bus oder Master: **PhaseLab Bus Analyzer**, *Gruppe 1*.
6. Wiedergabe starten.

## Tests

| # | Schritt | Erwartung |
|---|---|---|
| 1 | Aufbau wie oben, abspielen | „SYNC · Vergleich gültig“, Korrelation +1,00, Versatz 0 Samples, A+B 100 |
| 2 | Spur 2 um 1 ms verzögern (Track-Delay bzw. Zeitversatz der Spur, sonst den Clip minimal verschieben) | Versatz +48 Samples bei 48 kHz (+1,00 ms), Hinweis „Spur B … vorziehen“ |
| 3 | Dasselbe mit −1 ms | Versatz −48 Samples, Hinweis „… später setzen“ |
| 4 | Spur 2: Polarität umkehren (z. B. Utility/Phase-Invert vor dem Channel Analyzer) | Korrelation −1,00, Polaritätshinweis, A+B 0 |
| 5 | Auf Spur 2 ein Plugin mit Latenz **vor** den Channel Analyzer (Linear-Phase-EQ, Limiter mit Lookahead) | Versatz 0 → Host kompensiert richtig. Jeder andere Wert ist ein wichtiges Ergebnis – bitte genau notieren |
| 6 | Dasselbe Plugin **hinter** den Channel Analyzer | Versatz 0 |
| 7 | Low-Latency-/Monitoring-Modus der DAW ein, falls vorhanden | Ergebnis notieren |
| 8 | Stopp | „Wiedergabe starten“ bzw. „Quelle liefert keine Daten“, keine alten Werte als gültig |
| 9 | Springen in der Timeline, Loop an | Kurz „Lücke“/„Synchronisiere“, dann wieder gültig |
| 10 | Spur 1 löschen oder Channel Analyzer auf *Analyse* | „Quelle fehlt“ |
| 11 | Zweiter Channel Analyzer auch auf Slot A | Dieser zeigt „Slot A belegt“, nichts wird vermischt |
| 12 | Projekt speichern, schließen, neu laden | Verbindung kommt von selbst wieder, MONO ist aus |
| 13 | Export/Render (schneller als Echtzeit), danach Nulltest gegen ein Render ohne PhaseLab | Export läuft durch, Audio identisch; der Bus Analyzer sperrt den A/B-Vergleich währenddessen („Offline-Rendern“) |
| 14 | Freeze/Bounce einer Spur mit Channel Analyzer | Ergebnis notieren |
| 15 | Bitwig: Plugin-Hosting „einzeln“/„gemeinsam“; FL Studio: Plugin „bridged“ | Verbindung auch über getrennte Plugin-Prozesse |
| 16 | *Nur Senden* auf beiden Spuren | Vergleich weiter gültig, CPU-Anzeige der DAW notieren |

## Was schon ohne DAW geprüft ist

`PhaseLabTests` (44 Tests) prüft:
- Exakte Zuordnung bei 0, +1, −1 und 20 Samples Versatz, mit unterschiedlichen Blockgrößen und vertauschter Aufrufreihenfolge.
- Polarität und Summen-Score.
- Seek, Loop, gestoppten Transport, fehlende Quelle, Gruppen-Trennung und Slot-Konflikt.
- Überlauf, Offline-Sperre und verschiedene Sampleraten.
- Einen Sender in einem **zweiten Prozess**. Dabei wird der Versatz von 7 Samples exakt gefunden.

Offen ist nur, wie die DAWs Timeline und PDC tatsächlich liefern.
