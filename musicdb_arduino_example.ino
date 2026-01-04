/*
  MusicDB Arduino/ESP32 Example

  This example demonstrates how to use the generated music.db on an ESP32.
  It assumes you have the 'music.db' file on an SD card or SPIFFS.

  Dependencies:
  - musicdb.c
  - musicdb_format.h

  Note: On ESP32 with the standard SD library, `fopen` works nicely if you
  include FS.h/SD.h and initialize them. Newlib VFS wraps them.
*/

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>

// Include the library (in a real sketch, these would be in the libraries folder
// or tabs) You might need to rename musicdb.c to musicdb.h/cpp or just include
// logic if IDE complains about .c
extern "C" {
#include "musicdb.c"
}

// Global context
MDB_Context db;

void setup() {
  Serial.begin(115200);
  while (!Serial)
    delay(10);

  Serial.println("Initializing SD Card...");
  if (!SD.begin()) {
    Serial.println("SD initialization failed!");
    return;
  }
  Serial.println("SD Initialized.");

  // Open Database
  // Ensure music.db is at the root of the SD card
  int res = mdb_init(&db, "/sd/music.db");
  // Note: VFS path might be "/sd/music.db" or just "/music.db" depending on
  // mount point. Standard Arduino SD often mounts at "/".

  if (res != 0) {
    // Retry with plain path
    res = mdb_init(&db, "/music.db");
  }

  if (res != 0) {
    Serial.printf("Failed to init MusicDB: Error %d\n", res);
    return;
  }

  Serial.println("MusicDB Loaded Successfully!");
  Serial.printf("Database contains %d artists and %d albums.\n",
                db.header.artist_count, db.header.album_count);

  // Example: Lookup a specific artist index (e.g., first artist)
  if (db.header.artist_count > 0) {
    DbArtist a;
    if (mdb_get_artist(&db, 0, &a) == 0) {
      Serial.printf("First Artist: %s\n", a.name);

      // Get their first album
      if (a.album_count > 0) {
        DbAlbum alb;
        mdb_get_album(&db, a.album_start, &alb);
        Serial.printf("First Album: %s (%d)\n", alb.title, alb.year);
      }
    }
  }
}

void loop() {
  // Do nothing
  delay(1000);
}
