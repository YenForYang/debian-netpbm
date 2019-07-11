/*
** pngtopnm.c -
** read a Portable Network Graphics file and produce a portable anymap
**
** Copyright (C) 1995,1998 by Alexander Lehmann <alex@hal.rhein-main.de>
**                        and Willem van Schaik <willem@schaik.com>
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
**
** modeled after giftopnm by David Koblas and
** with lots of bits pasted from libpng.txt by Guy Eric Schalnat
*/

/* 
   BJH 20000408:  rename PPM_MAXMAXVAL to PPM_OVERALLMAXVAL
   BJH 20000303:  fix include statement so dependencies work out right.
*/
/* GRR 19991203:  moved VERSION to new version.h header file */

/* GRR 19990713:  fixed redundant freeing of png_ptr and info_ptr in setjmp()
 *  blocks and added "pm_close(ifp)" in each.  */

/* GRR 19990317:  declared "clobberable" automatic variables in convertpng()
 *  static to fix Solaris/gcc stack-corruption bug.  Also installed custom
 *  error-handler to avoid jmp_buf size-related problems (i.e., jmp_buf
 *  compiled with one size in libpng and another size here).  */

#include <math.h>
#include <string.h>
#include <zlib.h>
#include <png.h>	/* includes zlib.h and setjmp.h */
#define VERSION "2.37.4 (5 December 1999) +netpbm"

#include "pnm.h"

typedef struct _jmpbuf_wrapper {
  jmp_buf jmpbuf;
} jmpbuf_wrapper;

/* GRR 19991205:  this is used as a test for pre-1999 versions of netpbm and
 *   pbmplus vs. 1999 or later (in which pm_close was split into two)
 */
#ifdef PBMPLUS_RAWBITS
#  define pm_closer pm_close
#  define pm_closew pm_close
#endif

#ifndef TRUE
#  define TRUE 1
#endif
#ifndef FALSE
#  define FALSE 0
#endif
#ifndef NONE
#  define NONE 0
#endif

/* function prototypes */
#ifdef __STDC__
static png_uint_16 _get_png_val (png_byte **pp, int bit_depth);
static void store_pixel (xel *pix, png_uint_16 r, png_uint_16 g, png_uint_16 b,
                         png_uint_16 a);
static int iscolor (png_color c);
static void save_text (png_struct *png_ptr, png_info *info_ptr, FILE *tfp);
static void show_time (png_struct *png_ptr, png_info *info_ptr);
static void pngtopnm_error_handler (png_structp png_ptr, png_const_charp msg);
static void convertpng (FILE *ifp, FILE *tfp);
int main (int argc, char *argv[]);
#endif

enum alpha_handling
  {none, alpha_only, mix};

static png_uint_16 maxval, maxmaxval;
static png_uint_16 bgr, bgg, bgb; /* background colors */
static int verbose = FALSE;
static enum alpha_handling alpha = none;
static int background = -1;
static char *backstring;
static float displaygamma = -1.0; /* display gamma */
static float totalgamma = -1.0;
static int text = FALSE;
static char *text_file;
static int mtime = FALSE;
static jmpbuf_wrapper pngtopnm_jmpbuf_struct;

#define get_png_val(p) _get_png_val (&(p), bit_depth)

#ifdef __STDC__
static png_uint_16 _get_png_val (png_byte **pp, int bit_depth)
#else
static png_uint_16 _get_png_val (pp, bit_depth)
png_byte **pp;
int bit_depth;
#endif
{
  png_uint_16 c = 0;

  if (bit_depth == 16) {
    c = (*((*pp)++)) << 8;
  }
  c |= (*((*pp)++));

  if (maxval > maxmaxval)
    c /= ((maxval + 1) / (maxmaxval + 1));

  return c;
}

#ifdef __STDC__
static void store_pixel (xel *pix, png_uint_16 r, png_uint_16 g, png_uint_16 b, png_uint_16 a)
#else
static void store_pixel (pix, r, g, b, a)
xel *pix;
png_uint_16 r, g, b, a;
#endif
{
  if (alpha == alpha_only) {
    PNM_ASSIGN1 (*pix, a);
  } else {
    if ((alpha == mix) && (a != maxval)) {
      r = r * (double)a / maxval + ((1.0 - (double)a / maxval) * bgr);
      g = g * (double)a / maxval + ((1.0 - (double)a / maxval) * bgg);
      b = b * (double)a / maxval + ((1.0 - (double)a / maxval) * bgb);
    }
    PPM_ASSIGN (*pix, r, g, b);
  }
}

#ifdef __STDC__
static png_uint_16 gamma_correct (png_uint_16 v, float g)
#else
static png_uint_16 gamma_correct (v, g)
png_uint_16 v;
float g;
#endif
{
  if (g != -1.0)
    return (png_uint_16) (pow ((double) v / maxval, 
			       (1.0 / g)) * maxval + 0.5);
  else
    return v;
}

#ifdef __STDC__
static int iscolor (png_color c)
#else
static int iscolor (c)
png_color c;
#endif
{
  return c.red != c.green || c.green != c.blue;
}

#ifdef __STDC__
static void save_text (png_struct *png_ptr, png_info *info_ptr, FILE *tfp)
#else
static void save_text (png_ptr, info_ptr, tfp)
png_struct *png_ptr;
png_info *info_ptr;
FILE *tfp;
#endif
{
  int i, j, k;
  png_textp text;

  int num_text = png_get_text(png_ptr, info_ptr, &text, NULL);

  for (i = 0 ; i < num_text ; i++) {
    j = 0;
    while (text[i].key[j] != '\0' && text[i].key[j] != ' ')
      j++;    
    if (text[i].key[j] != ' ') {
      fprintf (tfp, "%s", text[i].key);
      for (j = strlen (text[i].key) ; j < 15 ; j++)
        putc (' ', tfp);
    } else {
      fprintf (tfp, "\"%s\"", text[i].key);
      for (j = strlen (text[i].key) ; j < 13 ; j++)
        putc (' ', tfp);
    }
    putc (' ', tfp); /* at least one space between key and text */
    
    for (j = 0 ; j < text[i].text_length ; j++) {
      putc (text[i].text[j], tfp);
      if (text[i].text[j] == '\n')
        for (k = 0 ; k < 16 ; k++)
          putc ((int)' ', tfp);
    }
    putc ((int)'\n', tfp);
  }
}

#ifdef __STDC__
static void show_time (png_struct *png_ptr, png_info *info_ptr)
#else
static void show_time (png_struct *png_ptr, info_ptr)
png_struct *png_ptr;
png_info *info_ptr;
#endif
{
  static char *month[] =
    {"", "January", "February", "March", "April", "May", "June",
     "July", "August", "September", "October", "November", "December"};

  if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tIME)) {
    png_timep mod_time;
    png_get_tIME(png_ptr, info_ptr, &mod_time);
    if (mod_time->month > 12) mod_time->month = 0;
    pm_message ("modification time: %02d %s %d %02d:%02d:%02d",
                mod_time->day, month[mod_time->month],
                mod_time->year, mod_time->hour,
                mod_time->minute, mod_time->second);
  }
}

#ifdef __STDC__
static void pngtopnm_error_handler (png_structp png_ptr, png_const_charp msg)
#else
static void pngtopnm_error_handler (png_ptr, msg)
png_structp png_ptr;
png_const_charp msg;
#endif
{
  jmpbuf_wrapper  *jmpbuf_ptr;

  /* this function, aside from the extra step of retrieving the "error
   * pointer" (below) and the fact that it exists within the application
   * rather than within libpng, is essentially identical to libpng's
   * default error handler.  The second point is critical:  since both
   * setjmp() and longjmp() are called from the same code, they are
   * guaranteed to have compatible notions of how big a jmp_buf is,
   * regardless of whether _BSD_SOURCE or anything else has (or has not)
   * been defined. */

  fprintf(stderr, "pnmtopng:  fatal libpng error: %s\n", msg);
  fflush(stderr);

  jmpbuf_ptr = png_get_error_ptr(png_ptr);
  if (jmpbuf_ptr == NULL) {         /* we are completely hosed now */
    fprintf(stderr,
      "pnmtopng:  EXTREMELY fatal error: jmpbuf unrecoverable; terminating.\n");
    fflush(stderr);
    exit(99);
  }

  longjmp(jmpbuf_ptr->jmpbuf, 1);
}

#define SIG_CHECK_SIZE 4

#ifdef __STDC__
static void convertpng (FILE *ifp, FILE *tfp)
#else
static void convertpng (ifp, tfp)
FILE *ifp;
FILE *tfp;
#endif
{
  unsigned char sig_buf [SIG_CHECK_SIZE];
  png_struct *png_ptr;
  png_info *info_ptr;
  pixel *row;
  png_byte **png_image;
  png_byte *png_pixel;
  pixel *pnm_pixel;
  int x, y;
  int linesize;
  png_uint_16 c, c2, c3, a;
  int pnm_type;
  int i;
  int trans_mix;
  pixel backcolor;
  char gamma_string[80];

  /* these variables are declared static because gcc wasn't kidding
   * about "variable XXX might be clobbered by `longjmp' or `vfork'"
   * (stack corruption observed on Solaris 2.6 with gcc 2.8.1, even
   * in the absence of any other error condition) */
  static char *type_string;
  static char *alpha_string;

  type_string = alpha_string = "";

  if (fread (sig_buf, 1, SIG_CHECK_SIZE, ifp) != SIG_CHECK_SIZE) {
    pm_closer (ifp);
    pm_error ("input file empty or too short");
  }
  if (png_sig_cmp (sig_buf, (png_size_t) 0, (png_size_t) SIG_CHECK_SIZE) != 0) {
    pm_closer (ifp);
    pm_error ("input file not a PNG file");
  }

  png_ptr = png_create_read_struct (PNG_LIBPNG_VER_STRING,
    &pngtopnm_jmpbuf_struct, pngtopnm_error_handler, NULL);
  if (png_ptr == NULL) {
    pm_closer (ifp);
    pm_error ("cannot allocate LIBPNG structure");
  }

  info_ptr = png_create_info_struct (png_ptr);
  if (info_ptr == NULL) {
    png_destroy_read_struct (&png_ptr, (png_infopp)NULL, (png_infopp)NULL);
    pm_closer (ifp);
    pm_error ("cannot allocate LIBPNG structures");
  }

  if (setjmp (pngtopnm_jmpbuf_struct.jmpbuf)) {
    png_destroy_read_struct (&png_ptr, &info_ptr, (png_infopp)NULL);
    pm_closer (ifp);
    pm_error ("setjmp returns error condition");
  }

  png_init_io (png_ptr, ifp);
  png_set_sig_bytes (png_ptr, SIG_CHECK_SIZE);
  png_read_info (png_ptr, info_ptr);

  png_uint_32 width;
  png_uint_32 height;
  int bit_depth;
  int color_type;
  int interlace_type;
  int compression_method;
  int filter_method;

  png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type,
               &interlace_type, &compression_method, &filter_method);

  if (verbose) {
    switch (color_type) {
      case PNG_COLOR_TYPE_GRAY:
        type_string = "gray";
        alpha_string = "";
        break;

      case PNG_COLOR_TYPE_GRAY_ALPHA:
        type_string = "gray";
        alpha_string = "+alpha";
        break;

      case PNG_COLOR_TYPE_PALETTE:
        type_string = "palette";
        alpha_string = "";
        break;

      case PNG_COLOR_TYPE_RGB:
        type_string = "truecolor";
        alpha_string = "";
        break;

      case PNG_COLOR_TYPE_RGB_ALPHA:
        type_string = "truecolor";
        alpha_string = "+alpha";
        break;
    }
    if (png_get_valid(png_ptr, info_ptr,PNG_INFO_tRNS)) {
      alpha_string = "+transparency";
    }

    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_gAMA)) {
        double gamma;
        png_get_gAMA(png_ptr, info_ptr, &gamma);
      sprintf (gamma_string, ", image gamma = %4.2f", gamma);
    } else {
      strcpy (gamma_string, "");
    }

    if (verbose) {
      pm_message ("reading a %dw x %dh image, %d bit%s %s%s%s%s",
		  width, height,
		  bit_depth, bit_depth > 1 ? "s" : "",
		  type_string, alpha_string, gamma_string,
		  interlace_type ? ", Adam7 interlaced" : "");
      png_color_16p background;
      png_get_bKGD(png_ptr, info_ptr, &background);
      pm_message ("background {index, gray, red, green, blue} = "
                  "{%d, %d, %d, %d, %d}",
                  background->index,
                  background->gray,
                  background->red,
                  background->green,
                  background->blue);
    }
  }

  png_image = (png_byte **)malloc (height * sizeof (png_byte*));
  if (png_image == NULL) {
    png_destroy_read_struct (&png_ptr, &info_ptr, (png_infopp)NULL);
    pm_closer (ifp);
    pm_error ("couldn't allocate space for image");
  }

  if (bit_depth == 16)
  {
    overflow2(2, width);
    linesize = 2 * width;
  }
  else
    linesize = width;

  if (color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
  {
    linesize *= 2;
    overflow2(2, linesize);
  }
  else
  if (color_type == PNG_COLOR_TYPE_RGB)
  {
    overflow2(3, linesize);
    linesize *= 3;
  }
  else
  if (color_type == PNG_COLOR_TYPE_RGB_ALPHA)
  {
    overflow2(4, linesize);
    linesize *= 4;
  }

  for (y = 0 ; y < height ; y++) {
    png_image[y] = malloc (linesize);
    if (png_image[y] == NULL) {
      for (x = 0 ; x < y ; x++)
        free (png_image[x]);
      free (png_image);
      png_destroy_read_struct (&png_ptr, &info_ptr, (png_infopp)NULL);
      pm_closer (ifp);
      pm_error ("couldn't allocate space for image");
    }
  }

  if (bit_depth < 8)
    png_set_packing (png_ptr);

  if (color_type == PNG_COLOR_TYPE_PALETTE) {
    maxval = 255;
  } else {
    maxval = (1l << bit_depth) - 1;
  }

  /* gamma-correction */
  if (displaygamma != -1.0) {
    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_gAMA)) {
        double gamma;
        png_get_gAMA(png_ptr, info_ptr, &gamma);

      if (displaygamma != gamma) {
        png_set_gamma (png_ptr, displaygamma, gamma);
	totalgamma = (double) gamma * (double) displaygamma;
	/* in case of gamma-corrections, sBIT's as in the PNG-file are not valid anymore */
        png_set_invalid(png_ptr, info_ptr, PNG_INFO_sBIT);
        if (verbose)
          pm_message ("image gamma is %4.2f, converted for display gamma of %4.2f",
                    gamma, displaygamma);
      }
    } else {
      double gamma = -1.0;
      // BUG? gAMA was found to be invalid in the if-path of this else.
      png_get_gAMA(png_ptr, info_ptr, &gamma);
      if (displaygamma != gamma) {
	png_set_gamma (png_ptr, displaygamma, 1.0);
	totalgamma = (double) displaygamma;
        png_set_invalid(png_ptr, info_ptr, PNG_INFO_sBIT);
	if (verbose)
	  pm_message ("image gamma assumed 1.0, converted for display gamma of %4.2f",
		      displaygamma);
      }
    }
  }

  /* sBIT handling is very tricky. If we are extracting only the image, we
     can use the sBIT info for grayscale and color images, if the three
     values agree. If we extract the transparency/alpha mask, sBIT is
     irrelevant for trans and valid for alpha. If we mix both, the
     multiplication may result in values that require the normal bit depth,
     so we will use the sBIT info only for transparency, if we know that only
     solid and fully transparent is used */

  if (png_get_valid(png_ptr, info_ptr, PNG_INFO_sBIT)) {
    switch (alpha) {
      case mix:
        if (color_type == PNG_COLOR_TYPE_RGB_ALPHA ||
            color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
          break;
        if (color_type == PNG_COLOR_TYPE_PALETTE &&
            png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)) {
          trans_mix = TRUE;
          png_bytep trans = NULL;
          int num_trans = 0;
          png_color_16p trans_values;
          png_get_tRNS(png_ptr, info_ptr, &trans, &num_trans, &trans_values);
          for (i = 0 ; i < num_trans ; i++)
            if (trans[i] != 0 && trans[i] != 255) {
              trans_mix = FALSE;
              break;
            }
          if (!trans_mix)
            break;
        }

        /* else fall though to normal case */

      case none:
      {
        png_color_8p sig_bit;
        png_get_sBIT(png_ptr, info_ptr, &sig_bit);
        if ((color_type == PNG_COLOR_TYPE_PALETTE ||
             color_type == PNG_COLOR_TYPE_RGB ||
             color_type == PNG_COLOR_TYPE_RGB_ALPHA) &&
            (sig_bit->red != sig_bit->green ||
             sig_bit->red != sig_bit->blue) &&
            alpha == none) {
	  pm_message ("different bit depths for color channels not supported");
	  pm_message ("writing file with %d bit resolution", bit_depth);
        } else {
          if ((color_type == PNG_COLOR_TYPE_PALETTE) &&
	      (sig_bit->red < 255)) {
            png_colorp palette;
            int num_palette;
            png_get_PLTE(png_ptr, info_ptr, &palette, &num_palette);

	    for (i = 0 ; i < num_palette ; i++) {
	      palette[i].red   >>= (8 - sig_bit->red);
	      palette[i].green >>= (8 - sig_bit->green);
	      palette[i].blue  >>= (8 - sig_bit->blue);
	    }
	    maxval = (1l << sig_bit->red) - 1;
	    if (verbose)
	      pm_message ("image has fewer significant bits, writing file with %d bits per channel", 
		sig_bit->red);
          } else
          if ((color_type == PNG_COLOR_TYPE_RGB ||
               color_type == PNG_COLOR_TYPE_RGB_ALPHA) &&
	      (sig_bit->red < bit_depth)) {
	    png_set_shift (png_ptr, sig_bit);
	    maxval = (1l << sig_bit->red) - 1;
	    if (verbose)
	      pm_message ("image has fewer significant bits, writing file with %d bits per channel", 
		sig_bit->red);
          } else 
          if ((color_type == PNG_COLOR_TYPE_GRAY ||
               color_type == PNG_COLOR_TYPE_GRAY_ALPHA) &&
	      (sig_bit->gray < bit_depth)) {
	    png_set_shift (png_ptr, sig_bit);
	    maxval = (1l << sig_bit->gray) - 1;
	    if (verbose)
	      pm_message ("image has fewer significant bits, writing file with %d bits",
		sig_bit->gray);
          }
        }
        break;
      }

      case alpha_only: {
        png_color_8p sig_bit;
        png_get_sBIT(png_ptr, info_ptr, &sig_bit);

          if ((color_type == PNG_COLOR_TYPE_RGB_ALPHA ||
             color_type == PNG_COLOR_TYPE_GRAY_ALPHA) &&
	    (sig_bit->gray < bit_depth)) {
	  png_set_shift (png_ptr, sig_bit);
	  if (verbose)
	    pm_message ("image has fewer significant bits, writing file with %d bits", 
		sig_bit->alpha);
	  maxval = (1l << sig_bit->alpha) - 1;
        }
        break;
      }

      }
  }
  /* didn't manage to get libpng to work (bugs?) concerning background */
  /* processing, therefore we do our own using bgr, bgg and bgb        */
  if (png_get_valid(png_ptr, info_ptr,  PNG_INFO_bKGD)) {
    png_color_16p background;
    png_get_bKGD(png_ptr, info_ptr, &background);

    switch (color_type) {
      case PNG_COLOR_TYPE_GRAY:
      case PNG_COLOR_TYPE_GRAY_ALPHA:
        bgr = bgg = bgb = gamma_correct (background->gray, totalgamma);
        break;
      case PNG_COLOR_TYPE_PALETTE: {
        png_colorp palette;
        int num_palette;
        png_get_PLTE(png_ptr, info_ptr, &palette, &num_palette);

        bgr = gamma_correct (palette[background->index].red, totalgamma);
        bgg = gamma_correct (palette[background->index].green, totalgamma);
        bgb = gamma_correct (palette[background->index].blue, totalgamma);
        break;
      }
      case PNG_COLOR_TYPE_RGB:
      case PNG_COLOR_TYPE_RGB_ALPHA:
        bgr = gamma_correct (background->red, totalgamma);
        bgg = gamma_correct (background->green, totalgamma);
        bgb = gamma_correct (background->blue, totalgamma);
        break;
    }
  }
  else
    /* when no background given, we use white [from version 2.37] */
    bgr = bgg = bgb = maxval;

  /* but if background was specified from the command-line, we always use that	*/
  /* I chose to do no gamma-correction in this case; which is a bit arbitrary	*/
  if (background > -1)
  {
    backcolor = ppm_parsecolor (backstring, maxval);
    switch (color_type) {
      case PNG_COLOR_TYPE_GRAY:
      case PNG_COLOR_TYPE_GRAY_ALPHA:
        bgr = bgg = bgb = PNM_GET1 (backcolor);
        break;
      case PNG_COLOR_TYPE_PALETTE:
      case PNG_COLOR_TYPE_RGB:
      case PNG_COLOR_TYPE_RGB_ALPHA:
        bgr = PPM_GETR (backcolor);
        bgg = PPM_GETG (backcolor);
        bgb = PPM_GETB (backcolor);
        break;
    }
  }

  png_read_image (png_ptr, png_image);
  png_read_end (png_ptr, info_ptr);

  if (mtime)
    show_time (png_ptr, info_ptr);
  if (text)
    save_text (png_ptr, info_ptr, tfp);

  if (png_get_valid(png_ptr, info_ptr,  PNG_INFO_pHYs)) {
    float r;
    png_uint_32 x_pixels_per_unit, y_pixels_per_unit;
    int unit_type;
    png_get_pHYs(png_ptr, info_ptr, &x_pixels_per_unit, &y_pixels_per_unit, &unit_type);
    r = (float)x_pixels_per_unit / y_pixels_per_unit;
    if (r != 1.0) {
      pm_message ("warning - non-square pixels; to fix do a 'pnmscale -%cscale %g'",
		    r < 1.0 ? 'x' : 'y',
		    r < 1.0 ? 1.0 / r : r );
    }
  }

  if ((row = pnm_allocrow (width)) == NULL) {
    for (y = 0 ; y < height ; y++)
      free (png_image[y]);
    free (png_image);
    png_destroy_read_struct (&png_ptr, &info_ptr, (png_infopp)NULL);
    pm_closer (ifp);
    pm_error ("couldn't allocate space for image");
  }

  if (alpha == alpha_only) {
    if (color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_RGB) {
      pnm_type = PBM_TYPE;
    } else
      if (color_type == PNG_COLOR_TYPE_PALETTE) {
        pnm_type = PBM_TYPE;
        if (png_get_valid(png_ptr, info_ptr,  PNG_INFO_tRNS)) {
          png_bytep trans = NULL;
          int num_trans = 0;
          png_color_16p trans_values;
          png_get_tRNS(png_ptr, info_ptr, &trans, &num_trans, &trans_values);

         for (i = 0 ; i < num_trans ; i++) {
            if (trans[i] != 0 && trans[i] != maxval) {
              pnm_type = PGM_TYPE;
              break;
            }
          }
        }
      } else {
        if (maxval == 1)
          pnm_type = PBM_TYPE;
        else
          pnm_type = PGM_TYPE;
      }
  } else {
    if (color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
      if (bit_depth == 1) {
        pnm_type = PBM_TYPE;
      } else {
        pnm_type = PGM_TYPE;
      }
    } else
    if (color_type == PNG_COLOR_TYPE_PALETTE) {
      pnm_type = PGM_TYPE;
      png_colorp palette;
      int num_palette;
      png_get_PLTE(png_ptr, info_ptr, &palette, &num_palette);
      for (i = 0 ; i < num_palette ; i++) {
        if (iscolor (palette[i])) {
          pnm_type = PPM_TYPE;
          break;
        }
      }
    } else {
      pnm_type = PPM_TYPE;
    }
  }

  if ((pnm_type == PGM_TYPE) && (maxval > PGM_OVERALLMAXVAL))
    maxmaxval = PGM_OVERALLMAXVAL;
  else if ((pnm_type == PPM_TYPE) && (maxval > PPM_OVERALLMAXVAL))
    maxmaxval = PPM_OVERALLMAXVAL;
  else maxmaxval = maxval;

  if (verbose)
    pm_message ("writing a %s file (maxval=%u)",
                pnm_type == PBM_TYPE ? "PBM" :
                pnm_type == PGM_TYPE ? "PGM" :
                pnm_type == PPM_TYPE ? "PPM" :
                                       "UNKNOWN!", 
		maxmaxval);

  pnm_writepnminit (stdout, width, height, maxval,
                    pnm_type, FALSE);

  for (y = 0 ; y < height ; y++) {

    png_pixel = png_image[y];
    pnm_pixel = row;
    for (x = 0 ; x < width ; x++) {
      c = get_png_val (png_pixel);
      switch (color_type) {
        case PNG_COLOR_TYPE_GRAY: {
          png_bytep trans = NULL;
          int num_trans = 0;
          png_color_16p trans_values;
          png_get_tRNS(png_ptr, info_ptr, &trans, &num_trans, &trans_values);

          store_pixel (pnm_pixel, c, c, c,
		((png_get_valid(png_ptr, info_ptr,  PNG_INFO_tRNS)) &&
		 (c == gamma_correct (trans_values->gray, totalgamma))) ?
			0 : maxval);
          break;
        }

        case PNG_COLOR_TYPE_GRAY_ALPHA:
          a = get_png_val (png_pixel);
          store_pixel (pnm_pixel, c, c, c, a);
          break;

        case PNG_COLOR_TYPE_PALETTE: {
          png_colorp palette;
          int num_palette;
          png_get_PLTE(png_ptr, info_ptr, &palette, &num_palette);

          png_bytep trans = NULL;
          int num_trans = 0;
          png_color_16p trans_values;
          if (png_get_valid(png_ptr, info_ptr,  PNG_INFO_tRNS)) {
            png_get_tRNS(png_ptr, info_ptr, &trans, &num_trans, &trans_values);
          }

          store_pixel (pnm_pixel, palette[c].red,
                       palette[c].green, palette[c].blue,
                       (png_get_valid(png_ptr, info_ptr,  PNG_INFO_tRNS)) &&
                        c<num_trans ?
                        trans[c] : maxval);
          break;
        }

        case PNG_COLOR_TYPE_RGB: {
          png_bytep trans = NULL;
          int num_trans = 0;
          png_color_16p trans_values;
          if (png_get_valid(png_ptr, info_ptr,  PNG_INFO_tRNS)) {
            png_get_tRNS(png_ptr, info_ptr, &trans, &num_trans, &trans_values);
          }

          c2 = get_png_val (png_pixel);
          c3 = get_png_val (png_pixel);
          store_pixel (pnm_pixel, c, c2, c3,
		((png_get_valid(png_ptr, info_ptr,  PNG_INFO_tRNS)) &&
		 (c  == gamma_correct (trans_values->red, totalgamma)) &&
		 (c2 == gamma_correct (trans_values->green, totalgamma)) &&
		 (c3 == gamma_correct (trans_values->blue, totalgamma))) ?
			0 : maxval);
          break;
        }

        case PNG_COLOR_TYPE_RGB_ALPHA:
          c2 = get_png_val (png_pixel);
          c3 = get_png_val (png_pixel);
          a = get_png_val (png_pixel);
          store_pixel (pnm_pixel, c, c2, c3, a);
          break;

        default:
          pnm_freerow (row);
          for (i = 0 ; i < height ; i++)
            free (png_image[i]);
          free (png_image);
          png_destroy_read_struct (&png_ptr, &info_ptr, (png_infopp)NULL);
          pm_closer (ifp);
          pm_error ("unknown PNG color type");
      }
      pnm_pixel++;
    }
    pnm_writepnmrow (stdout, row, width, maxval, pnm_type, FALSE);
  }

  fflush(stdout);
  pnm_freerow (row);
  for (y = 0 ; y < height ; y++)
    free (png_image[y]);
  free (png_image);
  png_destroy_read_struct (&png_ptr, &info_ptr, (png_infopp)NULL);
}

#ifdef __STDC__
int main (int argc, char *argv[])
#else
int main (argc, argv)
int argc;
char *argv[];
#endif
{
  FILE *ifp, *tfp;
  int argn;

  char *usage = "[-verbose] [-alpha | -mix] [-background color] ...\n\
             ... [-gamma value] [-text file] [-time] [pngfile]";

  pnm_init (&argc, argv);
  argn = 1;

  while (argn < argc && argv[argn][0] == '-' && argv[argn][1] != '\0') {
    if (pm_keymatch (argv[argn], "-verbose", 2)) {
      verbose = TRUE;
    } else
    if (pm_keymatch (argv[argn], "-alpha", 2)) {
      alpha = alpha_only;
    } else
    if (pm_keymatch (argv[argn], "-mix", 2)) {
      alpha = mix;
    } else
    if (pm_keymatch (argv[argn], "-background", 2)) {
      background = 1;
      if (++argn < argc)
        backstring = argv[argn];
      else
        pm_usage (usage);
    } else
    if (pm_keymatch (argv[argn], "-gamma", 2)) {
      if (++argn < argc)
        sscanf (argv[argn], "%f", &displaygamma);
      else
        pm_usage (usage);
    } else
    if (pm_keymatch (argv[argn], "-text", 3)) {
      text = TRUE;
      if (++argn < argc)
        text_file = argv[argn];
      else
        pm_usage (usage);
    } else
    if (pm_keymatch (argv[argn], "-time", 3)) {
      mtime = TRUE;
    } else {
      fprintf(stderr,"pngtopnm version %s, compiled with libpng version %s\n",
        VERSION, PNG_LIBPNG_VER_STRING);
      pm_usage (usage);
    }
    argn++;
  }

  if (argn != argc) {
    ifp = pm_openr (argv[argn]);
    ++argn;
  } else {
    ifp = stdin;
  }
  if (argn != argc)
    pm_usage (usage);

  if (text)
    tfp = pm_openw (text_file);
  else
    tfp = NULL;

  convertpng (ifp, tfp);

  if (text)
    pm_closew (tfp);

  pm_closer (ifp);
  pm_closew (stdout);
  exit (0);
}
