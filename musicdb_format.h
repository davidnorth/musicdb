#ifndef MUSICDB_FORMAT_H
#define MUSICDB_FORMAT_H

#include <stdint.h>

#define MAX_STRING_LEN 32
#define MDB_MAGIC "MDB1"

typedef struct {
    char magic[4]; // "MDB1"
    uint32_t artist_count;
    uint32_t album_count;
    uint32_t track_count;
    uint32_t string_pool_size;
} DbHeader;

// Letter Index Entry
typedef struct {
    uint16_t start_index;
    uint16_t count;
} LetterEntry;

// Letter Index: 27 entries (0=Misc, 1='A' ... 26='Z')
#define LETTER_INDEX_SIZE 27

// Artist Entry
typedef struct {
    char name[MAX_STRING_LEN];
    uint16_t album_start; // Index into the primary Album table (Sorted by Artist)
    uint16_t album_count;
} DbArtist;

// Album Entry
typedef struct {
    char title[MAX_STRING_LEN];
    uint16_t track_start; // Index into Track table
    uint16_t track_count;
    uint16_t year;
    uint16_t artist_index; // Back reference to Artist
} DbAlbum;

// Track Entry
typedef struct {
    char title[MAX_STRING_LEN];
    uint32_t filename_offset; // Offset into String Pool
    uint16_t track_number; // (disc << 8) | track
    uint16_t duration_sec;
} DbTrack;

#endif // MUSICDB_FORMAT_H
