/* tstuu.c
   Test the uucp package on a UNIX system.

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
   Revision 1.27  1991/12/29  00:55:23  ian
   Monty Solomon: added HAVE_UNION_WAIT

   Revision 1.26  1991/12/28  06:35:05  ian
   Use TIMES_TICK rather than CLK_TCK

   Revision 1.25  1991/12/28  06:10:50  ian
   Added HAVE_STRCHR and HAVE_INDEX to conf.h

   Revision 1.24  1991/12/28  04:05:13  ian
   Create spool directories

   Revision 1.23  1991/12/28  03:49:23  ian
   Added HAVE_MEMFNS and HAVE_BFNS; changed uses of memset to bzero

   Revision 1.22  1991/12/22  22:14:19  ian
   Monty Solomon: added HAVE_UNISTD_H configuration parameter

   Revision 1.21  1991/12/21  22:07:47  ian
   John Theus: don't warn if port file does not exist

   Revision 1.20  1991/12/19  04:25:57  ian
   Terry Gardner: configuration parameter to not use both NONBLOCK and NDELAY

   Revision 1.19  1991/12/19  03:55:40  ian
   Give the uucico processes a chance to die on their own

   Revision 1.18  1991/12/18  05:12:00  ian
   Added -l option to uucico to prompt for login name once and then exit

   Revision 1.17  1991/12/17  22:21:19  ian
   Sleep before printing login to wait until input has been flushed

   Revision 1.16  1991/12/17  04:55:01  ian
   David Nugent: ignore SIGHUP in uucico and uuxqt

   Revision 1.15  1991/12/11  04:21:37  ian
   Arne Ludwig: merge in Arne Ludwig's patches for V2 and BNU style logging

   Revision 1.14  1991/12/07  02:57:28  ian
   Allow failure message to be sent from uux test

   Revision 1.13  1991/12/01  19:58:01  ian
   Don't use the not-very-portable fd_set typedef at all

   Revision 1.12  1991/12/01  19:41:00  ian
   Don't read V2 or BNU configuration files while testing

   Revision 1.11  1991/12/01  03:29:30  ian
   Bob Izenberg: get definitions for EAGAIN and EWOULDBLOCK

   Revision 1.10  1991/12/01  02:23:12  ian
   Niels Baggesen: don't multiply include <unistd.h>

   Revision 1.9  1991/12/01  02:12:02  ian
   David Nugent: some systems don't define O_NDELAY

   Revision 1.8  1991/11/26  02:13:48  ian
   Bob Denny: Add definitions for FD_SET, FD_ZERO and FD_ISSET

   Revision 1.7  1991/11/26  01:45:42  ian
   Marty Shannon: configuration option to not include <sys/wait.h>

   Revision 1.6  1991/11/21  22:17:06  ian
   Add version string, print version when printing usage

   Revision 1.5  1991/11/14  21:07:15  ian
   Create port file and add protocol command for second system

   Revision 1.4  1991/11/12  19:47:04  ian
   Add called-chat set of commands to run a chat script on an incoming call

   Revision 1.3  1991/11/11  23:47:24  ian
   Added chat-program to run a program to do a chat script

   Revision 1.2  1991/11/11  04:21:16  ian
   Added 'f' protocol

   Revision 1.1  1991/09/10  19:40:31  ian
   Initial revision

   */

#include "uucp.h"

#if USE_RCS_ID
char tstuu_rcsid[] = "$Id$";
#endif

#include <stdio.h>
#include <signal.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#if USE_STDIO && HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "sysdep.h"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/times.h>

#if HAVE_SYSWAIT_H
#include <sys/wait.h>
#endif

#include "getopt.h"

/* We want an O_NDELAY definition.  */

#ifndef O_NDELAY
#ifdef FNDELAY
#define O_NDELAY FNDELAY
#else /* ! defined (FNDELAY) */
#define O_NDELAY 0
#endif /* ! defined (FNDELAY) */
#endif /* ! defined (O_NDELAY) */

/* We want an O_NONBLOCK definition.  */

#ifndef O_NONBLOCK
#ifdef FNBLOCK
#define O_NONBLOCK FNBLOCK
#else /* ! defined (FNBLOCK) */
#define O_NONBLOCK 0
#endif /* ! defined (FNBLOCK) */
#endif /* ! defined (O_NONBLOCK) */

#if USE_FOR_UNBLOCKED != 0
#define FILE_UNBLOCKED USE_FOR_UNBLOCKED
#else
#define FILE_UNBLOCKED (O_NDELAY | O_NONBLOCK)
#endif

/* Get definitions for both EAGAIN and EWOULDBLOCK.  */

#ifndef EAGAIN
#ifndef EWOULDBLOCK
#define EAGAIN (-1)
#define EWOULDBLOCK (-1)
#else /* defined (EWOULDBLOCK) */
#define EAGAIN EWOULDBLOCK
#endif /* defined (EWOULDBLOCK) */
#else /* defined (EAGAIN) */
#ifndef EWOULDBLOCK
#define EWOULDBLOCK EAGAIN
#endif /* ! defined (EWOULDBLOCK) */
#endif /* defined (EAGAIN) */

/* Make sure we have a CLK_TCK definition, even if it makes no sense.
   This is in case TIMES_TICK is defined as CLK_TCK.  */
#ifndef CLK_TCK
#define CLK_TCK (60)
#endif

#define ZUUCICO_CMD "login uucp"
#define UUCICO_EXECL "/bin/login", "login", "uucp"

/* External functions.  */

extern int select (), ioctl (), close (), dup2 (), execl (), access ();
extern int read (), write (), unlink (), open (), kill (), fcntl ();
extern int mkdir ();
extern int fclose (), fflush (), rand ();
extern unsigned int sleep ();
extern pid_t fork (), waitpid ();
extern clock_t times ();

#if ! HAVE_REMOVE
#define remove unlink
#endif

/* Local functions.  */

static void umake_file P((const char *zfile, int cextra));
static void uprepare_test P((int itest, boolean fcall_uucico,
			     const char *zsys));
static void ucheck_file P((const char *zfile, const char *zerr,
			   int cextra));
static void ucheck_test P((int itest, boolean fcall_uucico));
static void utransfer P((int ofrom, int oto, int otoslave, int *pc,
			 int *pcsleep));
static sigret_t uchild P((int isig));
static int cpshow P((char *z, int bchar));
static void xsystem P((const char *zcmd));

static int cDebug;
static int iTest;
static boolean fCall_uucico;
static int iPercent;
static pid_t iPid1, iPid2;
static int cFrom1, cFrom2;
static int cSleep1, cSleep2;
static char abLogout1[sizeof "tstout /dev/ptyp0"];
static char abLogout2[sizeof "tstout /dev/ptyp0"];
static char *zProtocols;

int
main (argc, argv)
     int argc;
     char **argv;
{
  int iopt;
  const char *zcmd1, *zcmd2;
  const char *zpty;
  const char *zsys;
  char abpty1[sizeof "/dev/ptyp0"];
  char abpty2[sizeof "/dev/ptyp0"];
  char *zptyname;
  int omaster1, oslave1, omaster2, oslave2;
  struct timeval stime;
  struct timeval spoll;

  zcmd1 = NULL;
  zcmd2 = NULL;
  zsys = "test2";

  while ((iopt = getopt (argc, argv, "c:p:s:t:ux1:2:")) != EOF)
    {
      switch (iopt)
	{
	case 'c':
	  zProtocols = optarg;
	  break;
	case 'p':
	  iPercent = atoi (optarg);
	  break;
	case 's':
	  zsys = optarg;
	  break;
	case 't':
	  iTest = atoi (optarg);
	  break;
	case 'u':
	  fCall_uucico = TRUE;
	  break;
	case 'x':
	  ++cDebug;
	  break;
	case '1':
	  zcmd1 = optarg;
	  break;
	case '2':
	  zcmd2 = optarg;
	  break;
	default:
	  fprintf (stderr,
		   "Taylor UUCP version %s, copyright (C) 1991 Ian Lance Taylor\n",
		   abVersion);
	  fprintf (stderr,
		   "Usage: tstuu [-x] [-t #] [-u] [-1 cmd] [-2 cmd]\n");
	  exit (EXIT_FAILURE);
	}
    }

  if (fCall_uucico && zcmd2 == NULL)
    zcmd2 = ZUUCICO_CMD;

  uprepare_test (iTest, fCall_uucico, zsys);

  (void) remove ("/usr/tmp/tstuu/spool1/core");
  (void) remove ("/usr/tmp/tstuu/spool2/core");

  omaster1 = -1;
  oslave1 = -1;
  omaster2 = -1;
  oslave2 = -1;
  zptyname = abpty1;

  for (zpty = "pqrs"; *zpty != '\0'; ++zpty)
    {
      int ipty;

      for (ipty = 0; ipty < 16; ipty++)
	{
	  int om, os;
	  FILE *e;
  
	  sprintf (zptyname, "/dev/pty%c%c", *zpty,
		   "0123456789abcdef"[ipty]);
	  om = open (zptyname, O_RDWR);
	  if (om < 0)
	    continue;
	  zptyname[5] = 't';
	  os = open (zptyname, O_RDWR);
	  if (os < 0)
	    {
	      (void) close (om);
	      continue;
	    }

	  if (omaster1 == -1)
	    {
	      omaster1 = om;
	      oslave1 = os;

	      e = fopen ("/usr/tmp/tstuu/pty1", "w");
	      if (e == NULL)
		{
		  perror ("fopen");
		  exit (EXIT_FAILURE);
		}
	      fprintf (e, "%s", zptyname + 5);
	      if (fclose (e) != 0)
		{
		  perror ("fclose");
		  exit (EXIT_FAILURE);
		}

	      zptyname = abpty2;
	    }
	  else
	    {
	      omaster2 = om;
	      oslave2 = os;

	      e = fopen ("/usr/tmp/tstuu/pty2", "w");
	      if (e == NULL)
		{
		  perror ("fopen");
		  exit (EXIT_FAILURE);
		}
	      fprintf (e, "%s", zptyname + 5);
	      if (fclose (e) != 0)
		{
		  perror ("fclose");
		  exit (EXIT_FAILURE);
		}
	      break;
	    }
	}

      if (omaster1 != -1 && omaster2 != -1)
	break;
    }

  if (omaster2 == -1)
    {
      fprintf (stderr, "No pseudo-terminals available\n");
      exit (EXIT_FAILURE);
    }

  /* Make sure we can or these into an int for the select call.  Most
     systems could use 31 instead of 15, but it should never be a
     problem.  */
  if (omaster1 > 15 || omaster2 > 15)
    {
      fprintf (stderr, "File descriptors are too large\n");
      exit (EXIT_FAILURE);
    }

  /* Prepare to log out the command if it is a login command.  On
     Ultrix 4.0 uucico can only be run from login for some reason.  */

  if (zcmd1 == NULL
      || strncmp (zcmd1, "login", sizeof "login" - 1) != 0)
    abLogout1[0] = '\0';
  else
    sprintf (abLogout1, "tstout %s", abpty1);

  if (zcmd2 == NULL
      || strncmp (zcmd2, "login", sizeof "login" - 1) != 0)
    abLogout2[0] = '\0';
  else
    sprintf (abLogout2, "tstout %s", abpty2);

  iPid1 = fork ();
  if (iPid1 < 0)
    {
      perror ("fork");
      exit (EXIT_FAILURE);
    }
  else if (iPid1 == 0)
    {
      if (close (0) < 0
	  || close (1) < 0
	  || close (omaster1) < 0
	  || close (omaster2) < 0
	  || close (oslave2) < 0)
	perror ("close");

      if (dup2 (oslave1, 0) < 0
	  || dup2 (oslave1, 1) < 0)
	perror ("dup2");

      if (close (oslave1) < 0)
	perror ("close");

      if (cDebug > 0)
	fprintf (stderr, "About to exec first process\n");

      if (zcmd1 != NULL)
	exit (system ((char *) zcmd1));
      else
	{
	  (void) execl ("uucico", "uucico", "-I", "/usr/tmp/tstuu/Config1",
			"-q", "-S", zsys, (const char *) NULL);
	  fprintf (stderr, "execl failed\n");
	  exit (EXIT_FAILURE);
	}
    }

  iPid2 = fork ();
  if (iPid2 < 0)
    {
      perror ("fork");
      kill (iPid1, SIGTERM);
      exit (EXIT_FAILURE);
    }
  else if (iPid2 == 0)
    {
      if (close (0) < 0
	  || close (1) < 0
	  || close (omaster1) < 0
	  || close (oslave1) < 0
	  || close (omaster2) < 0)
	perror ("close");

      if (dup2 (oslave2, 0) < 0
	  || dup2 (oslave2, 1) < 0)
	perror ("dup2");

      if (close (oslave2) < 0)
	perror ("close");

      if (cDebug > 0)
	fprintf (stderr, "About to exec second process\n");

      if (fCall_uucico)
	{
	  (void) execl (UUCICO_EXECL, (const char *) NULL);
	  fprintf (stderr, "execl failed\n");
	  exit (EXIT_FAILURE);
	}
      else if (zcmd2 != NULL)
	exit (system ((char *) zcmd2));
      else
	{
	  (void) execl ("uucico", "uucico", "-I", "/usr/tmp/tstuu/Config2",
			"-lq", (const char *)NULL);
	  fprintf (stderr, "execl failed\n");
	  exit (EXIT_FAILURE);
	}
    }

  signal (SIGCHLD, uchild);

  (void) fcntl (omaster1, F_SETFL, FILE_UNBLOCKED);
  (void) fcntl (omaster2, F_SETFL, FILE_UNBLOCKED);

  stime.tv_sec = 5;
  stime.tv_usec = 0;

  spoll.tv_sec = 0;
  spoll.tv_usec = 0;

  while (TRUE)
    {
      int iread;
      int iwrite;
      int cfds;

      iread = (1 << omaster1) | (1 << omaster2);

      cfds = select ((omaster1 > omaster2 ? omaster1 : omaster2) + 1,
		     &iread, (int *) NULL, (int *) NULL, &stime);
      if (cfds < 0)
	{
	  perror ("select");
	  uchild (SIGCHLD);
	}

      if (cfds == 0)
	{
	  if (cDebug > 0)
	    fprintf (stderr, "Five second pause\n");
	  continue;
	}

      if ((iread & (1 << omaster1)) != 0)
	{
	  iwrite = 1 << omaster2;
	  cfds = select (omaster2 + 1, (int *) NULL, &iwrite, (int *) NULL,
			 &spoll);
	  if (cfds < 0)
	    {
	      perror ("select");
	      uchild (SIGCHLD);
	    }
	  if (cfds > 0)
	    utransfer (omaster1, omaster2, oslave2, &cFrom1, &cSleep1);
	}
      if ((iread & (1 << omaster2)) != 0)
	{
	  iwrite = 1 << omaster1;
	  cfds = select (omaster1 + 1, (int *) NULL, &iwrite, (int *) NULL,
			 &spoll);
	  if (cfds < 0)
	    {
	      perror ("select");
	      uchild (SIGCHLD);
	    }
	  if (cfds > 0)
	    utransfer (omaster2, omaster1, oslave1, &cFrom2, &cSleep2);
	}
    }

  /*NOTREACHED*/
}

/* When a child dies, kill them both.  */

static sigret_t
uchild (isig)
     int isig;
{
  struct tms sbase, s1, s2;

  signal (SIGCHLD, SIG_DFL);

  /* Give the processes a chance to die on their own.  */
  sleep (1);

  (void) kill (iPid1, SIGTERM);
  (void) kill (iPid2, SIGTERM);

  (void) times (&sbase);
#if HAVE_UNION_WAIT
  (void) waitpid (iPid1, (union wait *) NULL, 0);
#else
  (void) waitpid (iPid1, (int *) NULL, 0);
#endif
  (void) times (&s1);
#if HAVE_UNION_WAIT
  (void) waitpid (iPid2, (union wait *) NULL, 0);
#else
  (void) waitpid (iPid2, (int *) NULL, 0);
#endif
  (void) times (&s2);

  fprintf (stderr,
	   " First child: user: %g; system: %g\n",
	   (double) (s1.tms_cutime - sbase.tms_cutime) / (double) TIMES_TICK,
	   (double) (s1.tms_cstime - sbase.tms_cstime) / (double) TIMES_TICK);
  fprintf (stderr,
	   "Second child: user: %g; system: %g\n",
	   (double) (s2.tms_cutime - s1.tms_cutime) / (double) TIMES_TICK,
	   (double) (s2.tms_cstime - s1.tms_cstime) / (double) TIMES_TICK);

  ucheck_test (iTest, fCall_uucico);

  if (abLogout1[0] != '\0')
    {
      if (cDebug > 0)
	fprintf (stderr, "Executing %s\n", abLogout1);
      (void) system (abLogout1);
    }
  if (abLogout2[0] != '\0')
    {
      if (cDebug > 0)
	fprintf (stderr, "Executing %s\n", abLogout2);
      (void) system (abLogout2);
    }

  fprintf (stderr, "Wrote %d bytes from 1 to 2 (slept %d)\n",
	   cFrom1, cSleep1);
  fprintf (stderr, "Wrote %d bytes from 2 to 1 (slept %d)\n",
	   cFrom2, cSleep2);

  if (access ("/usr/tmp/tstuu/spool1/core", R_OK) == 0)
    fprintf (stderr, "core file 1 exists\n");
  if (access ("/usr/tmp/tstuu/spool2/core", R_OK) == 0)
    fprintf (stderr, "core file 2 exists\n");

  exit (EXIT_SUCCESS);
}

/* Open a file without error.  */

static FILE *xfopen P((const char *zname, const char *zmode));

static FILE *
xfopen (zname, zmode)
     const char *zname;
     const char *zmode;
{
  FILE *eret;

  eret = fopen (zname, zmode);
  if (eret == NULL)
    {
      perror (zname);
      exit (EXIT_FAILURE);
    }
  return eret;
}

/* Close a file without error.  */

static void xfclose P((FILE *e));

static void
xfclose (e)
     FILE *e;
{
  if (fclose (e) != 0)
    {
      perror ("fclose");
      exit (EXIT_FAILURE);
    }
}

/* Create a test file.  */

static void
umake_file (z, c)
     const char *z;
     int c;
{
  int i;
  FILE *e;

  e = xfopen (z, "w");
	
  for (i = 0; i < 256; i++)
    {
      int i2;

      for (i2 = 0; i2 < 256; i2++)
	putc (i, e);
    }

  for (i = 0; i < c; i++)
    putc (i, e);

  xfclose (e);
}

/* Check a test file.  */

static void
ucheck_file (z, zerr, c)
     const char *z;
     const char *zerr;
     int c;
{
  int i;
  FILE *e;

  e = xfopen (z, "r");

  for (i = 0; i < 256; i++)
    {
      int i2;

      for (i2 = 0; i2 < 256; i2++)
	{
	  int bread;

	  bread = getc (e);
	  if (bread == EOF)
	    {
	      fprintf (stderr,
		       "%s: Unexpected EOF at position %d,%d\n",
		       zerr, i, i2);
	      xfclose (e);
	      return;
	    }
	  if (bread != i)
	    fprintf (stderr,
		     "%s: At position %d,%d got %d expected %d\n",
		     zerr, i, i2, bread, i);
	}
    }

  for (i = 0; i < c; i++)
    {
      int bread;

      bread = getc (e);
      if (bread == EOF)
	{
	  fprintf (stderr, "%s: Unexpected EOF at extra %d\n", zerr, i);
	  xfclose (e);
	  return;
	}
      if (bread != i)
	fprintf (stderr, "%s: At extra %d got %d expected %d\n",
		 zerr, i, bread, i);
    }

  if (getc (e) != EOF)
    fprintf (stderr, "%s: File is too long", zerr);

  xfclose (e);
}

/* Prepare all the configuration files for testing.  */

static void
uprepare_test (itest, fcall_uucico, zsys)
     int itest;
     boolean fcall_uucico;
     const char *zsys;
{
  FILE *e;
  const char *zuucp1, *zuucp2;
  const char *zuux1, *zuux2;
  char ab[1000];
  const char *zfrom;
  const char *zto;

  if (mkdir ("/usr/tmp/tstuu", S_IRWXU | S_IRWXG | S_IRWXO) != 0
      && errno != EEXIST)
    {
      perror ("mkdir");
      exit (EXIT_FAILURE);
    }

  if (mkdir ("/usr/tmp/tstuu/spool1", S_IRWXU | S_IRWXG | S_IRWXO) != 0
      && errno != EEXIST)
    {
      perror ("mkdir");
      exit (EXIT_FAILURE);
    }

  if (mkdir ("/usr/tmp/tstuu/spool2", S_IRWXU | S_IRWXG | S_IRWXO) != 0
      && errno != EEXIST)
    {
      perror ("mkdir");
      exit (EXIT_FAILURE);
    }

  e = xfopen ("/usr/tmp/tstuu/Config1", "w");

  fprintf (e, "# First test configuration file\n");
  fprintf (e, "nodename test1\n");
  fprintf (e, "spool /usr/tmp/tstuu/spool1\n");
  fprintf (e, "sysfile /usr/tmp/tstuu/System1\n");
  fprintf (e, "sysfile /usr/tmp/tstuu/System1.2\n");
  (void) remove ("/usr/tmp/tstuu/Log1");
#if ! HAVE_BNU_LOGGING
  fprintf (e, "logfile /usr/tmp/tstuu/Log1\n");
#else
  fprintf (e, "%s\n", "logfile /usr/tmp/tstuu/Log1/%s/%s");
#endif
  fprintf (e, "statfile /usr/tmp/tstuu/Stats1\n");
  fprintf (e, "debugfile /usr/tmp/tstuu/Debug1\n");
  fprintf (e, "callfile /usr/tmp/tstuu/Call1\n");
  fprintf (e, "pubdir /usr/tmp/tstuu\n");
#if HAVE_V2_CONFIG
  fprintf (e, "v2-files no\n");
#endif
#if HAVE_BNU_CONFIG
  fprintf (e, "bnu-files no\n");
#endif
  if (cDebug > 0)
    fprintf (e, "debug 9\n");

  xfclose (e);

  e = xfopen ("/usr/tmp/tstuu/System1", "w");

  fprintf (e, "# This file is ignored, to test multiple system files\n");
  fprintf (e, "time never\n");

  xfclose (e);

  e = xfopen ("/usr/tmp/tstuu/System1.2", "w");

  fprintf (e, "# First test system file\n");
  fprintf (e, "time any\n");
  fprintf (e, "port type stdin\n");
  fprintf (e, "port pty yes\n");
  fprintf (e, "# That was the defaults\n");
  fprintf (e, "system %s\n", zsys);
  if (! fcall_uucico)
    {
      FILE *eprog;

      eprog = xfopen ("/usr/tmp/tstuu/Chat1", "w");

      /* Wait for the other side to open the port and flush input.  */
      fprintf (eprog, "sleep 1\n");
      fprintf (eprog,
	       "echo password $1 speed $2 '(ignore this error)' 1>&2\n");
      fprintf (eprog, "echo test1\n");
      fprintf (eprog, "exit 0\n");

      xfclose (eprog);

      fprintf (e, "chat-program /bin/sh /usr/tmp/tstuu/Chat1 \\P \\S\n");

      fprintf (e, "chat word: \\P\n");
      fprintf (e, "chat-fail login;\n");
      fprintf (e, "call-login *\n");
      fprintf (e, "call-password *\n");
    }
  else
    fprintf (e, "chat \"\"\n");
  fprintf (e, "call-transfer yes\n");
  fprintf (e, "commands cat\n");
  if (! fcall_uucico && iPercent == 0)
    {
      fprintf (e, "protocol-parameter g window 7\n");
      fprintf (e, "protocol-parameter g packet-size 4096\n");
    }
  if (zProtocols != NULL)
    fprintf (e, "protocol %s\n", zProtocols);

  xfclose (e);

  e = xfopen ("/usr/tmp/tstuu/Call1", "w");

  fprintf (e, "Call out password file\n");
  fprintf (e, "%s test1 pass1\n", zsys);

  xfclose (e);

  if (! fcall_uucico)
    {
      FILE *eprog;

      e = xfopen ("/usr/tmp/tstuu/Config2", "w");

      fprintf (e, "# Second test configuration file\n");
      fprintf (e, "nodename test2\n");
      fprintf (e, "spool /usr/tmp/tstuu/spool2\n");
      fprintf (e, "sysfile /usr/tmp/tstuu/System2\n");
      (void) remove ("/usr/tmp/tstuu/Log2");
#if ! HAVE_BNU_LOGGING
      fprintf (e, "logfile /usr/tmp/tstuu/Log2\n");
#else
      fprintf (e, "%s\n", "logfile /usr/tmp/tstuu/Log2/%s/%s");
#endif
      fprintf (e, "statfile /usr/tmp/tstuu/Stats2\n");
      fprintf (e, "debugfile /usr/tmp/tstuu/Debug2\n");
      fprintf (e, "passwdfile /usr/tmp/tstuu/Pass2\n");
      fprintf (e, "pubdir /usr/tmp/tstuu\n");
#if HAVE_V2_CONFIG
      fprintf (e, "v2-files no\n");
#endif
#if HAVE_BNU_CONFIG
      fprintf (e, "bnu-files no\n");
#endif
      if (cDebug > 0)
	fprintf (e, "debug 9\n");

      xfclose (e);

      e = xfopen ("/usr/tmp/tstuu/System2", "w");

      fprintf (e, "# Second test system file\n");
      fprintf (e, "system test1\n");
      fprintf (e, "called-login test1\n");
      fprintf (e, "called-request true\n");
      if (zProtocols != NULL)
	fprintf (e, "protocol %s\n", zProtocols);

      eprog = xfopen ("/usr/tmp/tstuu/Chat2", "w");

      fprintf (eprog,
	       "echo port $1 '(ignore this error)' 1>&2\n");
      fprintf (eprog, "exit 0\n");

      xfclose (eprog);

      fprintf (e, "called-chat-program /bin/sh /usr/tmp/tstuu/Chat2 \\Y\n");
      fprintf (e, "time any\n");

      xfclose (e);

      e = xfopen ("/usr/tmp/tstuu/Pass2", "w");

      fprintf (e, "# Call in password file\n");
      fprintf (e, "test1 pass1\n");

      xfclose (e);
    }

  if (cDebug == 0)
    {
      zuucp1 = "./uucp -I /usr/tmp/tstuu/Config1 -r";
      zuux1 = "./uux -I /usr/tmp/tstuu/Config1 -r";
    }
  else
    {
      zuucp1 = "./uucp -I /usr/tmp/tstuu/Config1 -r -x 9";
      zuux1 = "./uux -I /usr/tmp/tstuu/Config1 -r -x 9";
    }

  if (fcall_uucico)
    {
      zuucp2 = "/usr/bin/uucp -r";
      zuux2 = "/usr/bin/uux -r";
    }
  else
    {
      if (cDebug == 0)
	{
	  zuucp2 = "./uucp -I /usr/tmp/tstuu/Config2 -r";
	  zuux2 = "./uux -I /usr/tmp/tstuu/Config2 -r";
	}
      else
	{
	  zuucp2 = "./uucp -I /usr/tmp/tstuu/Config2 -r -x 9";
	  zuux2 = "./uux -I /usr/tmp/tstuu/Config2 -r -x 9";
	}
    }

  /* Test transferring a file from the first system to the second.  */

  if (itest == 0 || itest == 1)
    {
      zfrom = "/usr/tmp/tstuu/from1";
      if (fcall_uucico)
	zto = "/usr/spool/uucppublic/to1";
      else
	zto = "/usr/tmp/tstuu/to1";

      (void) remove (zto);
      umake_file (zfrom, 0);

      sprintf (ab, "%s %s %s!%s", zuucp1, zfrom, zsys, zto);
      xsystem (ab);
    }

  /* Test having the first system request a file from the second.  */

  if (itest == 0 || itest == 2)
    {
      if (fcall_uucico)
	zfrom = "/usr/spool/uucppublic/from2";
      else
	zfrom = "/usr/tmp/tstuu/from2";
      zto = "/usr/tmp/tstuu/to2";

      (void) remove (zto);
      umake_file (zfrom, 3);

      sprintf (ab, "%s %s!%s %s", zuucp1, zsys, zfrom, zto);
      xsystem (ab);
    }

  /* Test having the second system send a file to the first.  */

  if (itest == 0 || itest == 3)
    {
      if (fcall_uucico)
	zfrom = "/usr/spool/uucppublic/from3";
      else
	zfrom = "/usr/tmp/tstuu/from3";
      zto = "/usr/tmp/tstuu/to3";

      (void) remove (zto);
      umake_file (zfrom, 5);

      sprintf (ab, "%s -c ~/from3 test1!~/to3", zuucp2);
      xsystem (ab);
    }

  /* Test having the second system request a file from the first.  */

  if (itest == 0 || itest == 4)
    {
      zfrom = "/usr/tmp/tstuu/from4";
      if (fcall_uucico)
	zto = "/usr/spool/uucppublic/to4";
      else
	zto = "/usr/tmp/tstuu/to4";

      (void) remove (zto);
      umake_file (zfrom, 7);

      sprintf (ab, "%s test1!%s %s", zuucp2, zfrom, zto);
      xsystem (ab);
    }

  /* Test having the second system make an execution request.  */

  if (itest == 0 || itest == 5)
    {
      zfrom = "/usr/tmp/tstuu/from5";
      if (fcall_uucico)
	zto = "/usr/spool/uucppublic/to5";
      else
	zto = "/usr/tmp/tstuu/to5";

      (void) remove (zto);
      umake_file (zfrom, 11);

      sprintf (ab, "%s test1!cat '<%s' '>%s'", zuux2, zfrom, zto);
      xsystem (ab);
    }

  /* Test having the first system request a wildcard.  */

  if (itest == 0 || itest == 6)
    {
      const char *zfrom1, *zfrom2;

      if (fcall_uucico)
	{
	  zfrom = "/usr/spool/uucppublic/to6\\*";
	  zfrom1 = "/usr/spool/uucppublic/to6.1";
	  zfrom2 = "/usr/spool/uucppublic/to6.2";
	}
      else
	{
	  zfrom = "/usr/tmp/tstuu/spool2/to6\\*";
	  zfrom1 = "/usr/tmp/tstuu/spool2/to6.1";
	  zfrom2 = "/usr/tmp/tstuu/spool2/to6.2";
	}

      umake_file (zfrom1, 100);
      umake_file (zfrom2, 101);
      (void) remove ("/usr/tmp/tstuu/to6.1");
      (void) remove ("/usr/tmp/tstuu/to6.2");

      sprintf (ab, "%s %s!%s /usr/tmp/tstuu", zuucp1, zsys, zfrom);
      xsystem (ab);
    }

  /* Test having the second system request a wildcard.  */

  if (itest == 0 || itest == 7)
    {
      const char *zto1, *zto2;

      if (fcall_uucico)
	{
	  zto = "/usr/spool/uucppublic";
	  zto1 = "/usr/spool/uucppublic/to7.1";
	  zto2 = "/usr/spool/uucppublic/to7.2";
	}
      else
	{
	  zto = "/usr/tmp/tstuu";
	  zto1 = "/usr/tmp/tstuu/to7.1";
	  zto2 = "/usr/tmp/tstuu/to7.2";
	}

      umake_file ("/usr/tmp/tstuu/spool1/to7.1", 150);
      umake_file ("/usr/tmp/tstuu/spool1/to7.2", 155);
      (void) remove (zto1);
      (void) remove (zto2);

      sprintf (ab, "%s test1!/usr/tmp/tstuu/spool1/to7.\\* %s", zuucp2,
	       zto);
      xsystem (ab);
    }
}

/* Try to make sure the file transfers were successful.  */

static void
ucheck_test (itest, fcall_uucico)
     int itest;
     boolean fcall_uucico;
{
  if (itest == 0 || itest == 1)
    {
      if (fcall_uucico)
	ucheck_file ("/usr/spool/uucppublic/to1", "test 1", 0);
      else
	ucheck_file ("/usr/tmp/tstuu/to1", "test 1", 0);
    }

  if (itest == 0 || itest == 2)
    ucheck_file ("/usr/tmp/tstuu/to2", "test 2", 3);

  if (itest == 0 || itest == 3)
    ucheck_file ("/usr/tmp/tstuu/to3", "test 3", 5);

  if (itest == 0 || itest == 4)
    {
      if (fcall_uucico)
	ucheck_file ("/usr/spool/uucppublic/to4", "test 4", 7);
      else
	ucheck_file ("/usr/tmp/tstuu/to4", "test 4", 7);
    }

  if (itest == 0 || itest == 6)
    {
      ucheck_file ("/usr/tmp/tstuu/to6.1", "test 6.1", 100);
      ucheck_file ("/usr/tmp/tstuu/to6.2", "test 6.2", 101);
    }

  if (itest == 0 || itest == 7)
    {
      const char *zto1, *zto2;

      if (fcall_uucico)
	{
	  zto1 = "/usr/spool/uucppublic/to7.1";
	  zto2 = "/usr/spool/uucppublic/to7.2";
	}
      else
	{
	  zto1 = "/usr/tmp/tstuu/to7.1";
	  zto2 = "/usr/tmp/tstuu/to7.2";
	}

      ucheck_file (zto1, "test 7.1", 150);
      ucheck_file (zto2, "test 7.2", 155);
    }
}

/* A debugging routine used when displaying buffers.  */

static int
cpshow (z, ichar)
     char *z;
     int ichar;
{
  if (isprint (BUCHAR (ichar)) && ichar != '\"')
    {
      *z = (char) ichar;
      return 1;
    }

  *z++ = '\\';

  switch (ichar)
    {
    case '\n':
      *z = 'n';
      return 2;
    case '\r':
      *z = 'r';
      return 2;
    case '\"':
      *z = '\"';
      return 2;
    default:
      sprintf (z, "%03o", (unsigned int)(ichar & 0xff));
      return strlen (z) + 1;
    }
}      

/* Transfer data from one pseudo-terminal to the other.  */

static void
utransfer (ofrom, oto, otoslave, pc, pcsleep)
     int ofrom;
     int oto;
     int otoslave;
     int *pc;
     int *pcsleep;
{
  int cread;
#ifdef FIONREAD
  char abbuf[10000];
#else
  char abbuf[80];
#endif
  char *zwrite;

  cread = read (ofrom, abbuf, sizeof abbuf);
  if (cread < 0)
    {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
	cread = 0;
      else
	{
	  perror ("read");
	  uchild (SIGCHLD);
	}
    }

  if (cDebug > 0)
    {
      char abshow[325];
      char *zshow;
      int i;

      zshow = abshow;
      for (i = 0; i < cread && i < 80; i++)
	zshow += cpshow (zshow, abbuf[i]);
      if (i < cread)
	{
	  *zshow++ = '.';
	  *zshow++ = '.';
	  *zshow++ = '.';
	}
      *zshow = '\0';
      fprintf (stderr, "Writing to %d: %d \"%s\"\n", oto, cread, abshow);
      fflush (stderr);
    }

  if (iPercent > 0)
    {
      int i;
      int c;

      c = 0;
      for (i = 0; i < cread; i++)
	{
	  if (rand () % 100 < iPercent)
	    {
	      ++abbuf[i];
	      ++c;
	    }
	}
      if (cDebug > 0 && c > 0)
	fprintf (stderr, "Clobbered %d bytes\n", c);
    }

  zwrite = abbuf;
  while (cread > 0)
    {
      long cunread;
      int cdo;
      int cwrote;
  
#ifdef FIONREAD
      if (ioctl (otoslave, FIONREAD, &cunread) < 0)
	{
	  perror ("FIONREAD");
	  uchild (SIGCHLD);
	}
      if (cDebug > 0)
	fprintf (stderr, "%ld unread\n", cunread);
#else /* ! FIONREAD */
      cunread = 0;
#endif /* ! FIONREAD */

      cdo = cread;
      if (256 - cunread < cdo)
	{
	  cdo = 256 - cunread;
	  if (cdo == 0)
	    {
	      ++*pcsleep;
	      (void) sleep (1);
	      continue;
	    }
	}

      cwrote = write (oto, zwrite, cdo);
      if (cwrote < 0)
	{
	  perror ("write");
	  uchild (SIGCHLD);
	}
      cread -= cwrote;
      zwrite += cwrote;
      *pc += cwrote;
    }
}

/* A version of the system command that checks for errors.  */

static void
xsystem (zcmd)
     const char *zcmd;
{
  int istat;

  istat = system ((char *) zcmd);
  if (istat != 0)
    {
      fprintf (stderr, "Command failed with status %d\n", istat);
      fprintf (stderr, "%s\n", zcmd);
      exit (EXIT_FAILURE);
    }
}

/* We don't want to link in util.c, since that would bring in the log
   file stuff.  Instead, we have local copies of functions that may be
   needed by getopt.c.  This should be done in a cleaner way.  */

#if ! HAVE_MEMFNS && ! HAVE_BFNS

/* Copy one block of memory to another.  */

pointer
memcpy (ptoarg, pfromarg, c)
     pointer ptoarg;
     constpointer pfromarg;
     int c;
{
  char *pto = (char *) ptoarg;
  const char *pfrom = (const char *) pfromarg;

  while (c-- != 0)
    *pto++ = *pfrom++;
  return ptoarg;
}

#endif /* ! HAVE_MEMFNS && ! HAVE_BFNS */

#if ! HAVE_STRCHR && ! HAVE_INDEX

/* Look for a character in a string.  This is supposed to work for a
   null byte, although we never actually call it with one.  */

char *
strchr (z, b)
     const char *z;
     int b;
{
  b = (char) b;
  while (*z != b)
    if (*z++ == '\0')
      return NULL;
  return (char *) z;
}

#endif /* ! HAVE_STRCHR && ! HAVE_INDEX */

#if ! HAVE_ALLOCA

/* The GNU getopt function calls alloca, and we don't want to link
   util.c with this program since that would bring in the log file
   stuff and other junk we don't need.  */

pointer
alloca (isize)
     int isize;
{
  return malloc (isize);
}

#endif /* HAVE_ALLOCA */
