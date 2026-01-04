#include <dirent.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "musicdb_format.h"

// --- Raw Track List (Scanning Phase) ---

typedef struct {
  char path[1024];
  char artist[MAX_STRING_LEN];
  char album[MAX_STRING_LEN];
  char title[MAX_STRING_LEN];
  int track_number;
  int disc_number;
  int year;
  int duration;
} TrackInfo;

typedef struct {
  TrackInfo *tracks;
  size_t count;
  size_t capacity;
} TrackList;

void list_init(TrackList *list) {
  list->count = 0;
  list->capacity = 10;
  list->tracks = malloc(list->capacity * sizeof(TrackInfo));
}

void list_add(TrackList *list, TrackInfo track) {
  if (list->count >= list->capacity) {
    list->capacity *= 2;
    list->tracks = realloc(list->tracks, list->capacity * sizeof(TrackInfo));
  }
  list->tracks[list->count++] = track;
}

void list_free(TrackList *list) { free(list->tracks); }

// --- Metadata Extraction ---

void extract_metadata(const char *path, TrackList *list) {
  AVFormatContext *fmt_ctx = NULL;
  // quiet logging
  av_log_set_level(AV_LOG_ERROR);

  if (avformat_open_input(&fmt_ctx, path, NULL, NULL) != 0) {
    fprintf(stderr, "Could not open file: %s\n", path);
    return;
  }

  if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
    fprintf(stderr, "Could not find stream info: %s\n", path);
    avformat_close_input(&fmt_ctx);
    return;
  }

  AVDictionary *tags = fmt_ctx->metadata;
  AVDictionaryEntry *tag = NULL;

  TrackInfo t = {0};
  strncpy(t.path, path, sizeof(t.path) - 1);
  t.duration = fmt_ctx->duration / AV_TIME_BASE;

// Helper to get tag safely
#define GET_TAG(key, dest)                                                     \
  tag = av_dict_get(tags, key, NULL, AV_DICT_IGNORE_SUFFIX);                   \
  if (tag)                                                                     \
    strncpy(dest, tag->value, sizeof(dest) - 1);

  GET_TAG("artist", t.artist);
  GET_TAG("album", t.album);
  GET_TAG("title", t.title);

  // Default if missing
  if (strlen(t.artist) == 0)
    strcpy(t.artist, "Unknown Artist");
  if (strlen(t.album) == 0)
    strcpy(t.album, "Unknown Album");
  if (strlen(t.title) == 0) {
    // Use filename as title if missing
    const char *slash = strrchr(path, '/');
    const char *dot = strrchr(path, '.');
    if (slash) {
      size_t len =
          (dot && dot > slash) ? (size_t)(dot - slash - 1) : strlen(slash + 1);
      if (len >= sizeof(t.title))
        len = sizeof(t.title) - 1;
      strncpy(t.title, slash + 1, len);
    }
  }

  tag = av_dict_get(tags, "track", NULL, AV_DICT_IGNORE_SUFFIX);
  if (tag) {
    t.track_number = atoi(tag->value);
  } else {
    // Try TRCK
    tag = av_dict_get(tags, "TRCK", NULL, AV_DICT_IGNORE_SUFFIX);
    if (tag)
      t.track_number = atoi(tag->value);
  }

  tag = av_dict_get(tags, "date", NULL, AV_DICT_IGNORE_SUFFIX);
  if (tag) {
    t.year = atoi(tag->value);
  }

  list_add(list, t);
  avformat_close_input(&fmt_ctx);
}

// --- Directory Scanning ---

int is_audio_file(const char *filename) {
  const char *ext = strrchr(filename, '.');
  if (!ext)
    return 0;
  if (strcasecmp(ext, ".mp3") == 0)
    return 1;
  if (strcasecmp(ext, ".m4a") == 0)
    return 1;
  if (strcasecmp(ext, ".aac") == 0)
    return 1;
  if (strcasecmp(ext, ".wav") == 0)
    return 1;
  if (strcasecmp(ext, ".flac") == 0)
    return 1;
  return 0;
}

void scan_directory(const char *base_path, TrackList *list) {
  DIR *dir;
  struct dirent *entry;
  char path[1024];

  if (!(dir = opendir(base_path)))
    return;

  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;

    snprintf(path, sizeof(path), "%s/%s", base_path, entry->d_name);

    struct stat statbuf;
    if (stat(path, &statbuf) == -1)
      continue;

    if (S_ISDIR(statbuf.st_mode)) {
      scan_directory(path, list);
    } else if (S_ISREG(statbuf.st_mode) && is_audio_file(entry->d_name)) {
      extract_metadata(path, list);
    }
  }
  closedir(dir);
}

// --- Hierarchy & Generation ---

typedef struct {
  char path[1024];
  int track_number; // disc << 8 | track
  int duration;
  char title[MAX_STRING_LEN];
} TempTrack;

typedef struct {
  char title[MAX_STRING_LEN];
  int year;
  TempTrack *tracks;
  size_t track_count;
  size_t track_capacity;
} TempAlbum;

typedef struct {
  char name[MAX_STRING_LEN];
  char sort_name[MAX_STRING_LEN];
  TempAlbum *albums;
  size_t album_count;
  size_t album_capacity;
} TempArtist;

typedef struct {
  TempArtist *artists;
  size_t count;
  size_t capacity;
} TempDB;

// String Pool
char *string_pool = NULL;
size_t string_pool_size = 0;
size_t string_pool_capacity = 0;

uint32_t add_to_pool(const char *str) {
  size_t len = strlen(str) + 1;
  if (string_pool_size + len > string_pool_capacity) {
    string_pool_capacity =
        (string_pool_capacity == 0) ? 1024 * 1024 : string_pool_capacity * 2;
    string_pool = realloc(string_pool, string_pool_capacity);
  }
  uint32_t offset = (uint32_t)string_pool_size;
  memcpy(string_pool + offset, str, len);
  string_pool_size += len;
  return offset;
}

void get_sort_name(const char *name, char *out) {
  if (strncasecmp(name, "The ", 4) == 0) {
    strcpy(out, name + 4);
  } else {
    strcpy(out, name);
  }
}

TempArtist *find_or_add_artist(TempDB *db, const char *name) {
  for (size_t i = 0; i < db->count; i++) {
    if (strcmp(db->artists[i].name, name) == 0) {
      return &db->artists[i];
    }
  }
  if (db->count >= db->capacity) {
    db->capacity = (db->capacity == 0) ? 10 : db->capacity * 2;
    db->artists = realloc(db->artists, db->capacity * sizeof(TempArtist));
  }
  TempArtist *a = &db->artists[db->count++];
  memset(a, 0, sizeof(TempArtist));
  strncpy(a->name, name, MAX_STRING_LEN - 1);
  get_sort_name(name, a->sort_name);
  return a;
}

TempAlbum *find_or_add_album(TempArtist *artist, const char *title, int year) {
  for (size_t i = 0; i < artist->album_count; i++) {
    if (strcmp(artist->albums[i].title, title) == 0) {
      return &artist->albums[i];
    }
  }
  if (artist->album_count >= artist->album_capacity) {
    artist->album_capacity =
        (artist->album_capacity == 0) ? 4 : artist->album_capacity * 2;
    artist->albums =
        realloc(artist->albums, artist->album_capacity * sizeof(TempAlbum));
  }
  TempAlbum *alb = &artist->albums[artist->album_count++];
  memset(alb, 0, sizeof(TempAlbum));
  strncpy(alb->title, title, MAX_STRING_LEN - 1);
  alb->year = year;
  return alb;
}

void add_track_to_hierarchy(TempDB *db, TrackInfo *t) {
  TempArtist *artist = find_or_add_artist(db, t->artist);
  TempAlbum *album = find_or_add_album(artist, t->album, t->year);

  if (album->track_count >= album->track_capacity) {
    album->track_capacity =
        (album->track_capacity == 0) ? 8 : album->track_capacity * 2;
    album->tracks =
        realloc(album->tracks, album->track_capacity * sizeof(TempTrack));
  }
  TempTrack *tr = &album->tracks[album->track_count++];
  strncpy(tr->path, t->path, sizeof(tr->path) - 1);
  strncpy(tr->title, t->title, MAX_STRING_LEN - 1);
  tr->track_number = (t->disc_number << 8) | t->track_number;
  tr->duration = t->duration;
}

int cmp_artists(const void *a, const void *b) {
  const TempArtist *ta = (const TempArtist *)a;
  const TempArtist *tb = (const TempArtist *)b;
  return strcasecmp(ta->sort_name, tb->sort_name);
}

int cmp_albums_year(const void *a, const void *b) {
  const TempAlbum *ta = (const TempAlbum *)a;
  const TempAlbum *tb = (const TempAlbum *)b;
  if (ta->year != tb->year)
    return ta->year - tb->year;
  return strcasecmp(ta->title, tb->title);
}

int cmp_tracks(const void *a, const void *b) {
  const TempTrack *ta = (const TempTrack *)a;
  const TempTrack *tb = (const TempTrack *)b;
  return ta->track_number - tb->track_number;
}

typedef struct {
  char title[MAX_STRING_LEN];
  uint16_t original_index;
} AlbumSortEntry;

int cmp_album_sort_entry(const void *a, const void *b) {
  const AlbumSortEntry *ta = (const AlbumSortEntry *)a;
  const AlbumSortEntry *tb = (const AlbumSortEntry *)b;
  char sort_a[MAX_STRING_LEN], sort_b[MAX_STRING_LEN];
  get_sort_name(ta->title, sort_a);
  get_sort_name(tb->title, sort_b);
  return strcasecmp(sort_a, sort_b);
}

int get_letter_index(const char *str) {
  char name[MAX_STRING_LEN];
  get_sort_name(str, name);
  char c = name[0];
  if (c >= 'a' && c <= 'z')
    c -= 32;
  if (c >= 'A' && c <= 'Z')
    return c - 'A' + 1;
  return 0; // Misc
}

void generate_db(TrackList *raw_list, const char *out_file) {
  TempDB db = {0};

  // 1. Build Hierarchy
  for (size_t i = 0; i < raw_list->count; i++) {
    add_track_to_hierarchy(&db, &raw_list->tracks[i]);
  }

  // 2. Sort
  qsort(db.artists, db.count, sizeof(TempArtist), cmp_artists);
  for (size_t i = 0; i < db.count; i++) {
    TempArtist *artist = &db.artists[i];
    qsort(artist->albums, artist->album_count, sizeof(TempAlbum),
          cmp_albums_year);
    for (size_t j = 0; j < artist->album_count; j++) {
      qsort(artist->albums[j].tracks, artist->albums[j].track_count,
            sizeof(TempTrack), cmp_tracks);
    }
  }

  // 3. Serialize
  FILE *f = fopen(out_file, "wb");
  if (!f) {
    perror("fopen");
    return;
  }

  uint32_t total_artists = (uint32_t)db.count;
  uint32_t total_albums = 0;
  uint32_t total_tracks = 0;

  for (size_t i = 0; i < db.count; i++) {
    total_albums += db.artists[i].album_count;
    for (size_t j = 0; j < db.artists[i].album_count; j++) {
      total_tracks += db.artists[i].albums[j].track_count;
    }
  }

  // Header
  DbHeader header;
  memcpy(header.magic, MDB_MAGIC, 4);
  header.artist_count = total_artists;
  header.album_count = total_albums;
  header.track_count = total_tracks;
  header.string_pool_size = 0;
  fwrite(&header, sizeof(header), 1, f);

  long artist_index_pos = ftell(f);
  fseek(f, sizeof(LetterEntry) * LETTER_INDEX_SIZE, SEEK_CUR);

  long album_index_pos = ftell(f);
  fseek(f, sizeof(LetterEntry) * LETTER_INDEX_SIZE, SEEK_CUR);

  // Letter Indices Tracking
  LetterEntry artist_letters[LETTER_INDEX_SIZE] = {0};

  // Write Artists
  uint16_t current_album_idx = 0;
  for (size_t i = 0; i < db.count; i++) {
    TempArtist *ta = &db.artists[i];

    int let = get_letter_index(ta->sort_name);
    if (artist_letters[let].count == 0) {
      artist_letters[let].start_index = (uint16_t)i;
    }
    artist_letters[let].count++;

    DbArtist da;
    strncpy(da.name, ta->name, MAX_STRING_LEN);
    da.album_start = current_album_idx;
    da.album_count = (uint16_t)ta->album_count;
    fwrite(&da, sizeof(DbArtist), 1, f);
    current_album_idx += ta->album_count;
  }

  // Write Albums & Build Sort List
  AlbumSortEntry *album_sort_list =
      malloc(total_albums * sizeof(AlbumSortEntry));
  uint32_t album_written_count = 0;
  uint16_t current_track_idx = 0;

  for (size_t i = 0; i < db.count; i++) {
    TempArtist *ta = &db.artists[i];
    for (size_t j = 0; j < ta->album_count; j++) {
      TempAlbum *talb = &ta->albums[j];

      DbAlbum dalb;
      strncpy(dalb.title, talb->title, MAX_STRING_LEN);
      dalb.track_start = current_track_idx;
      dalb.track_count = (uint16_t)talb->track_count;
      dalb.year = (uint16_t)talb->year;
      dalb.artist_index = (uint16_t)i;
      fwrite(&dalb, sizeof(DbAlbum), 1, f);

      strncpy(album_sort_list[album_written_count].title, talb->title,
              MAX_STRING_LEN);
      album_sort_list[album_written_count].original_index =
          (uint16_t)album_written_count;
      album_written_count++;

      current_track_idx += talb->track_count;
    }
  }

  // Album Title Index
  qsort(album_sort_list, total_albums, sizeof(AlbumSortEntry),
        cmp_album_sort_entry);

  LetterEntry album_letters[LETTER_INDEX_SIZE] = {0};
  uint16_t *album_title_index = malloc(total_albums * sizeof(uint16_t));

  for (size_t i = 0; i < total_albums; i++) {
    album_title_index[i] = album_sort_list[i].original_index;

    char sort_t[MAX_STRING_LEN];
    get_sort_name(album_sort_list[i].title, sort_t);
    int let = get_letter_index(sort_t);
    if (album_letters[let].count == 0) {
      album_letters[let].start_index = (uint16_t)i;
    }
    album_letters[let].count++;
  }

  fwrite(album_title_index, sizeof(uint16_t), total_albums, f);
  free(album_title_index);
  free(album_sort_list);

  // Write Tracks
  for (size_t i = 0; i < db.count; i++) {
    TempArtist *ta = &db.artists[i];
    for (size_t j = 0; j < ta->album_count; j++) {
      TempAlbum *talb = &ta->albums[j];
      for (size_t k = 0; k < talb->track_count; k++) {
        TempTrack *ttr = &talb->tracks[k];

        DbTrack dtr;
        strncpy(dtr.title, ttr->title, MAX_STRING_LEN);
        dtr.track_number = (uint16_t)ttr->track_number;
        dtr.duration_sec = (uint16_t)ttr->duration;
        dtr.filename_offset = add_to_pool(ttr->path);

        fwrite(&dtr, sizeof(DbTrack), 1, f);
      }
    }
  }

  // Write String Pool
  if (string_pool)
    fwrite(string_pool, 1, string_pool_size, f);

  // Finalize
  long end_pos = ftell(f);

  fseek(f, 0, SEEK_SET);
  header.string_pool_size = (uint32_t)string_pool_size;
  fwrite(&header, sizeof(header), 1, f);

  fseek(f, artist_index_pos, SEEK_SET);
  fwrite(artist_letters, sizeof(LetterEntry), LETTER_INDEX_SIZE, f);

  fseek(f, album_index_pos, SEEK_SET);
  fwrite(album_letters, sizeof(LetterEntry), LETTER_INDEX_SIZE, f);

  fseek(f, end_pos, SEEK_SET);
  fclose(f);

  printf("DB Generated: %s\n", out_file);
  printf("Stats: %d Artists, %d Albums, %d Tracks, %zu Pool Bytes\n",
         total_artists, total_albums, total_tracks, string_pool_size);
}

int main(int argc, char **argv) {
  if (argc < 3) {
    printf("Usage: %s <directory> <output_db>\n", argv[0]);
    return 1;
  }

  TrackList list;
  list_init(&list);

  printf("Scanning %s...\n", argv[1]);
  scan_directory(argv[1], &list);
  printf("Scanned %zu tracks. Generating DB...\n", list.count);

  generate_db(&list, argv[2]);

  list_free(&list);
  return 0;
}
