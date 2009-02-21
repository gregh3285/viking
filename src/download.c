/*
 * viking -- GPS Data and Topo Analyzer, Explorer, and Manager
 *
 * Copyright (C) 2003-2005, Evan Battaglia <gtoevan@gmx.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <utime.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>


#include "download.h"

#include "curl_download.h"

gboolean a_check_html_file(FILE* f)
{
  gchar **s;
  gchar *bp;
  fpos_t pos;
  gchar buf[33];
  size_t nr;
  gchar * html_str[] = {
    "<html",
    "<!DOCTYPE html",
    "<head",
    "<title",
    NULL
  };

  memset(buf, 0, sizeof(buf));
  fgetpos(f, &pos);
  rewind(f);
  nr = fread(buf, 1, sizeof(buf) - 1, f);
  fsetpos(f, &pos);
  for (bp = buf; (bp < (buf + sizeof(buf) - 1)) && (nr > (bp - buf)); bp++) {
    if (!(isspace(*bp)))
      break;
  }
  if ((bp >= (buf + sizeof(buf) -1)) || ((bp - buf) >= nr))
    return FALSE;
  for (s = html_str; *s; s++) {
    if (strncasecmp(*s, bp, strlen(*s)) == 0)
      return TRUE;
  }
  return FALSE;
}

gboolean a_check_map_file(FILE* f)
{
  return !a_check_html_file(f);
}

static int download( const char *hostname, const char *uri, const char *fn, DownloadOptions *options, gboolean ftp)
{
  FILE *f;
  int ret;
  gchar *tmpfilename;
  gboolean failure = FALSE;
  time_t time_condition = 0;

  /* Check file */
  if ( g_file_test ( fn, G_FILE_TEST_EXISTS ) == TRUE )
  {
    if (options != NULL && options->check_file_server_time) {
      /* Get the modified time of this file */
      struct stat buf;
      g_stat ( fn, &buf );
      time_condition = buf.st_mtime;
    } else {
      /* Nothing to do as file already exists, so return */
      return -3;
    }
  } else {
    gchar *dir = g_path_get_dirname ( fn );
    g_mkdir_with_parents ( dir , 0777 );
    g_free ( dir );
  }

  tmpfilename = g_strdup_printf("%s.tmp", fn);
  f = g_fopen ( tmpfilename, "w+bx" );  /* truncate file and open it in exclusive mode */
  if ( ! f ) {
    if (errno == EEXIST)
      g_debug("%s: Couldn't take lock on temporary file \"%s\"\n", __FUNCTION__, tmpfilename);
    g_free ( tmpfilename );
    return -4;
  }

  /* Call the backend function */
  ret = curl_download_get_url ( hostname, uri, f, options, ftp, time_condition );

  if (ret != DOWNLOAD_NO_ERROR && ret != DOWNLOAD_NO_NEWER_FILE) {
    g_debug("%s: download failed: curl_download_get_url=%d", __FUNCTION__, ret);
    failure = TRUE;
  }

  if (!failure && options != NULL && options->check_file != NULL && ! options->check_file(f)) {
    g_debug("%s: file content checking failed", __FUNCTION__);
    failure = TRUE;
  }

  if (failure)
  {
    g_warning(_("Download error: %s"), fn);
    g_remove ( tmpfilename );
    g_free ( tmpfilename );
    fclose ( f );
    f = NULL;
    g_remove ( fn ); /* couldn't create temporary. delete 0-byte file. */
    return -1;
  }

  if (ret == DOWNLOAD_NO_NEWER_FILE)
    g_remove ( tmpfilename );
  else
    g_rename ( tmpfilename, fn ); /* move completely-downloaded file to permanent location */
  g_free ( tmpfilename );
  fclose ( f );
  f = NULL;
  return 0;
}

/* success = 0, -1 = couldn't connect, -2 HTTP error, -3 file exists, -4 couldn't write to file... */
/* uri: like "/uri.html?whatever" */
/* only reason for the "wrapper" is so we can do redirects. */
int a_http_download_get_url ( const char *hostname, const char *uri, const char *fn, DownloadOptions *opt )
{
  return download ( hostname, uri, fn, opt, FALSE );
}

int a_ftp_download_get_url ( const char *hostname, const char *uri, const char *fn, DownloadOptions *opt )
{
  return download ( hostname, uri, fn, opt, TRUE );
}
