/* prot.c
   Protocol support routines to move commands and data around.

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
char prot_rcsid[] = "$Id$";
#endif

#include <string.h>
#include <errno.h>

#include "system.h"
#include "port.h"
#include "prot.h"

/* This file implements the generic UUCP protocol for making and
   confirming file transfer requests.  This involves sending ASCII
   strings back and forth between the communicating daemons.  It would
   be possible to use a different scheme when designing a new
   protocol, but this scheme is used by all traditional UUCP
   protocols.  */

/* Local functions.  */

static boolean fpsendfile_confirm P((void));
static boolean fprecfile_confirm P((void));
static boolean fploop P((void));
static void upadd_cmd P((const char *z, int clen, boolean flast));
static const char *zpget_cmd P((void));

/* Variables visible to the protocol-specific routines.  */

/* Protocol structure.  */
const struct sprotocol *qProto;

/* Buffer to hold received data.  */
char abPrecbuf[CRECBUFLEN];

/* Index of start of data in abPrecbuf.  */
int iPrecstart;

/* Index of end of data (first byte not included in data) in abPrecbuf.  */
int iPrecend;

/* Whether an unexpected shutdown is OK now; this is used to avoid
   giving a warning for systems that hang up in a hurry.  */
boolean fPerror_ok;

/* Amount of data sent for current file.  */
static long cPsent_bytes;

/* Amount of data received for current file.  */
static long cPreceived_bytes;

/* Whether an error has been reported for current file being received.  */
static boolean fPreceived_error;

/* Send a file.  If we are the master, we must send a command to
   transfer the file and wait for a confirmation that we can begin
   sending the file.  If we are the slave, the master has sent us a
   command and is waiting for a reply; we must confirm that we will
   send the file.  Either way, we begin transferring data.

   This function returns FALSE if there is a communication failure.
   It returns TRUE otherwise, even if the file transfer failed.  */

boolean
fsend_file (fmaster, e, qcmd, zmail, ztosys, fnew)
     boolean fmaster;
     openfile_t e;
     const struct scmd *qcmd;
     const char *zmail;
     const char *ztosys;
     boolean fnew;
{
  if (fmaster)
    {
      int clen;
      char *zsend;
      const char *zrec;
      
      /* Send the string
	 S zfrom zto zuser zoptions ztemp imode znotify
	 to the remote system.  We put a '-' in front of the (possibly
	 empty) options and a '0' in front of the mode.  The remote
	 system will ignore ztemp, but it is supposed to be sent anyhow.
	 If fnew is TRUE, we also send the size; in this case if ztemp
	 is empty we must send it as "".  */
      clen = (strlen (qcmd->zfrom) + strlen (qcmd->zto)
	      + strlen (qcmd->zuser) + strlen (qcmd->zoptions)
	      + strlen (qcmd->ztemp) + strlen (qcmd->znotify)
	      + 50);
      zsend = (char *) alloca (clen);
      if (! fnew)
	sprintf (zsend, "S %s %s %s -%s %s 0%o %s", qcmd->zfrom, qcmd->zto,
		 qcmd->zuser, qcmd->zoptions, qcmd->ztemp, qcmd->imode,
		 qcmd->znotify);
      else
	{
	  const char *znotify;

	  if (qcmd->znotify[0] != '\0')
	    znotify = qcmd->znotify;
	  else
	    znotify = "\"\"";
	  sprintf (zsend, "S %s %s %s -%s %s 0%o %s %ld", qcmd->zfrom,
		   qcmd->zto, qcmd->zuser, qcmd->zoptions, qcmd->ztemp,
		   qcmd->imode, znotify, qcmd->cbytes);
	}

      if (! (qProto->pfsendcmd) (zsend))
	{
	  (void) ffileclose (e);
	  return FALSE;
	}

      /* Now we must await a reply.  */

      zrec = zpget_cmd ();
      if (zrec == NULL)
	{
	  (void) ffileclose (e);
	  return FALSE;
	}

      if (zrec[0] != 'S'
	  || (zrec[1] != 'Y' && zrec[1] != 'N'))
	{
	  ulog (LOG_ERROR, "Bad response to send request");
	  (void) ffileclose (e);
	  return FALSE;
	}

      if (zrec[1] == 'N')
	{
	  const char *zerr;

	  if (zrec[2] == '2')
	    zerr = "permission denied";
	  else if (zrec[2] == '4')
	    {
	      /* This means the remote system cannot create
		 work files; log the error and try again later.  */
	      ulog (LOG_ERROR,
		    "Can't send %s: remote cannot create work files",
		    qcmd->zfrom);
	      (void) ffileclose (e);
	      return TRUE;
	    }
	  else if (zrec[2] == '6')
	    {
	      /* The remote system says the file is too large.  It
		 would be better if we could determine whether it will
		 always be too large.  */
	      ulog (LOG_ERROR, "%s is too big to send now",
		    qcmd->zfrom);
	      (void) ffileclose (e);
	      return TRUE;
	    }
	  else
	    zerr = "unknown reason";

	  ulog (LOG_ERROR, "Can't send %s: %s", qcmd->zfrom, zerr);
	  (void) fsysdep_did_work (qcmd->pseq);
	  (void) ffileclose (e);
	  return TRUE;
	}
    }
  else
    {
      char absend[20];

      /* We are the slave; confirm that we will send the file.  We
	 send the file mode in the confirmation string.  */

      sprintf (absend, "RY 0%o", qcmd->imode);

      if (! (qProto->pfsendcmd) (absend))
	{
	  (void) ffileclose (e);
	  return FALSE;
	}
    }

  /* Record the file we are sending, and let the protocol take over.  */

  if (! fstore_sendfile (e, qcmd->pseq, qcmd->zfrom, qcmd->zto, ztosys,
			 qcmd->zuser, zmail))
    return FALSE;

  cPsent_bytes = 0;

  return fploop ();
}

/* Confirm that a file has been received correctly by the other side.
   Return FALSE for a communication error.  We expect the receiving
   system to send back CY; if an error occurred while moving the
   received file into its final location, the receiving system will
   send back CN5.  */

static boolean
fpsendfile_confirm ()
{
  const char *zrec;

  zrec = zpget_cmd ();
  if (zrec == NULL)
    return FALSE;

  if (zrec[0] != 'C'
      || (zrec[1] != 'Y' && zrec[1] != 'N'))
    {
      ulog (LOG_ERROR, "Bad confirmation for sent file");
      (void) fsent_file (FALSE, cPsent_bytes);
    }
  else if (zrec[1] == 'N')
    {
      if (zrec[2] == '5')
	ulog (LOG_ERROR, "File could not be stored in final location");
      else
	ulog (LOG_ERROR, "File send failed for unknown reason");
      (void) fsent_file (FALSE, cPsent_bytes);
    }
  else
    (void) fsent_file (TRUE, cPsent_bytes);

  return TRUE;
}

/* Receive a file.  If we are the master, we must set up a file
   request and wait for the other side to confirm it.  If we are the
   slave, we must confirm a request made by the other side.  We then
   start receiving the file.

   This function must return FALSE if there is a communication error
   and TRUE otherwise.  We return TRUE even if the file transfer
   fails.  */

boolean
freceive_file (fmaster, e, qcmd, zmail, zfromsys, fnew)
     boolean fmaster;
     openfile_t e;
     const struct scmd *qcmd;
     const char *zmail;
     const char *zfromsys;
     boolean fnew;
{
  unsigned int imode;

  if (fmaster)
    {
      int clen;
      char *zsend;
      const char *zrec;

      /* We send the string
	 R from to user options
	 We put a dash in front of options.  If we are talking to a
	 counterpart, we also send the maximum size file we are
	 prepared to accept, as returned by esysdep_open_receive.  */
      
      clen = (strlen (qcmd->zfrom) + strlen (qcmd->zto)
	      + strlen (qcmd->zuser) + strlen (qcmd->zoptions) + 30);
      zsend = (char *) alloca (clen);

      if (! fnew)
	sprintf (zsend, "R %s %s %s -%s", qcmd->zfrom, qcmd->zto,
		 qcmd->zuser, qcmd->zoptions);
      else
	sprintf (zsend, "R %s %s %s -%s %ld", qcmd->zfrom, qcmd->zto,
		 qcmd->zuser, qcmd->zoptions, qcmd->cbytes);

      if (! (qProto->pfsendcmd) (zsend))
	{
	  (void) ffileclose (e);
	  return FALSE;
	}

      /* Wait for a reply.  */

      zrec = zpget_cmd ();
      if (zrec == NULL)
	{
	  (void) ffileclose (e);
	  return FALSE;
	}

      if (zrec[0] != 'R'
	  || (zrec[1] != 'Y' && zrec[1] != 'N'))
	{
	  ulog (LOG_ERROR, "Bad response to receive request");
	  (void) ffileclose (e);
	  return FALSE;
	}

      if (zrec[1] == 'N')
	{
	  const char *zerr;

	  if (zrec[2] == '2')
	    zerr = "no such file";
	  else if (zrec[2] == '6')
	    {
	      /* We sent over the maximum file size we were prepared
		 to receive, and the remote system is telling us that
		 the file is larger than that.  Try again later.  It
		 would be better if we could know whether there will
		 ever be enough room.  */
	      ulog (LOG_ERROR, "%s is too big to receive",
		    qcmd->zfrom);
	      (void) ffileclose (e);
	      return TRUE;
	    }
	  else
	    zerr = "unknown reason";
	  ulog (LOG_ERROR, "Can't receive %s: %s", qcmd->zfrom, zerr);
	  (void) fsysdep_did_work (qcmd->pseq);
	  (void) ffileclose (e);
	  return TRUE;
	}
      
      /* The mode should have been sent as "RY 0%o".  If it wasn't,
	 we use 0666.  */
      imode = (unsigned int) strtol (zrec + 2, (char **) NULL, 8);
      if (imode == 0)
	imode = 0666;
    }
  else
    {
      /* Tell the other system to go ahead and send.  */

      if (! (qProto->pfsendcmd) ("SY"))
	{
	  (void) ffileclose (e);
	  return FALSE;
	}
      imode = qcmd->imode;
    }

  if (! fstore_recfile (e, qcmd->pseq, qcmd->zfrom, qcmd->zto, zfromsys,
			qcmd->zuser, imode, zmail, qcmd->ztemp))
    return FALSE;

  cPreceived_bytes = 0;
  fPreceived_error = FALSE;

  return fploop ();
}

/* Confirm that a file was received correctly.  */

static boolean
fprecfile_confirm ()
{
  if (freceived_file (TRUE, cPreceived_bytes))
    return (qProto->pfsendcmd) ("CY");
  else
    return (qProto->pfsendcmd) ("CN5");
}

/* Send a transfer request.  This is only called by the master.  It
   ignored the pseq entry in the scmd structure.  */

boolean
fxcmd (qcmd)
     const struct scmd *qcmd;
{
  int clen;
  char *zsend;
  const char *zrec;

  /* We send the string
     X from to user options
     We put a dash in front of options.  */
      
  clen = (strlen (qcmd->zfrom) + strlen (qcmd->zto)
	  + strlen (qcmd->zuser) + strlen (qcmd->zoptions) + 7);
  zsend = (char *) alloca (clen);

  sprintf (zsend, "X %s %s %s -%s", qcmd->zfrom, qcmd->zto,
	   qcmd->zuser, qcmd->zoptions);

  if (! (qProto->pfsendcmd) (zsend))
    return FALSE;

  /* Wait for a reply.  */

  zrec = zpget_cmd ();
  if (zrec == NULL)
    return FALSE;

  if (zrec[0] != 'X'
      || (zrec[1] != 'Y' && zrec[1] != 'N'))
    {
      ulog (LOG_ERROR, "Bad response to wildcard request");
      return FALSE;
    }

  if (zrec[1] == 'N')
    {
      ulog (LOG_ERROR, "Work request denied");
      return TRUE;
    }
  
  return TRUE;
}

/* Confirm a transfer request.  */

boolean
fxcmd_confirm ()
{
  return (qProto->pfsendcmd) ("XY");
}

/* Signal a file transfer failure to the other side.  This is only called
   by the slave.  */

boolean
ftransfer_fail (bcmd, twhy)
     int bcmd;
     enum tfailure twhy;
{
  const char *z;

  switch (bcmd)
    {
    case 'S':
      switch (twhy)
	{
	case FAILURE_PERM:
	  z = "SN2";
	  break;
	case FAILURE_OPEN:
	  z = "SN4";
	  break;
	case FAILURE_SIZE:
	  z = "SN6";
	  break;
	default:
	  z = "SN";
	  break;
	}
      break;
    case 'R':
      switch (twhy)
	{
	case FAILURE_PERM:
	case FAILURE_OPEN:
	  z = "RN2";
	  break;
	case FAILURE_SIZE:
	  z = "RN6";
	  break;
	default:
	  z = "RN";
	  break;
	}
      break;
    case 'X':
      z = "XN";
      break;
    default:
#if DEBUG > 0
      ulog (LOG_ERROR, "ftransfer_fail: Can't happen");
#endif
      return FALSE;
    }
  
  return (qProto->pfsendcmd) (z);
}

/* Get and parse a command from the other system.  Handle hangups
   specially.  */

boolean
fgetcmd (fmaster, qcmd)
     boolean fmaster;
     struct scmd *qcmd;
{
  static char *z;
  static int c;

  while (TRUE)
    {
      const char *zcmd;
      int clen;

      zcmd = zpget_cmd ();
      if (zcmd == NULL)
	return FALSE;

      clen = strlen (zcmd);
      if (clen + 1 > c)
	{
	  c = clen + 1;
	  z = (char *) xrealloc ((pointer) z, c);
	}
      strcpy (z, zcmd);

      if (! fparse_cmd (z, qcmd))
	continue;

      /* Handle hangup commands specially.  If it's just 'H', return
	 it.  If it's 'N', the other side is denying a hangup request
	 which we can just ignore (since the top level code assumes
	 that hangup requests are denied).  If it's 'Y', the other
	 side is confirming a hangup request.  In this case we confirm
	 with an "HY", wait for yet another "HY" from the other side,
	 and then finally shut down the protocol (I don't know why it
	 works this way, but it does).  We then return a 'Y' command
	 to the top level code.  */

      if (qcmd->bcmd == 'N')
	{
#if DEBUG > 0
	  if (fmaster)
	    ulog (LOG_ERROR, "Got hangup reply as master");
#endif
	  continue;
	}

      if (qcmd->bcmd == 'Y')
	{
#if DEBUG > 0
	  if (fmaster)
	    ulog (LOG_ERROR, "Got hangup reply as master");
#endif
	  /* Don't check errors rigorously here, since the other side
	     might jump the gun and hang up.  */

	  if (! (qProto->pfsendcmd) ("HY"))
	    return TRUE;
	  fPerror_ok = TRUE;
	  zcmd = zpget_cmd ();
	  fPerror_ok = FALSE;
	  if (zcmd == NULL)
	    return TRUE;
	  if (strcmp (zcmd, "HY") != 0)
	    ulog (LOG_ERROR, "Got \"%s\" when expecting \"HY\"",
		  zcmd);
	  (void) (qProto->pfshutdown) ();
	  return TRUE;
	}

      return TRUE;
    }

  /*NOTREACHED*/
}

/* Hangup.  */

boolean
fhangup_request ()
{
  return (qProto->pfsendcmd) ("H");
}

/* Reply to a hangup request.  This is only called by the slave.  If
   fconfirm is TRUE, we are closing down the protocol.  We send an HY
   message.  The master responds with an HY message.  We send another
   HY message, and then shut down the protocol.  */

boolean
fhangup_reply (fconfirm)
     boolean fconfirm;
{
  if (! fconfirm)
    return (qProto->pfsendcmd) ("HN");
  else
    {
      const char *z;

      if (! (qProto->pfsendcmd) ("HY"))
	return FALSE;

      z = zpget_cmd ();
      if (z == NULL)
	return FALSE;
      if (strcmp (z, "HY") != 0)
	ulog (LOG_ERROR, "Got \"%s\" when expecting \"HY\"", z);
      else
	{
	  if (! (qProto->pfsendcmd) ("HY"))
	    return FALSE;
	}

      return (qProto->pfshutdown) ();
    }
}

/* Loop sending and/or receiving data.  If there is a file to send,
   this will send it until the entire file has been sent or a command
   has been received from the remote system or a complete file has
   been received from the remote system.  Otherwise this will simply
   call the protocol to wait until a complete file or command has been
   received from the remote system.  */

static boolean
fploop ()
{
  boolean fexit;

#if DEBUG > 7
  if (iDebug > 7)
    ulog (LOG_DEBUG, "fploop: Main protocol loop");
#endif

  if (ffileisopen (eSendfile))
    {
      int iend;

      iend = iPrecend;

      while (TRUE)
	{
	  /* We keep sending out packets until we have something
	     in the receive buffer.  */
	  while (iend == iPrecend)
	    {
	      char *zdata;
	      int cdata;

	      /* Get a packet and fill it with data.  */

	      zdata = (qProto->pzgetspace) (&cdata);
	      if (zdata == NULL)
		return FALSE;

	      cdata = cfileread (eSendfile, zdata, cdata);
	      if (ffilereaderror (eSendfile, cdata))
		{
		  /* The protocol gives us no way to report a file
		     sending error, so we just drop the connection.
		     What else can we do?  */
		  ulog (LOG_ERROR, "read: %s", strerror (errno));
		  usendfile_error ();
		  return FALSE;
		}

	      if (! (qProto->pfsenddata) (zdata, cdata))
		return FALSE;

	      cPsent_bytes += cdata;

	      /* If we have reached the end of the file, wait for
		 confirmation and return out to get the next file.  */
	      if (cdata == 0)
		return fpsendfile_confirm ();
	    }

	  /* Process the data in the receive buffer, and decide
	     whether it's time to get out.  */
	  if (! (qProto->pfprocess) (&fexit))
	    return FALSE;
	  if (fexit)
	    return TRUE;

	  iend = iPrecend;
	}
    }

#if DEBUG > 0
  /* If there is no file to send, there really should be a file to
     receive.  */

  if (! ffileisopen(eRecfile))
    ulog (LOG_FATAL, "fploop: No send or receive file");
#endif

  /* We have no file to send.  Wait for data to come in.  */

  return (qProto->pfwait) ();
}

/* This function is called by the protocol routines when data has
   arrived.  Some protocols may know whether the data is for a command
   or a file; for others, if a receive file is open it is for the file
   and is otherwise for a command.  This function will set *pfexit to
   TRUE if it has received a complete file (assumed to be true if
   cdata is zero) or a complete command (assumed to be true if the
   argument data contains a null byte).  It will return FALSE on
   error.  */

boolean 
fgot_data (zdata, cdata, fcmd, ffile, pfexit)
     const char *zdata;
     int cdata;
     boolean fcmd;
     boolean ffile;
     boolean *pfexit;
{
  *pfexit = FALSE;

  if (! fcmd && ! ffile)
    {
      if (ffileisopen (eRecfile))
	ffile = TRUE;
      else
	fcmd = TRUE;
    }

#if DEBUG > 0
  if (ffile && ! ffileisopen (eRecfile))
    ulog (LOG_FATAL, "fgot_data: No file to receive into");
#endif

  if (ffile)
    {
      if (cdata == 0)
	{
	  if (! fprecfile_confirm ())
	    return FALSE;
	  *pfexit = TRUE;
	  return TRUE;
	}
      else
	{
	  int cwrote;

	  /* Cast zdata to avoid warnings because of erroneous
	     prototypes on Ultrix.  */
	  cwrote = cfilewrite (eRecfile, (char *) zdata, cdata);
	  if (cwrote != cdata && ! fPreceived_error)
	    {
	      if (cwrote < 0)
		ulog (LOG_ERROR, "write: %s", strerror (errno));
	      else
		ulog (LOG_ERROR, "write of %d wrote only %d",
		      cdata, cwrote);
	      urecfile_error ();
	      fPreceived_error = TRUE;
	    }

	  cPreceived_bytes += cdata;

	  return TRUE;
	}
    }
  else
    {
      const char *z;

      /* We want to add this data to the current command string.  If
	 there is no null character in the data, this string will be
	 continued by the next packet.  Otherwise this must be the
	 last string in the command, and we don't care about what
	 comes after the null byte.  */

      z = (const char *) memchr ((constpointer) zdata, '\0', cdata);
      if (z == NULL)
	upadd_cmd (zdata, cdata, FALSE);
      else
	{
	  upadd_cmd (zdata, z - zdata, TRUE);
	  *pfexit = TRUE;
	}

      return TRUE;
    }
}

/* This function is called by fgot_data when a command string is
   received.  We must queue up received commands since we don't know
   when we'll be able to get to them (for example, the
   acknowledgements for the last few packets of a sent file may
   contain the string indicating whether the file was received
   correctly).  */

struct spcmdqueue
{
  struct spcmdqueue *qnext;
  int csize;
  int clen;
  char *z;
};

static struct spcmdqueue *qPcmd_queue;
static struct spcmdqueue *qPcmd_free;

static void
upadd_cmd (z, clen, flast)
     const char *z;
     int clen;
     boolean flast;
{
  struct spcmdqueue *q;

  q = qPcmd_free;
  if (q == NULL)
    {
      q = (struct spcmdqueue *) xmalloc (sizeof (struct spcmdqueue));
      q->qnext = NULL;
      q->csize = 0;
      q->clen = 0;
      q->z = NULL;
      qPcmd_free = q;
    }

  if (q->clen + clen + 1 > q->csize)
    {
      q->csize = q->clen + clen + 1;
      q->z = (char *) xrealloc ((pointer) q->z, q->csize);
    }

  memcpy (q->z + q->clen, z, clen);
  q->clen += clen;
  q->z[q->clen] = '\0';

  /* If the last string in this command, add it to the queue of
     finished commands.  */

  if (flast)
    {
      struct spcmdqueue **pq;

      for (pq = &qPcmd_queue; *pq != NULL; pq = &(*pq)->qnext)
	;
      *pq = q;
      qPcmd_free = q->qnext;
      q->qnext = NULL;
    }
}

/* Get a command string.  We just have to wait until the receive
   packet function gives us something in qPcmd_queue.  The return
   value of this may be treated as a static buffer; it will last
   at least until the next packet is received.  */

static const char *
zpget_cmd ()
{
  struct spcmdqueue *q;

  /* Wait until a command comes in.  */
  while (qPcmd_queue == NULL)
    {
#if DEBUG > 4
      if (iDebug > 4)
	ulog (LOG_DEBUG, "zpget_cmd: Waiting for packet");
#endif

      if (! (qProto->pfwait) ())
	return NULL;
    }

  q = qPcmd_queue;
  qPcmd_queue = q->qnext;

  q->clen = 0;

  /* We must not replace qPcmd_free, because it may already be
     receiving a new command string.  */
  if (qPcmd_free == NULL)
    {
      q->qnext = NULL;
      qPcmd_free = q;
    }
  else
    {
      q->qnext = qPcmd_free->qnext;
      qPcmd_free->qnext = q;
    }

  return q->z;
}

/* We want to output and input at the same time, if supported on this
   machine.  If we have something to send, we send it all while
   accepting a large amount of data.  Once we have sent everything we
   look at whatever we have received.  If data comes in faster than we
   can send it, we may run out of buffer space.  */

boolean
fsend_data (zsend, csend)
     const char *zsend;
     int csend;
{
  while (csend > 0)
    {
      char *zrec;
      int crec, csent;

      if (iPrecend < iPrecstart)
	{
	  zrec = abPrecbuf + iPrecend;
	  crec = iPrecstart - iPrecend - 1;
	}
      else if (iPrecend < CRECBUFLEN)
	{
	  zrec = abPrecbuf + iPrecend;
	  crec = CRECBUFLEN - iPrecend;
	}
      else
	{
	  zrec = abPrecbuf;
	  crec = iPrecstart - 1;
	}

      csent = csend;

      if (! fport_io (zsend, &csent, zrec, &crec))
	return FALSE;

      csend -= csent;
      zsend += csent;

      iPrecend = (iPrecend + crec) % CRECBUFLEN;
    }

  return TRUE;
}

/* Read data from the other system when we have nothing to send.  The
   argument cneed is the amount of data the caller wants, and ctimeout
   is the timeout in seconds.  The function sets *pcrec to the amount
   of data which was actually received, which may be less than cneed
   if there isn't enough room in the recieve buffer.  If no data is
   received before the timeout expires, *pcrec will be returned as 0.
   If an error occurs, the function returns FALSE.  */

boolean
freceive_data (cneed, pcrec, ctimeout)
     int cneed;
     int *pcrec;
     int ctimeout;
{
  char *zrec;

  if (iPrecend < iPrecstart)
    {
      zrec = abPrecbuf + iPrecend;
      *pcrec = iPrecstart - iPrecend - 1;
    }
  else if (iPrecend < CRECBUFLEN)
    {
      zrec = abPrecbuf + iPrecend;
      *pcrec = CRECBUFLEN - iPrecend;
    }
  else
    {
      zrec = abPrecbuf;
      *pcrec = iPrecstart - 1;
    }
  
  if (*pcrec < cneed)
    cneed = *pcrec;

  if (! fport_read (zrec, pcrec, cneed, ctimeout, TRUE))
    return FALSE;

  iPrecend = (iPrecend + *pcrec) % CRECBUFLEN;

  return TRUE;
}