# ADMAX
Advanced Document Management And Xtras

#### Hier entsteht:
ein Dokumenten-Verwaltungs-System mit folgender Zielsetzung 

- Archivierung von Dokumenten wie PDFs oder Bildern
- Verschlagwortung der Dokumente
- Versionierung der Dokumente
- Bearbeitung von PDF-Formularen
- Workflow Management
- Berechtigungskonzept  
- Client-Server-Technologie
- Standort-verteilte Datenspeicherung
- Verschlüsselte Datenübertragung

## Prototyp

Das Projekt besteht aus einem Server sowie einen grafischen Client und einen 
Client auf Kommandozeilenebene.

Im grafischen Client können Dokumente sowie Bilder aus Dateien geladen, verschlagwortet 
und auf 
dem Server hochgeladen werden. Es kann über Schlagworte gesucht werden und die 
Dokumente können angezeigt werden.

Der Kommandozeilen-Client kann alle Datensätze entladen und wieder restaurieren.

## Technik

### Komunikation
Die Kommunikation erfolgt über einen TCP-Port. Über diesen werden XML-Strukturen
sowie Binärdaten transportiert. Die Verschlüsselung des Transports erfolgt innerhalb
der XML-Struktur über XML-Encryption durch eine AES-256 Verschlüsselung. alle Binärdaten
werden ebenso verschlüsselt. Die benötigten Zertifikate zum Erzeugen des Session-Keys
werden mittels RSA verschlüsselt.

Eine Kommunikation kann nur aufgebaut werden, wenn beiden Parteien die jeweigen 
Public-Keys der Gegenseite vorliegen.

In der aktuellen Version muss der Schlüsselaustausch manuell bzw. über ein 
gemeinsames Verzeichnis erfolgen.

### Datenablage

#### Filesystem
Die Ablage der Dokumente erfolgt als Datei in einem Verzeichnis, die Metadaten liegen in einer SQLite DB.

#### Datenbank
Eine Speicherung von Dokumenten und Metadaten erfolgt in einer MongoDB.