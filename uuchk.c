/* uuchk.c
   Display what we think the permissions of systems are.

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
   Revision 1.2  1991/09/19  02:22:44  ian
   Chip Salzenberg's patch to allow ";retrytime" at the end of a time string

   Revision 1.1  1991/09/10  19:40:31  ian
   Initial revision

   */

#include "uucp.h"

#if USE_RCS_ID
char uuchk_rcsid[] = "$Id$";
#endif

#include <signal.h>
#include <string.h>

#include "getopt.h"

#include "port.h"
#include "system.h"
#include "sysdep.h"

/* Local functions.  */

static void ukusage P((void));
static sigret_t ukcatch P((int isig));
static void ukshow P((const struct ssysteminfo *qsys));
static boolean fkshow_port P((struct sport *qport, boolean fin));
static void ukshow_dialer P((struct sdialer *qdial));
static void ukshow_size P((const char *z, boolean fcall, boolean flocal));
static void ukshow_proto_params P((int c, struct sproto_param *pas,
				   int cindent));

/* Long getopt options.  */

static const struct option asKlongopts[] = { { NULL, 0, NULL, 0 } };

const struct option *_getopt_long_options = asKlongopts;

int
main (argc, argv)
     int argc;
     char **argv;
{
  int iopt;
  /* The configuration file name.  */
  const char *zconfig = NULL;
  /* The command argument debugging level.  */
  int idebug = -1;
  int c;
  struct ssysteminfo *pas;
  int i;

  while ((iopt = getopt (argc, argv, "I:x:")) != EOF)
    {
      switch (iopt)
	{
	case 'I':
	  /* Set the configuration file name.  */
	  zconfig = optarg;
	  break;

	case 'x':
	  /* Set the debugging level.  */
	  idebug = atoi (optarg);
	  break;

	case 0:
	  /* Long option found and flag set.  */
	  break;

	default:
	  ukusage ();
	  break;
	}
    }

  if (optind != argc)
    ukusage ();

  uread_config (zconfig);

  /* Let the command line arguments override the configuration file.  */
  if (idebug != -1)
    iDebug = idebug;

  /* The only signal we need to catch is SIGABRT, and we only need to
     catch it so that we can behave sensibly on a LOG_FATAL error.
     There are no cleanup actions to take, so we can let other signals
     do whatever they like.  */

#ifdef SIGABRT
  (void) signal (SIGABRT, ukcatch);
#endif

  usysdep_initialize (FALSE);

  uread_all_system_info (&c, &pas);

  for (i = 0; i < c; i++)
    {
      ukshow (&pas[i]);
      if (i < c - 1)
	printf ("\n");
    }

  ulog_close ();

  usysdep_exit (TRUE);

  /* Avoid errors about not returning a value.  */
  return 0;
}

/* Print a usage message and die.  */

static void
ukusage ()
{
  fprintf (stderr,
	   "Usage: uuchk [-I file] [-x debug]\n");
  fprintf (stderr,
	   " -x debug: Set debugging level (0 for none, 9 is max)\n");
#if HAVE_TAYLOR_CONFIG
  fprintf (stderr,
	   " -I file: Set configuration file to use (default %s)\n",
	   CONFIGFILE);
#endif /* HAVE_TAYLOR_CONFIG */
  exit (EXIT_FAILURE);
}

/* Catch a signal (we only do this because a fatal error raises
   SIGABRT).  */

static sigret_t
ukcatch (isig)
     int isig;
{
  if (! fAborting)
    ulog (LOG_ERROR, "Got signal %d", isig);

  ulog_close ();

  signal (isig, SIG_DFL);

  if (fAborting)
    usysdep_exit (FALSE);
  else
    raise (isig);
}

/* Dump out the information for a system.  */

static void
ukshow (qsys)
     const struct ssysteminfo *qsys;
{
  int i;
  const struct ssysteminfo *qlast;

  printf ("System: %s", qsys->zname);
  if (qsys->zalias != NULL)
    printf (" (%s)", qsys->zalias);
  printf ("\n");

  qlast = NULL;
  for (i = 0; qsys != NULL; qlast = qsys, qsys = qsys->qalternate, i++)
    {
      char *z;
      boolean fcall, fcalled, fend;

      if (i != 0 || qsys->qalternate != NULL)
	printf ("Alternate %d\n", i);

      /* See if this alternate could be used when calling out.  */

      fcall = (i == 0
	       || qsys->ztime != qlast->ztime
	       || qsys->zport != qlast->zport
	       || qsys->qport != qlast->qport
	       || qsys->ibaud != qlast->ibaud
	       || qsys->zphone != qlast->zphone
	       || qsys->zchat != qlast->zchat);

      if (fcall && strcasecmp (qsys->ztime, "zNever") == 0)
	fcall = FALSE;

      /* If this is the first alternate, it can be used to accept
	 a call.  Otherwise it can be used if it specifies a different
	 login name than the previous alternate.  */
      fcalled = (i == 0
		 || (qlast != NULL
		     && qsys->zcalled_login != NULL
		     && (qlast->zcalled_login == NULL
			 || strcmp (qsys->zcalled_login,
				    qlast->zcalled_login) != 0)));

      if (! fcall && ! fcalled)
	{
	  printf (" This alternate is never used\n");
	  continue;
	}

      if (fcalled)
	{
	  if (qsys->zcalled_login != NULL
	      && strcmp (qsys->zcalled_login, "ANY") != 0)
	    {
	      if (i == 0 && qsys->qalternate == NULL)
		printf (" Caller must log in as %s\n", qsys->zcalled_login);
	      else
		printf (" When called using login name %s\n",
			qsys->zcalled_login);
	    }
	  else
	    printf (" When called using any login name\n");

	  if (qsys->zlocalname != NULL)
	    printf (" Will use %s as name of local system\n",
		    qsys->zlocalname);
	}

      if (fcalled && qsys->fcallback)
	{
	  printf (" If called, will call back\n");
	  fcalled = FALSE;
	}

      if (fcall)
	{
	  if (i == 0 && qsys->qalternate == NULL)
	    printf (" Call out");
	  else
	    printf (" This alternate applies when calling");
	  
	  if (qsys->zport != NULL || qsys->qport != NULL)
	    {
	      printf (" using ");
	      if (qsys->zport != NULL)
		printf ("port %s", qsys->zport);
	      else
		printf ("a specially defined port");
	      if (qsys->ibaud != 0)
		{
		  printf (" at speed %ld", qsys->ibaud);
		  if (qsys->ihighbaud != 0)
		    printf (" to %ld", qsys->ihighbaud);
		}
	      printf ("\n");
	    }
	  else if (qsys->ibaud != 0)
	    {
	      printf (" at speed %ld", qsys->ibaud);
	      if (qsys->ihighbaud != 0)
		printf (" to %ld", qsys->ihighbaud);
	      printf ("\n");
	    }
	  else
	    printf (" using any port\n");

	  if (qsys->qport != NULL)
	    {
	      printf (" The port is defined as:\n");
	      (void) fkshow_port (qsys->qport, FALSE);
	    }
	  else
	    {
	      struct sport sdummy;

	      printf (" The possible ports are:\n");
	      (void) ffind_port (qsys->zport, qsys->ibaud,
				 qsys->ihighbaud, &sdummy,
				 fkshow_port, FALSE);
	    }

	  if (qsys->zphone != NULL)
	    printf (" Phone number %s\n", qsys->zphone);

	  if (qsys->zchat != NULL)
	    {
	      printf (" Chat script %s\n", qsys->zchat);
	      printf (" Chat script timeout %d\n", qsys->cchat_timeout);
	      if (qsys->zchat_fail != NULL)
		printf (" Chat failure strings %s\n", qsys->zchat_fail);
	    }

	  if (qsys->zcall_login != NULL)
	    {
	      if (strcmp (qsys->zcall_login, "*") != 0)
		printf (" Login name %s\n", qsys->zcall_login);
	      else
		{
		  char *zlogin, *zpass;

		  if (! fcallout_login (qsys, &zlogin, &zpass))
		    printf (" Can not determine login name\n");
		  else
		    {
		      printf (" Login name %s\n", zlogin);
		      xfree ((pointer) zlogin);
		      xfree ((pointer) zpass);
		    }
		}
	    }

	  if (qsys->zcall_password != NULL)
	    {
	      if (strcmp (qsys->zcall_password, "*") != 0)
		printf (" Password %s\n", qsys->zcall_password);
	      else
		{
		  char *zlogin, *zpass;

		  if (! fcallout_login (qsys, &zlogin, &zpass))
		    printf (" Can not determine password\n");
		  else
		    {
		      printf (" Password %s\n", zpass);
		      xfree ((pointer) zlogin);
		      xfree ((pointer) zpass);
		    }
		}
	    }

	  z = alloca (strlen (qsys->ztime) + 1);
	  strcpy (z, qsys->ztime);
	  do
	    {
	      int c;

	      c = strcspn (z, " ");
	      fend = z[c] == '\0';
	      z[c] = '\0';

	      if (strcasecmp (z + 1, "never") != 0)
		{
		  char *zsemi;

		  printf (" If there is ");
		  if (*z == BGRADE_LOW)
		    printf ("any work");
		  else
		    printf ("work of grade %c or higher", *z);
		  zsemi = strchr (z, ';');
		  if (zsemi != NULL)
		    *zsemi = '\0';
		  printf (" may call at time %s", z + 1);
		  if (zsemi != NULL)
		    printf (" (retry time %d)", atoi (zsemi + 1));
		  printf ("\n");
		}

	      z += c + 1;
	    }
	  while (! fend);

	  if (qsys->zcalltimegrade != NULL)
	    {
	      z = alloca (strlen (qsys->zcalltimegrade) + 1);
	      strcpy (z, qsys->zcalltimegrade);
	      do
		{
		  int c;

		  c = strcspn (z, " ");
		  fend = z[c] == '\0';
		  z[c] = '\0';

		  printf (" If calling at time %s will accept ", z + 1);
		  if (*z == BGRADE_LOW)
		    printf ("any work");
		  else
		    printf ("work of grade %c or higher", *z);
		  printf ("\n");

		  z += c + 1;
		}
	      while (! fend);
	    }
	}

      if (qsys->fsequence)
	printf (" Sequence numbers are used\n");

      if (fcall)
	{
	  ukshow_size (qsys->zcall_local_size, TRUE, TRUE);
	  ukshow_size (qsys->zcall_remote_size, TRUE, FALSE);
	}
      if (fcalled)
	{
	  ukshow_size (qsys->zcalled_local_size, FALSE, TRUE);
	  ukshow_size (qsys->zcalled_remote_size, FALSE, TRUE);
	}

      if (fcall)
	{
	  printf (" %sllow remote requests when calling\n",
		  qsys->fcall_request ? "A" : "Do not a");
	  printf (" May %smake local requests when calling\n",
		  qsys->fcall_transfer ? "" : "not ");
	}

      if (fcalled)
	{
	  printf (" %sllow remote requests when called\n",
		  qsys->fcalled_request ? "A" : "Do not a");
	  printf (" May %smake local requests when called\n",
		  qsys->fcalled_transfer ? "" : "not ");
	}

      if (qsys->fcall_transfer || qsys->fcalled_transfer)
	printf (" May send by local request: %s\n", qsys->zlocal_send);
      if (qsys->fcall_request || qsys->fcalled_request)
	printf (" May send by remote request: %s\n", qsys->zremote_send);
      if (qsys->fcall_transfer || qsys->fcalled_transfer)
	printf (" May accept by local request: %s\n", qsys->zlocal_receive);
      if (qsys->fcall_request || qsys->fcalled_request)
	printf (" May accept by remote request: %s\n", qsys->zremote_receive);

      printf (" May execute %s (path %s)\n", qsys->zcmds, qsys->zpath);

      if (qsys->cfree_space != 0)
	printf (" Will leave %ld bytes available\n", qsys->cfree_space);

      if (qsys->zpubdir != NULL)
	printf (" Public directory is %s\n", qsys->zpubdir);

      if (qsys->zprotocols != NULL)
	printf (" Will use protocols %s\n", qsys->zprotocols);
      else
	printf (" Will use any known protocol\n");

      if (qsys->cproto_params != 0)
	ukshow_proto_params (qsys->cproto_params, qsys->qproto_params, 1);
    }
}

/* Show information about a port.  */

/*ARGSUSED*/
static boolean
fkshow_port (qport, fin)
     struct sport *qport;
     boolean fin;
{
  printf ("  Port name %s\n", qport->zname);
  switch (qport->ttype)
    {
    case PORTTYPE_STDIN:
      printf ("   Port type stdin\n");
      break;
    case PORTTYPE_DIRECT:
      printf ("   Port type direct\n");
      if (qport->u.sdirect.zdevice != NULL)
	printf ("   Device %s\n", qport->u.sdirect.zdevice);
      printf ("   Speed %ld\n", qport->u.sdirect.ibaud);
      break;
    case PORTTYPE_MODEM:
      printf ("   Port type modem\n");
      if (qport->u.smodem.zdevice != NULL)
	printf ("   Device %s\n", qport->u.smodem.zdevice);
      if (qport->u.smodem.zdial_device != NULL)
	printf ("   Dial device %s\n", qport->u.smodem.zdial_device);
      printf ("   Speed %ld\n", qport->u.smodem.ibaud);
      if (qport->u.smodem.ilowbaud != qport->u.smodem.ihighbaud)
	printf ("   Speed range %ld to %ld\n", qport->u.smodem.ilowbaud,
		qport->u.smodem.ihighbaud);
      printf ("   Carrier %savailable\n",
	      qport->u.smodem.fcarrier ? "" : "not ");
      if (qport->u.smodem.qdialer != NULL)
	{
	  printf ("   Specially defined dialer\n");
	  ukshow_dialer (qport->u.smodem.qdialer);
	}
      else if (qport->u.smodem.zdialer != NULL)
	{
	  struct sdialer sdial;

	  printf ("   Dialer %s\n", qport->u.smodem.zdialer);
	  if (fread_dialer_info (qport->u.smodem.zdialer, &sdial))
	    ukshow_dialer (&sdial);
	}
      break;
    default:
      printf ("   CAN'T HAPPEN\n");
      break;
    }

  if (qport->cproto_params != 0)
    ukshow_proto_params (qport->cproto_params, qport->qproto_params, 3);

  /* Return FALSE to force ffind_port to continue searching.  */
  return FALSE;
}

/* Show information about a dialer.  */

static void
ukshow_dialer (q)
     struct sdialer *q;
{
  if (q->zchat != NULL)
    {
      printf ("    Chat script %s\n", q->zchat);
      printf ("    Chat script timeout %d\n", q->cchat_timeout);
      if (q->zchat_fail != NULL)
	printf ("    Chat failure strings %s\n", q->zchat_fail);
    }
  if (q->zdialtone != NULL)
    printf ("    Wait for dialtone %s\n", q->zdialtone);
  if (q->zpause != NULL)
    printf ("    Pause while dialing %s\n", q->zpause);
  printf ("    Carrier %savailable\n", q->fcarrier ? "" : "not ");
  if (q->fcarrier)
    printf ("    Wait %d seconds for carrier\n", q->ccarrier_wait);
  if (q->fdtr_toggle)
    {
      printf ("    Toggle DTR");
      if (q->fdtr_toggle_wait)
	printf (" and wait");
      printf ("\n");
    }
  if (q->zcomplete)
    printf ("    When complete %s\n", q->zcomplete);
  if (q->zabort)
    printf ("    When aborting %s\n", q->zabort);
  if (q->cproto_params != 0)
    ukshow_proto_params (q->cproto_params, q->qproto_params, 4);
}

/* Show a size/time restriction.  */

static void
ukshow_size (zstring, fcall, flocal)
     const char *zstring;
     boolean fcall;
     boolean flocal;
{
  char *z;
  boolean fend;

  if (zstring == NULL)
    return;

  z = (char *) alloca (sizeof (zstring) + 1);
  strcpy (z, zstring);
  do
    {
      long isize;
      int c;

      isize = strtol (z, &z, 10);

      ++z;
      c = strcspn (z, " ");
      fend = z[c] == '\0';
      z[c] = '\0';

      printf (" If call");
      if (fcall)
	printf ("ing");
      else
	printf ("ed");
      printf (" at time %s permit ", z);
      if (flocal)
	printf ("local");
      else
	printf ("remote");
      printf ("ly request transfers of up to %ld bytes\n", isize);

      z += c + 1;
    }
  while (! fend);
}

/* Show protocol parameters.  */

static void
ukshow_proto_params (c, pas, cindent)
     int c;
     struct sproto_param *pas;
     int cindent;
{
  int ip;

  for (ip = 0; ip < c; ip++)
    {
      int ie, isp;

      for (isp = 0; isp < cindent; isp++)
	printf (" ");
      printf ("For protocol %c will use the following parameters\n",
	      pas[ip].bproto);
      for (ie = 0; ie < pas[ip].centries; ie++)
	{
	  int ia;
	  struct sproto_param_entry *qe;

	  qe = &pas[ip].qentries[ie];
	  for (isp = 0; isp < cindent; isp++)
	    printf (" ");
	  for (ia = 0; ia < qe->cargs; ia++)
	    printf (" %s", qe->azargs[ia]);
	  printf ("\n");
	}
    }
}
