#include "musicdb_format.h"
#include <stdio.h>
#include <string.h>

// Platform Abstraction (default to stdio)
// On Arduino, you would replace these with SD library calls.

typedef struct {
  FILE *file;
  DbHeader header;
  LetterEntry artist_letters[LETTER_INDEX_SIZE];
  LetterEntry album_letters[LETTER_INDEX_SIZE];
} MDB_Context;

int mdb_init(MDB_Context *ctx, const char *filename) {
  if (!ctx)
    return -1;
  ctx->file = fopen(filename, "rb");
  if (!ctx->file)
    return -2;

  if (fread(&ctx->header, sizeof(DbHeader), 1, ctx->file) != 1) {
    fclose(ctx->file);
    return -3;
  }

  if (strncmp(ctx->header.magic, MDB_MAGIC, 4) != 0) {
    fclose(ctx->file);
    return -4;
  }

  if (fread(ctx->artist_letters, sizeof(LetterEntry), LETTER_INDEX_SIZE,
            ctx->file) != LETTER_INDEX_SIZE) {
    fclose(ctx->file);
    return -5;
  }

  if (fread(ctx->album_letters, sizeof(LetterEntry), LETTER_INDEX_SIZE,
            ctx->file) != LETTER_INDEX_SIZE) {
    fclose(ctx->file);
    return -6;
  }

  return 0; // Success
}

void mdb_close(MDB_Context *ctx) {
  if (ctx && ctx->file) {
    fclose(ctx->file);
    ctx->file = NULL;
  }
}

// Helpers
int mdb_read_at(MDB_Context *ctx, long offset, void *buf, size_t size) {
  if (fseek(ctx->file, offset, SEEK_SET) != 0)
    return -1;
  if (fread(buf, 1, size, ctx->file) != size)
    return -2;
  return 0;
}

// Data Access
int mdb_get_artist(MDB_Context *ctx, int index, DbArtist *out_artist) {
  if (index < 0 || (uint32_t)index >= ctx->header.artist_count)
    return -1;

  // Header + LetterIndex + LetterIndex
  long offset = sizeof(DbHeader) +
                (sizeof(LetterEntry) * LETTER_INDEX_SIZE * 2) +
                (index * sizeof(DbArtist));

  return mdb_read_at(ctx, offset, out_artist, sizeof(DbArtist));
}

int mdb_get_album(MDB_Context *ctx, int index, DbAlbum *out_album) {
  if (index < 0 || (uint32_t)index >= ctx->header.album_count)
    return -1;

  // Offset calculation:
  // Artists start after letter indices.
  long artists_size = ctx->header.artist_count * sizeof(DbArtist);
  long offset = sizeof(DbHeader) +
                (sizeof(LetterEntry) * LETTER_INDEX_SIZE * 2) + artists_size;

  // Scan to album index
  offset += (index * sizeof(DbAlbum));

  return mdb_read_at(ctx, offset, out_album, sizeof(DbAlbum));
}

int mdb_get_track(MDB_Context *ctx, int index, DbTrack *out_track) {
  if (index < 0 || (uint32_t)index >= ctx->header.track_count)
    return -1;

  long artists_size = ctx->header.artist_count * sizeof(DbArtist);
  long albums_size = ctx->header.album_count * sizeof(DbAlbum);
  long title_index_size = ctx->header.album_count * sizeof(uint16_t);

  long offset = sizeof(DbHeader) +
                (sizeof(LetterEntry) * LETTER_INDEX_SIZE * 2) + artists_size +
                albums_size + title_index_size;

  offset += (index * sizeof(DbTrack));
  return mdb_read_at(ctx, offset, out_track, sizeof(DbTrack));
}

int mdb_get_album_by_title_index(MDB_Context *ctx, int sort_index,
                                 DbAlbum *out_album) {
  if (sort_index < 0 || (uint32_t)sort_index >= ctx->header.album_count)
    return -1;

  long artists_size = ctx->header.artist_count * sizeof(DbArtist);
  long albums_size = ctx->header.album_count * sizeof(DbAlbum);
  long index_start = sizeof(DbHeader) +
                     (sizeof(LetterEntry) * LETTER_INDEX_SIZE * 2) +
                     artists_size + albums_size;

  uint16_t real_index;
  if (mdb_read_at(ctx, index_start + (sort_index * sizeof(uint16_t)),
                  &real_index, sizeof(uint16_t)) != 0)
    return -2;

  return mdb_get_album(ctx, real_index, out_album);
}

// String Pool
int mdb_read_string(MDB_Context *ctx, uint32_t offset, char *buf,
                    size_t max_len) {
  long pool_start = sizeof(DbHeader) +
                    (sizeof(LetterEntry) * LETTER_INDEX_SIZE * 2) +
                    (ctx->header.artist_count * sizeof(DbArtist)) +
                    (ctx->header.album_count * sizeof(DbAlbum)) +
                    (ctx->header.album_count * sizeof(uint16_t)) +
                    (ctx->header.track_count * sizeof(DbTrack));

  if (fseek(ctx->file, pool_start + offset, SEEK_SET) != 0)
    return -1;

  // Read char by char until null or max_len
  size_t i = 0;
  while (i < max_len - 1) {
    int c = fgetc(ctx->file);
    if (c == EOF)
      break;
    buf[i++] = (char)c;
    if (c == '\0')
      return 0;
  }
  buf[i] = '\0';
  return 0;
}
