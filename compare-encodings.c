/*  RawUpdates2ppm -- an utility to convert ``raw'' files saved with
 *    the fbs-dump utility to separate 24-bit ppm files.
 *  $Id: compare-encodings.c,v 1.1.1.1 2008-07-15 23:08:12 dcommander Exp $
 *  Copyright (C) 2000 Const Kaplinsky <const@ce.cctpu.edu.ru>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * Important note: this ``utility'' is more a hack than a product. It
 * was written for one-time use.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <netinet/in.h>

#include "rfb.h"

#define SAVE_PATH  "./ppm"
/* #define SAVE_PPM_FILES */
/* #define LAZY_TIGHT */

/* Server messages */
#define SMSG_FBUpdate         0
#define SMSG_SetColourMap     1
#define SMSG_Bell             2
#define SMSG_ServerCutText    3

static int color_depth = 16;
static int sum_raw = 0, sum_tight = 0, sum_hextile = 0, sum_zlib = 0;


static void show_usage (char *program_name);
static int do_convert (FILE *in);
static int parse_fb_update (FILE *in);

static int parse_ht_rectangle (FILE *in, int xpos, int ypos,
                               int width, int height, int rect_no);
static int parse_raw_rectangle (FILE *in, int xpos, int ypos,
                                int width, int height, int rect_no);

static int save_rectangle (FILE *ppm, int width, int height, int depth);

static int handle_hextile8 (FILE *in, int width, int height);
static int handle_hextile16 (FILE *in, int width, int height);
static int handle_hextile32 (FILE *in, int width, int height);

static void copy_data (char *data, int rw, int rh,
                       int x, int y, int w, int h, int bpp);

static CARD32 get_CARD32 (char *ptr);
static CARD16 get_CARD16 (char *ptr);

static void do_compare (char *data, int w, int h);


static int total_updates;
static int total_rects;

int main (int argc, char *argv[])
{
  FILE *in;
  int max_argc = 2;
  char buf[12];
  int err;

  if (argc > 1) {
    if (strcmp (argv[1], "-8") == 0) {
      color_depth = 8;
      max_argc++;
    } else if (strcmp (argv[1], "-16") == 0) {
      color_depth = 16;
      max_argc++;
    } else if (strcmp (argv[1], "-24") == 0) {
      color_depth = 24;
      max_argc++;
    } else if (argv[1][0] == '-') {
      show_usage (argv[0]);
      return 1;
    }
  }

  if (argc > max_argc) {
    show_usage (argv[0]);
    return 1;
  }

  in = (argc == max_argc) ? fopen (argv[argc - 1], "r") : stdin;
  if (in == NULL) {
    perror ("Cannot open input file");
    return 1;
  }

  err = (do_convert (in) != 0);

  if (in != stdin)
    fclose (in);

  fprintf (stderr, (err) ? "Fatal error has occured.\n" : "Succeeded.\n");
  return err;
}

static void show_usage (char *program_name)
{
  fprintf (stderr,
           "Usage: %s [-8|-16|-24] [INPUT_FILE]\n"
           "\n"
           "If the INPUT_FILE name is not provided, standard input is used.\n",
           program_name);
}

static int do_convert (FILE *in)
{
  int msg_type, n, bytes;
  char buf[8];
  size_t text_len;

  InitEverything (color_depth);

#ifdef LAZY_TIGHT
  rfbTightDisableGradient = TRUE;
#endif

  total_updates = 0;
  total_rects = 0;

  printf ("upd.no -                              Bytes per rectangle:\n"
          "   rect.no   coords     size       raw |hextile| zlib | tight\n"
          "---------- -------------------- -------+-------+------+------\n");

  msg_type = getc (in);
  while (msg_type != EOF) {
    switch (msg_type) {
    case SMSG_FBUpdate:
      if (parse_fb_update (in) != 0)
        return -1;
      break;

    case SMSG_SetColourMap:
      fprintf (stderr, "> SetColourMap...\n");
      if (fread (buf + 1, 1, 5, in) != 5) {
        fprintf (stderr, "Read error.\n");
        return -1;
      }
      n = (int) get_CARD16 (buf + 4);
      while (n--) {
        if (fread (buf, 1, 6, in) != 6) {
          fprintf (stderr, "Read error.\n");
          return -1;
        }
      }
      break;

    case SMSG_Bell:
      fprintf (stderr, "> Bell...\n");
      break;

    case SMSG_ServerCutText:
      fprintf (stderr, "> ServerCutText...\n");
      if (fread (buf + 1, 1, 7, in) != 7) {
        fprintf (stderr, "Read error.\n");
        return -1;
      }
      n = (size_t) get_CARD32 (buf + 4);
      while (n--) {
        if (getc (in) == EOF) {
          fprintf (stderr, "Read error.\n");
          return -1;
        }
      }
      break;

    default:
      fprintf (stderr, "Unknown server message: 0x%X\n", msg_type);
      return -1;                /* Unknown server message */
    }
    msg_type = getc (in);
  }

  printf ("\nGrand totals:\n"
          "                                raw  | hextile |  zlib  |  tight\n"
          "                            ---------+---------+--------+--------\n"
          "Bytes in all rectangles:    %9d|%9d|%8d|%8d\n"
          "Tight/XXX bandwidth saving: %8.2f%%|%8.2f%%|%7.2f%%|\n",
          sum_raw, sum_hextile, sum_zlib, sum_tight,
          (double)(sum_raw - sum_tight) * 100 / (double) sum_raw,
          (double)(sum_hextile - sum_tight) * 100 / (double) sum_hextile,
          (double)(sum_zlib - sum_tight) * 100 / (double) sum_zlib);

  return (ferror (in)) ? -1 : 0;
}

static int parse_fb_update (FILE *in)
{
  char buf[12];
  CARD16 rect_count;
  CARD16 xpos, ypos, width, height;
  int i;
  CARD32 enc;

  if (fread (buf + 1, 1, 3, in) != 3) {
    fprintf (stderr, "Read error.\n");
    return -1;
  }

  rect_count = get_CARD16 (buf + 2);

  for (i = 0; i < rect_count; i++) {
    if (fread (buf, 1, 12, in) != 12) {
      fprintf (stderr, "Read error.\n");
      return -1;
    }
    xpos = get_CARD16 (buf);
    ypos = get_CARD16 (buf + 2);
    width = get_CARD16 (buf + 4);
    height = get_CARD16 (buf + 6);

    /* 16 bits, hextile encoding only */
    enc = get_CARD32 (buf + 8);
    if (enc == 5) {
      if (parse_ht_rectangle (in, xpos, ypos, width, height, i) != 0) {
        fprintf (stderr, "Error parsing rectangle.\n");
        return -1;
      }
    } else if (enc == 0) {
      if (parse_raw_rectangle (in, xpos, ypos, width, height, i) != 0) {
        fprintf (stderr, "Error parsing rectangle.\n");
        return -1;
      }
    } else {
      fprintf (stderr, "Wrong encoding: 0x%02lX.\n", enc);
      return -1;
    }
    total_rects++;
  }
  total_updates++;

  return 0;
}

static int parse_raw_rectangle (FILE *in, int xpos, int ypos,
                                int width, int height, int rect_no)
{
  fprintf (stderr, "! Raw encoding...\n");
  return 0;
}

static int parse_ht_rectangle (FILE *in, int xpos, int ypos,
                               int width, int height, int rect_no)
{
  char fname[80];
  FILE *ppm;
  int err;
  int pixel_bytes;

  switch (color_depth) {
  case 8:
    err = handle_hextile8 (in, width, height);
    pixel_bytes = 1;
    break;
  case 16:
    err = handle_hextile16 (in, width, height);
    pixel_bytes = 2;
    break;
  default:                      /* 24 */
    err = handle_hextile32 (in, width, height);
    pixel_bytes = 4;
  }

  if (err != 0) {
    fprintf (stderr, "Error decoding hextile rectangle.\n");
    return -1;
  }

  sprintf (fname, "%.40s/%05d-%04d.ppm", SAVE_PATH, total_updates, rect_no);

#ifdef SAVE_PPM_FILES

  ppm = fopen (fname, "w");
  if (ppm != NULL) {
    save_rectangle (ppm, width, height, color_depth);
    fclose (ppm);
  }

#endif

  rfbClient.rfbBytesSent[rfbEncodingHextile] = 0;
  rfbClient.rfbBytesSent[rfbEncodingZlib] = 0;
  rfbClient.rfbBytesSent[rfbEncodingTight] = 0;

  if (!rfbSendRectEncodingHextile(&rfbClient, 0, 0, width, height)) {
      fprintf (stderr, "Error!.\n");
      return -1;
  }

  if (!rfbSendRectEncodingZlib(&rfbClient, 0, 0, width, height)) {
      fprintf (stderr, "Error!.\n");
      return -1;
  }

  if (!rfbSendRectEncodingTight(&rfbClient, 0, 0, width, height)) {
      fprintf (stderr, "Error!.\n");
      return -1;
  }

  printf ("%05d-%04d (%4d,%3d %4d*%3d): %7d|%7d|%6d|%6d\n",
          total_updates, rect_no, xpos, ypos, width, height,
          width * height * pixel_bytes + 12,
          rfbClient.rfbBytesSent[rfbEncodingHextile],
          rfbClient.rfbBytesSent[rfbEncodingZlib],
          rfbClient.rfbBytesSent[rfbEncodingTight]);

  sum_raw += width * height * pixel_bytes + 12;
  sum_hextile += rfbClient.rfbBytesSent[rfbEncodingHextile];
  sum_tight += rfbClient.rfbBytesSent[rfbEncodingTight];
  sum_zlib += rfbClient.rfbBytesSent[rfbEncodingZlib];

  return 0;
}

static int save_rectangle (FILE *ppm, int width, int height, int depth)
{
  CARD8 *data8 = (CARD8 *) rfbScreen.pfbMemory;
  CARD16 *data16 = (CARD16 *) rfbScreen.pfbMemory;
  CARD32 *data32 = (CARD32 *) rfbScreen.pfbMemory;
  int i, r, g, b;

  fprintf (ppm, "P6\n%d %d\n255\n", width, height);

  switch (depth) {
  case 8:
    for (i = 0; i < width * height; i++) {
      r = *data8 & 0x07;
      r = (r << 5) | (r << 2) | (r >> 1);
      g = (*data8 >> 3) & 0x07;
      g = (g << 5) | (g << 2) | (g >> 1);
      b = (*data8 >> 6) & 0x03;
      b = (b << 6) | (b << 4) | (b >> 2) | b;

      putc (r, ppm); putc (g, ppm); putc (b, ppm);

      data8++;
    }
    break;
  case 16:
    for (i = 0; i < width * height; i++) {
      r = (*data16 >> 11) & 0x1F;
      r = (r << 3) | (r >> 2);
      g = (*data16 >> 5) & 0x3F;
      g = (g << 2) | (g >> 4);
      b = *data16 & 0x1F;
      b = (b << 3) | (b >> 2);

      putc (r, ppm); putc (g, ppm); putc (b, ppm);

      data16++;
    }
    break;
  default:                      /* 24 */
    for (i = 0; i < width * height; i++) {
      r = (*data32 >> 16) & 0xFF;
      g = (*data32 >> 8) & 0xFF;
      b = *data32 & 0xFF;

      putc (r, ppm); putc (g, ppm); putc (b, ppm);

      data32++;
    }
  }

  return 1;
}

/*
 * Decoding hextile rectangles.
 */

#define rfbHextileRaw			(1 << 0)
#define rfbHextileBackgroundSpecified	(1 << 1)
#define rfbHextileForegroundSpecified	(1 << 2)
#define rfbHextileAnySubrects		(1 << 3)
#define rfbHextileSubrectsColoured	(1 << 4)

#define DEFINE_HANDLE_HEXTILE(bpp)                                         \
                                                                           \
static int handle_hextile##bpp (FILE *in, int width, int height)           \
{                                                                          \
  CARD##bpp bg = 0, fg = 0;                                                \
  int x, y, w, h;                                                          \
  int sx, sy, sw, sh;                                                      \
  int jx, jy;                                                              \
  int subencoding, n_subrects, coord_pair;                                 \
  CARD##bpp data[16*16];                                                   \
  int i;                                                                   \
                                                                           \
  for (y = 0; y < height; y += 16) {                                       \
    for (x = 0; x < width; x += 16) {                                      \
      w = h = 16;                                                          \
      if (width - x < 16)                                                  \
        w = width - x;                                                     \
      if (height - y < 16)                                                 \
        h = height - y;                                                    \
                                                                           \
      subencoding = getc (in);                                             \
      if (subencoding == EOF) {                                            \
        fprintf (stderr, "Read error.\n");                                 \
        return -1;                                                         \
      }                                                                    \
                                                                           \
      if (subencoding & rfbHextileRaw) {                                   \
        if (fread (data, 1, w * h * (bpp / 8), in) != w * h * (bpp / 8)) { \
          fprintf (stderr, "Read error.\n");                               \
          return -1;                                                       \
        }                                                                  \
                                                                           \
        copy_data ((char *)data, width, height, x, y, w, h, bpp);          \
        continue;                                                          \
      }                                                                    \
                                                                           \
      if (subencoding & rfbHextileBackgroundSpecified) {                   \
        if (fread (&bg, 1, (bpp / 8), in) != (bpp / 8)) {                  \
          fprintf (stderr, "Read error.\n");                               \
          return -1;                                                       \
        }                                                                  \
      }                                                                    \
                                                                           \
      for (i = 0; i < w * h; i++)                                          \
        data[i] = bg;                                                      \
                                                                           \
      if (subencoding & rfbHextileForegroundSpecified) {                   \
        if (fread (&fg, 1, (bpp / 8), in) != (bpp / 8)) {                  \
          fprintf (stderr, "Read error.\n");                               \
          return -1;                                                       \
        }                                                                  \
      }                                                                    \
                                                                           \
      if (!(subencoding & rfbHextileAnySubrects)) {                        \
        copy_data ((char *)data, width, height, x, y, w, h, bpp);          \
        continue;                                                          \
      }                                                                    \
                                                                           \
      if ((n_subrects = getc (in)) == EOF) {                               \
        fprintf (stderr, "Read error.\n");                                 \
        return -1;                                                         \
      }                                                                    \
                                                                           \
      for (i = 0; i < n_subrects; i++) {                                   \
        if (subencoding & rfbHextileSubrectsColoured) {                    \
          if (fread (&fg, 1, (bpp / 8), in) != (bpp / 8)) {                \
            fprintf (stderr, "Read error.\n");                             \
            return -1;                                                     \
          }                                                                \
        }                                                                  \
        if ((coord_pair = getc (in)) == EOF) {                             \
          fprintf (stderr, "Read error.\n");                               \
          return -1;                                                       \
        }                                                                  \
        sx = rfbHextileExtractX (coord_pair);                              \
        sy = rfbHextileExtractY (coord_pair);                              \
        if ((coord_pair = getc (in)) == EOF) {                             \
          fprintf (stderr, "Read error.\n");                               \
          return -1;                                                       \
        }                                                                  \
        sw = rfbHextileExtractW (coord_pair);                              \
        sh = rfbHextileExtractH (coord_pair);                              \
        if (sx + sw > w || sy + sh > h) {                                  \
          fprintf (stderr, "Wrong hextile data, please use"                \
                           " appropriate -8/-16/-24 option.\n");           \
          return -1;                                                       \
        }                                                                  \
                                                                           \
        for (jy = sy; jy < sy + sh; jy++) {                                \
          for (jx = sx; jx < sx + sw; jx++)                                \
            data[jy * w + jx] = fg;                                        \
        }                                                                  \
      }                                                                    \
      copy_data ((char *)data, width, height, x, y, w, h, bpp);            \
    }                                                                      \
  }                                                                        \
  return 0;                                                                \
}

DEFINE_HANDLE_HEXTILE(8)
DEFINE_HANDLE_HEXTILE(16)
DEFINE_HANDLE_HEXTILE(32)

static void copy_data (char *data, int rw, int rh,
                       int x, int y, int w, int h, int bpp)
{
  int px, py;
  int pixel_bytes;

  pixel_bytes = bpp / 8;

  rfbScreen.paddedWidthInBytes = rw * pixel_bytes;
  rfbScreen.width = rw;
  rfbScreen.height = rh;
  rfbScreen.sizeInBytes = rw * rh * pixel_bytes;

  for (py = y; py < y + h; py++) {
    memcpy (&rfbScreen.pfbMemory[(py * rw + x) * pixel_bytes], data,
            w * pixel_bytes);
    data += w * pixel_bytes;
  }
}

static CARD32 get_CARD32 (char *ptr)
{
  return (CARD32) ntohl (*(unsigned long *)ptr);
}

static CARD16 get_CARD16 (char *ptr)
{
  return (CARD16) ntohs (*(unsigned short *)ptr);
}
