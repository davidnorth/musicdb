#include "musicdb.c" // Hack: include C file directly for simple single-file build or link it. We'll link it.
#include <stdio.h>

// Re-declare to avoid include issues if linking but "musicdb.c" is just
// included above for simplicity in one go? Actually let's NOT include .c, let's
// include .h and compile together. But I haven't added prototypes to .h yet for
// the library functions, only structs.

// Let's add prototypes to a new header or append to musicdb_format.h?
// Keeping format pure structs is good. Let's make "musicdb_client.h" or just
// put prototypes here for now.

int mdb_init(MDB_Context *ctx, const char *filename);
int mdb_get_artist(MDB_Context *ctx, int index, DbArtist *out_artist);
int mdb_get_album(MDB_Context *ctx, int index, DbAlbum *out_album);
int mdb_get_track(MDB_Context *ctx, int index, DbTrack *out_track);
int mdb_read_string(MDB_Context *ctx, uint32_t offset, char *buf,
                    size_t max_len);

int main(int argc, char **argv) {
  if (argc < 2) {
    printf("Usage: %s <db_file>\n", argv[0]);
    return 1;
  }

  MDB_Context ctx;
  if (mdb_init(&ctx, argv[1]) != 0) {
    printf("Failed to init DB\n");
    return 1;
  }

  printf("DB Loaded. Artists: %d, Albums: %d, Tracks: %d\n",
         ctx.header.artist_count, ctx.header.album_count,
         ctx.header.track_count);

  // List all Artists
  printf("\n--- Artists ---\n");
  for (uint32_t i = 0; i < ctx.header.artist_count; i++) {
    DbArtist a;
    mdb_get_artist(&ctx, i, &a);
    printf("[%d] %s (%d albums)\n", i, a.name, a.album_count);

    // List Albums for this Artist
    for (int j = 0; j < a.album_count; j++) {
      DbAlbum alb;
      mdb_get_album(&ctx, a.album_start + j, &alb);
      printf("  - %s (%d) [%d tracks]\n", alb.title, alb.year, alb.track_count);

      // List Tracks
      for (int k = 0; k < alb.track_count; k++) {
        DbTrack tr;
        mdb_get_track(&ctx, alb.track_start + k, &tr);
        char path[1024];
        mdb_read_string(&ctx, tr.filename_offset, path, sizeof(path));
        printf("    %d. %s [%ds] (%s)\n", tr.track_number & 0xFF, tr.title,
               tr.duration_sec, path);
      }
    }
  }

  // Check Letter Index 'R' (Rolling Stones)
  // 'R' is index ('R'-'A'+1) = 18.
  int letter_idx = 'R' - 'A' + 1;
  LetterEntry le = ctx.artist_letters[letter_idx];
  printf("\n--- Letter Index 'R' ---\n");
  printf("Start: %d, Count: %d\n", le.start_index, le.count);

  mdb_close(&ctx);
  return 0;
}
