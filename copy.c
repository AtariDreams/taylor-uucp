/* copy.c
   Copy one file to another for the UUCP package.

   Copyright (C) 1991 Ian Lance Taylor

   This file is part of the Taylor UUCP package.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

   The author of the program may be contacted at ian@airs.com or
   c/o AIRS, P.O. Box 520, Waltham, MA 02254.

   $Log$
   */

#include "uucp.h"

#if USE_RCS_ID
char copy_rcsid[] = "$Id$";
#endif

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "system.h"
#include "sysdep.h"

/* Copy one file to another.  The new file is created private to UUCP.  */

#if USE_STDIO

boolean
fcopy_file (zfrom, zto, fpublic)
     const char *zfrom;
     const char *zto;
     boolean fpublic;
{
  FILE *efrom;
  FILE *eto;
  char ab[8192];
  int c;

  efrom = fopen (zfrom, BINREAD);
  if (efrom == NULL)
    {
      ulog (LOG_ERROR, "fopen (%s): %s", zfrom, strerror (errno));
      return FALSE;
    }
  eto = esysdep_fopen (zto, fpublic);
  if (eto == NULL)
    {
      (void) fclose (efrom);
      return FALSE;
    }

  while ((c = fread (ab, sizeof (char), sizeof ab, efrom)) != 0)
    {
      if (fwrite (ab, sizeof (char), c, eto) != c)
	{
	  ulog (LOG_ERROR, "fwrite: %s", strerror (errno));
	  (void) fclose (efrom);
	  (void) fclose (eto);
	  (void) remove (zto);
	  return FALSE;
	}
    }

  (void) fclose (efrom);

  if (fclose (eto) != 0)
    {
      ulog (LOG_ERROR, "fclose: %s", strerror (errno));
      (void) remove (zto);
      return FALSE;
    }

  return TRUE;
}

#else /* USE_STDIO */

#include <unistd.h>
#include <fcntl.h>

boolean
fcopy_file (zfrom, zto, fpublic)
     const char *zfrom;
     const char *zto;
     boolean fpublic;
{
  int ofrom;
  int oto;
  char ab[8192];
  int c;

  ofrom = open (zfrom, O_RDONLY, 0);
  if (ofrom < 0)
    {
      ulog (LOG_ERROR, "open (%s): %s", zfrom, strerror (errno));
      return FALSE;
    }

  /* These file mode arguments are from the UNIX version of sysdep.h;
     each system dependent header file will need their own
     definitions.  */
  oto = open (zto, O_WRONLY | O_CREAT | O_TRUNC,
	      fpublic ? IPUBLIC_FILE_MODE : IPRIVATE_FILE_MODE);
  if (oto < 0)
    {
      (void) close (ofrom);
      return FALSE;
    }

  while ((c = read (ofrom, ab, sizeof ab)) > 0)
    {
      if (write (oto, ab, c) != c)
	{
	  ulog (LOG_ERROR, "write: %s", strerror (errno));
	  (void) close (ofrom);
	  (void) close (oto);
	  (void) remove (zto);
	  return FALSE;
	}
    }

  (void) close (ofrom);

  if (close (oto) == EOF)
    {
      ulog (LOG_ERROR, "close: %s", strerror (errno));
      (void) remove (zto);
      return FALSE;
    }

  if (c < 0)
    {
      ulog (LOG_ERROR, "read: %s", strerror (errno));
      (void) remove (zto);
      return FALSE;
    }

  return TRUE;
}

#endif /* ! USE_STDIO */
