/* proti.c
   The 'i' protocol.

   Copyright (C) 1992 Ian Lance Taylor

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
   c/o Infinity Development Systems, P.O. Box 520, Waltham, MA 02254.
   */

#include "uucp.h"

#if USE_RCS_ID
const char proti_rcsid[] = "$Id$";
#endif

#include <ctype.h>
#include <errno.h>

#include "conn.h"
#include "trans.h"
#include "system.h"
#include "prot.h"

/* The 'i' protocol is a simple sliding window protocol, created by
   me.  It is in many ways similar to the 'g' protocol.  Several ideas
   are also taken from the paper ``A High-Throughput Message Transport
   System'' by P. Lauder.  I don't know where the paper was published,
   but the author's e-mail address is piers@cs.su.oz.au.  However, I
   haven't adopted his main idea, which is to dispense with windows
   entirely.  This is because some links still do require flow control
   and, more importantly, because I want to have a limit to the amount
   of data I must be able to resend upon request.  To reduce the costs
   of window acknowledgements, I use a large window and only require
   an ack at the halfway point.

   Each packet starts with a header containing the following
   information:

   Intro byte           8 bits          byte 1
   Packet number        5 bits          byte 2
   Local channel        3 bits
   Packet ack           5 bits          byte 3
   Remote channel       3 bits
   Packet type          3 bits          bytes 4-5
   Direction            1 bit
   Data length         12 bits
   Header check         8 bits          byte 6

   If the data length is not 0, this is followed by the data and a 32
   bit CRC checksum.

   The following packet types are defined:

   SYNC  Initialize the connection
   DATA  Data packet
   ACK   Simple acknowledgement with no data
   NAK   Negative acknowledgement; requests resend of single packet
   SPOS  Set file position
   CLOSE Close the connection
   */

/* The offsets of the bytes in the packet header.  */

#define IHDR_INTRO (0)
#define IHDR_LOCAL (1)
#define IHDR_REMOTE (2)
#define IHDR_CONTENTS1 (3)
#define IHDR_CONTENTS2 (4)
#define IHDR_CHECK (5)

/* Macros to set and extract values of IHDR_LOCAL and IHDR_REMOTE.  */
#define IHDRWIN_SET(iseq, ichan) (((iseq) << 3) | (ichan))
#define IHDRWIN_GETSEQ(ival) (((ival) >> 3) & 0x1f)
#define IHDRWIN_GETCHAN(ival) ((ival) & 0x07)

/* Macros to set and extract values of IHDR_CONTENTS fields.  */
#define IHDRCON_SET1(ttype, fcaller, cbytes) \
  (((ttype) << 5) | ((fcaller) ? (1 << 4) : 0) | (((cbytes) >> 8) & 0x0f))
#define IHDRCON_SET2(ttype, fcaller, cbytes) ((cbytes) & 0xff)
#define THDRCON_GETTYPE(i1, i2) (((i1) >> 5) & 0x07)
#define FHDRCON_GETCALLER(i1, i2) (((i1) & (1 << 4)) != 0)
#define CHDRCON_GETBYTES(i1, i2) ((((i1) & 0x0f) << 8) | ((i2) & 0xff))

/* Macros for the IHDR_CHECK field.  */
#define IHDRCHECK_VAL(zhdr) \
  ((zhdr[IHDR_LOCAL] \
    ^ zhdr[IHDR_REMOTE] \
    ^ zhdr[IHDR_CONTENTS1] \
    ^ zhdr[IHDR_CONTENTS2]) \
   & 0xff)

/* Length of the packet header.  */
#define CHDRLEN (6)

/* Amount of space to skip between start of packet and actual data.
   This is used to make the actual data longword aligned, to encourage
   good performance when copying data into the buffer.  */
#define CHDRSKIPLEN (CHDRLEN + (sizeof (long) - CHDRLEN % sizeof (long)))

/* Amount of space to skip between memory buffer and header.  */
#define CHDROFFSET (CHDRSKIPLEN - CHDRLEN)

/* Length of the trailing checksum.  */
#define CCKSUMLEN (4)

/* Macros to set and get the checksum.  These multiply evaluate their
   arguments.  */
#define ICKSUM_GET(z) \
  ((((((((unsigned long) ((z)[0] & 0xff)) << 8) \
       | (unsigned long) ((z)[1] & 0xff)) << 8) \
     | (unsigned long) ((z)[2] & 0xff)) << 8) \
   | (unsigned long) ((z)[3] & 0xff))
#define UCKSUM_SET(z, i) \
  (void) ((z)[0] = (((i) >> 24) & 0xff), \
	  (z)[1] = (((i) >> 16) & 0xff), \
	  (z)[2] = (((i) >> 8) & 0xff), \
	  (z)[3] = ((i) & 0xff))

/* The header introduction character.  */
#define IINTRO ('\007')

/* The packet types.  */

#define DATA (0)
#define SYNC (1)
#define ACK (2)
#define NAK (3)
#define SPOS (4)
#define CLOSE (5)

/* Largest possible packet size (plus 1).  */
#define IMAXPACKSIZE (1 << 12)

/* Largest possible sequence number (plus 1).  */
#define IMAXSEQ 32

/* Get the next sequence number given a sequence number.  */
#define INEXTSEQ(i) ((i + 1) & (IMAXSEQ - 1))

/* Compute i1 - i2 in sequence space (i.e., the number of packets from
   i2 to i1).  */
#define CSEQDIFF(i1, i2) (((i1) + IMAXSEQ - (i2)) & (IMAXSEQ - 1))

/* Largest possible channel number (plus 1).  */
#define IMAXICHAN (8)

/* Default packet size to request (protocol parameter
   ``packet-size'').  */
#define IREQUEST_PACKSIZE (1024)

/* Default window size to request (protocol parameter ``window'').  */
#define IREQUEST_WINSIZE (16)

/* Default timeout to use when sending the SYNC packet (protocol
   parameter ``sync-timeout'').  */
#define CSYNC_TIMEOUT (10)

/* Default number of times to retry sending the SYNC packet (protocol
   parameter ``sync-retries'').  */
#define CSYNC_RETRIES (6)

/* Default timeout to use when waiting for a packet (protocol
   parameter ``timeout'').  */
#define CTIMEOUT (10)

/* Default number of times to retry sending a packet before giving up
   (protocol parameter ``retries'').  */
#define CRETRIES (6)

/* Default maximum level of errors to accept before giving up
   (protocol parameter ``errors'').  */
#define CERRORS (100)

/* Default decay rate.  Each time we receive this many packets
   succesfully, we decrement the error level by one (protocol
   parameter ``error-decay'').  */
#define CERROR_DECAY (10)

/* Local variables.  */

/* Packet size to request (protocol parameter ``packet-size'').  */
static int iIrequest_packsize = IREQUEST_PACKSIZE;

/* Window size to request (protocol parameter ``window'').  */
static int iIrequest_winsize = IREQUEST_WINSIZE;

/* Remote packet size (set from SYNC packet or from
   iIforced_remote_packsize).  */
static int iIremote_packsize;

/* Forced remote packet size, used if non-zero (protocol parameter
   ``remote-packet-size'').  */
static int iIforced_remote_packsize = 0;

/* Remote window size (set from SYNC packet or from
   iIforced_remote_winsize).  */
static int iIremote_winsize;

/* Forced remote window size, used if non-zero (protocol parameter
   ``remote-window'').  */
static int iIforced_remote_winsize = 0;

/* Timeout to use when sending the SYNC packet (protocol
   parameter ``sync-timeout'').  */
static int cIsync_timeout = CSYNC_TIMEOUT;

/* Number of times to retry sending the SYNC packet (protocol
   parameter ``sync-retries'').  */
static int cIsync_retries = CSYNC_RETRIES;

/* Timeout to use when waiting for a packet (protocol parameter
   ``timeout'').  */
static int cItimeout = CTIMEOUT;

/* Number of times to retry sending a packet before giving up
   (protocol parameter ``retries'').  */
static int cIretries = CRETRIES;

/* Maximum level of errors to accept before giving up (protocol
   parameter ``errors'').  */
static int cIerrors = CERRORS;

/* Each time we receive this many packets succesfully, we decrement
   the error level by one (protocol parameter ``error-decay'').  */
static int cIerror_decay = CERROR_DECAY;

/* Next sequence number to send.  */
static int iIsendseq;

/* Last sequence number received.  */
static int iIrecseq;

/* Last sequence number we have acknowledged.  */
static int iIlocal_ack;

/* Last sequence number remote system has acknowledged.  */
static int iIremote_ack;

/* File position we are sending from.  */
static int iIsendpos;

/* File position we are receiving to.  */
static int iIrecpos;

/* TRUE if closing the connection.  */
static boolean fIclosing;

/* Array of sent packets indexed by packet number.  */
static char *azIsendbuffers[IMAXSEQ];

/* Array of received packets that we aren't ready to process yet,
   indexed by packet number.  */
static char *azIrecbuffers[IMAXSEQ];

/* For each packet sequence number, record whether we sent a NAK for
   the packet.  */
static boolean afInaked[IMAXSEQ];

/* Number of SYNC packets received (used only to detect whether one
   was received).  */
static int cIsyncs;

/* Number of packets sent.  */
static long cIsent_packets;

/* Number of packets received.  */
static long cIreceived_packets;

/* Number of packets resent.  */
static long cIresent_packets;

/* Number of bad packet headers received.  */
static long cIbad_hdr;

/* Number of out of order packets received.  */
static long cIbad_order;

/* Number of bad checksums received.  */
static long cIbad_cksum;

/* Number of packets rejected by remote system.  */
static long cIremote_rejects;

/* Protocol parameter commands.  */

struct uuconf_cmdtab asIproto_params[] =
{
  { "packet-size", UUCONF_CMDTABTYPE_INT, (pointer) &iIrequest_packsize,
      NULL },
  { "window", UUCONF_CMDTABTYPE_INT, (pointer) &iIrequest_winsize, NULL },
  { "remote-packet-size", UUCONF_CMDTABTYPE_INT,
      (pointer) &iIforced_remote_packsize, NULL },
  { "remote-window", UUCONF_CMDTABTYPE_INT,
      (pointer) &iIforced_remote_winsize, NULL },
  { "sync-timeout", UUCONF_CMDTABTYPE_INT, (pointer) &cIsync_timeout,
      NULL },
  { "sync-retries", UUCONF_CMDTABTYPE_INT, (pointer) &cIsync_retries,
      NULL },
  { "timeout", UUCONF_CMDTABTYPE_INT, (pointer) &cItimeout, NULL },
  { "retries", UUCONF_CMDTABTYPE_INT, (pointer) &cIretries, NULL },
  { "errors", UUCONF_CMDTABTYPE_INT, (pointer) &cIerrors, NULL },
  { "error-decay", UUCONF_CMDTABTYPE_INT, (pointer) &cIerror_decay, NULL },
  { NULL, 0, NULL, NULL }
};

/* Local functions.  */

static boolean finak P((struct sdaemon *qdaemon, int iseq));
static boolean fiwait_for_packet P((struct sdaemon *qdaemon,
				    int ctimeout, int cretries,
				    boolean *ftimedout));
static boolean ficheck_errors P((void));
static boolean fiprocess_data P((struct sdaemon *qdaemon,
				 boolean *pfexit, boolean *pffound,
				 size_t *pcneed));
static boolean fiprocess_packet P((struct sdaemon *qdaemon,
				   const char *zhdr,
				   const char *zfirst, int cfirst,
				   const char *zsecond, int csecond,
				   boolean *pfexit));

/* Start the protocol.  We keep transmitting a SYNC packet containing
   the window and packet size we would like to receive until we
   receive a SYNC packet from the remote system.  The first two bytes
   of the data contents of a SYNC packet are the maximum packet size
   we want to receive (high byte, low byte), and the next byte is the
   maximum window size we want to use.  */

boolean
fistart (qdaemon)
     struct sdaemon *qdaemon;
{
  char ab[CHDRLEN + 3 + CCKSUMLEN];
  unsigned long icksum;
  int ctries;
  int csyncs;

  if (iIforced_remote_packsize <= 0
      || iIforced_remote_packsize >= IMAXPACKSIZE)
    iIforced_remote_packsize = 0;
  else
    iIremote_packsize = iIforced_remote_packsize;
  if (iIforced_remote_winsize <= 0 || iIforced_remote_winsize >= IMAXSEQ)
    iIforced_remote_winsize = 0;
  else
    iIremote_winsize = iIforced_remote_winsize;

  iIsendseq = 1;
  iIrecseq = 0;
  iIlocal_ack = 0;
  iIremote_ack = 0;
  iIsendpos = 0;
  iIrecpos = 0;
  fIclosing = FALSE;

  cIsent_packets = 0;
  cIreceived_packets = 0;
  cIresent_packets = 0;
  cIbad_hdr = 0;
  cIbad_order = 0;
  cIbad_cksum = 0;
  cIremote_rejects = 0;

  ab[IHDR_INTRO] = IINTRO;
  ab[IHDR_LOCAL] = ab[IHDR_REMOTE] = IHDRWIN_SET (0, 0);
  ab[IHDR_CONTENTS1] = IHDRCON_SET1 (SYNC, qdaemon->fcaller, 3);
  ab[IHDR_CONTENTS2] = IHDRCON_SET2 (SYNC, qdaemon->fcaller, 3);
  ab[IHDR_CHECK] = IHDRCHECK_VAL (ab);
  ab[CHDRLEN + 0] = (iIrequest_packsize >> 8) & 0xff;
  ab[CHDRLEN + 1] = iIrequest_packsize & 0xff;
  ab[CHDRLEN + 2] = iIrequest_winsize;
  icksum = icrc (ab + CHDRLEN, 3, ICRCINIT);
  UCKSUM_SET (ab + CHDRLEN + 3, icksum);

  /* The static cIsyncs is incremented each time a SYNC packet is
     received.  */
  csyncs = cIsyncs;
  ctries = 0;

  while (TRUE)
    {
      boolean ftimedout;

      DEBUG_MESSAGE2 (DEBUG_PROTO,
		      "fistart: Sending SYNC packsize %d winsize %d",
		      iIrequest_packsize, iIrequest_winsize);

      if (! fsend_data (qdaemon->qconn, ab, CHDRLEN + 3 + CCKSUMLEN,
			TRUE))
	return FALSE;

      if (fiwait_for_packet (qdaemon, cIsync_timeout, 0, &ftimedout))
	{
	  if (csyncs != cIsyncs)
	    break;
	}
      else
	{
	  if (! ftimedout)
	    return FALSE;

	  ++ctries;
	  if (ctries > cIsync_retries)
	    {
	      ulog (LOG_ERROR, "Protocol startup failed");
	      return FALSE;
	    }
	}
    }

  /* We got a SYNC packet; set up packet buffers to use.  */
  do
    {
      int iseq;

      for (iseq = 0; iseq < IMAXSEQ; iseq++)
	{
	  azIrecbuffers[iseq] = NULL;
	  afInaked[iseq] = FALSE;
	  azIsendbuffers[iseq] = (char *) malloc (iIremote_packsize
						  + CHDRSKIPLEN
						  + CCKSUMLEN);
	  if (azIsendbuffers[iseq] == NULL)
	    {
	      int ifree;

	      for (ifree = 0; ifree < iseq; ifree++)
		free ((pointer) azIsendbuffers[ifree]);
	      break;
	    }
	}

      if (iseq >= IMAXSEQ)
	{
	  DEBUG_MESSAGE0 (DEBUG_PROTO, "fistart: Protocol started");

	  return TRUE;
	}

      iIremote_packsize >>= 1;
    }
  while (iIremote_packsize > 200);

  ulog (LOG_ERROR, "Protocol startup failed; insufficient memory for packets");

  return FALSE;
}

/* Shut down the protocol.  We can be fairly informal about this,
   since we know that the upper level protocol has already exchanged
   hangup messages.  If we didn't know that, we would have to make
   sure that all packets before the CLOSE had been received.  */

boolean
fishutdown (qdaemon)
     struct sdaemon *qdaemon;
{
  char *z;
  size_t clen;

  fIclosing = TRUE;

  z = zigetspace (qdaemon, &clen) - CHDRLEN;

  z[IHDR_INTRO] = IINTRO;
  z[IHDR_LOCAL] = IHDRWIN_SET (iIsendseq, 0);
  z[IHDR_REMOTE] = IHDRWIN_SET (iIrecseq, 0);
  iIlocal_ack = iIrecseq;
  z[IHDR_CONTENTS1] = IHDRCON_SET1 (CLOSE, qdaemon->fcaller, 0);
  z[IHDR_CONTENTS2] = IHDRCON_SET2 (CLOSE, qdaemon->fcaller, 0);
  z[IHDR_CHECK] = IHDRCHECK_VAL (z);

  DEBUG_MESSAGE0 (DEBUG_PROTO, "fishutdown: Sending CLOSE");

  if (! fsend_data (qdaemon->qconn, z, CHDRLEN, FALSE))
    return FALSE;

  ulog (LOG_NORMAL,
	"Protocol 'i' packets: sent %ld, resent %ld, received %ld",
	cIsent_packets, cIresent_packets, cIreceived_packets);
  if (cIbad_hdr != 0
      || cIbad_cksum != 0
      || cIbad_order != 0
      || cIremote_rejects != 0)
    ulog (LOG_NORMAL,
	  "Errors: header %ld, checksum %ld, order %ld, remote rejects %ld",
	  cIbad_hdr, cIbad_cksum, cIbad_order, cIremote_rejects);

  /* Reset the protocol parameters to their default values.  */
  iIrequest_packsize = IREQUEST_PACKSIZE;
  iIrequest_winsize = IREQUEST_WINSIZE;
  iIforced_remote_packsize = 0;
  iIforced_remote_winsize = 0;
  cIsync_timeout = CSYNC_TIMEOUT;
  cIsync_retries = CSYNC_RETRIES;
  cItimeout = CTIMEOUT;
  cIretries = CRETRIES;
  cIerrors = CERRORS;
  cIerror_decay = CERROR_DECAY;

  return TRUE;
}

/* Send a command string.  These are just sent as normal packets,
   ending in a packet containing a null byte.  */

boolean
fisendcmd (qdaemon, z, ilocal, iremote)
     struct sdaemon *qdaemon;
     const char *z;
     int ilocal;
     int iremote;
{
  size_t clen;

  DEBUG_MESSAGE1 (DEBUG_UUCP_PROTO, "fisendcmd: Sending command \"%s\"", z);

  clen = strlen (z);

  while (TRUE)
    {
      char *zpacket;
      size_t csize;

      zpacket = zigetspace (qdaemon, &csize);

      if (clen < csize)
	{
	  memcpy (zpacket, z, clen + 1);
	  return fisenddata (qdaemon, zpacket, clen + 1, ilocal, iremote,
			     (long) -1);
	}

      memcpy (zpacket, z, csize);
      z += csize;
      clen -= csize;

      if (! fisenddata (qdaemon, zpacket, csize, ilocal, iremote, (long) -1))
	return FALSE;
    }
  /*NOTREACHED*/
}

/* Get buffer space to use for packet data.  We return a pointer to
   the space to be used for the actual data, leaving room for the
   header.  */

/*ARGSUSED*/
char *
zigetspace (qdaemon, pclen)
     struct sdaemon *qdaemon;
     size_t *pclen;
{
  *pclen = iIremote_packsize;
  return azIsendbuffers[iIsendseq] + CHDRSKIPLEN;
}

/* Send a data packet.  The zdata argument will always point to value
   returned by zigetspace, so we know that we have room before it for
   the header information.  */

boolean
fisenddata (qdaemon, zdata, cdata, ilocal, iremote, ipos)
     struct sdaemon *qdaemon;
     char *zdata;
     size_t cdata;
     int ilocal;
     int iremote;
     long ipos;
{
  char *zhdr;
  unsigned long icksum;
  boolean fret;

#if DEBUG > 0
  if (ilocal < 0 || ilocal >= IMAXICHAN
      || iremote < 0 || iremote >= IMAXICHAN)
    ulog (LOG_FATAL, "fisenddata: ilocal %d iremote %d", ilocal, iremote);
#endif

  /* If we are changing the file position, we must send an SPOS
     packet.  */
  if (ipos != iIsendpos && ipos != (long) -1)
    {
      int inext;
      char *zspos;

      /* We need to get a buffer to hold the SPOS packet, and it needs
	 to be next sequence number.  However, the data we have been
	 given is currently in the next sequence number buffer.  So we
	 shuffle the buffers around.  */
      inext = INEXTSEQ (iIsendseq);
      zspos = azIsendbuffers[inext];
      azIsendbuffers[inext] = zdata - CHDRSKIPLEN;
      azIsendbuffers[iIsendseq] = zspos;
      zspos += CHDROFFSET;

      zspos[IHDR_INTRO] = IINTRO;
      zspos[IHDR_LOCAL] = IHDRWIN_SET (iIsendseq, 0);
      zspos[IHDR_REMOTE] = IHDRWIN_SET (iIrecseq, 0);
      iIlocal_ack = iIrecseq;
      zspos[IHDR_CONTENTS1] = IHDRCON_SET1 (SPOS, qdaemon->fcaller,
					    CCKSUMLEN);
      zspos[IHDR_CONTENTS2] = IHDRCON_SET2 (SPOS, qdaemon->fcaller,
					    CCKSUMLEN);
      zspos[IHDR_CHECK] = IHDRCHECK_VAL (zspos);
      UCKSUM_SET (zspos + CHDRLEN, (unsigned long) ipos);
      icksum = icrc (zspos + CHDRLEN, CCKSUMLEN, ICRCINIT);
      UCKSUM_SET (zspos + CHDRLEN + CCKSUMLEN, icksum);

      DEBUG_MESSAGE1 (DEBUG_PROTO, "fisenddata: Sending SPOS %ld",
		      ipos);

      if (! fsend_data (qdaemon->qconn, zspos,
			CHDRLEN + CCKSUMLEN + CCKSUMLEN, TRUE))
	return FALSE;

      iIsendseq = INEXTSEQ (iIsendseq);
      iIsendpos = ipos;
    }

  zhdr = zdata - CHDRLEN;
  zhdr[IHDR_INTRO] = IINTRO;
  zhdr[IHDR_LOCAL] = IHDRWIN_SET (iIsendseq, ilocal);
  zhdr[IHDR_CONTENTS1] = IHDRCON_SET1 (DATA, qdaemon->fcaller, cdata);
  zhdr[IHDR_CONTENTS2] = IHDRCON_SET2 (DATA, qdaemon->fcaller, cdata);

  /* Compute and set the checksum.  */
  if (cdata > 0)
    {
      icksum = icrc (zdata, cdata, ICRCINIT);
      UCKSUM_SET (zdata + cdata, icksum);
    }

  /* Wait until there is an opening in the window (we hope to not have
     to wait here at all, actually; ideally the window should be large
     enough to avoid a wait).  */
  if (iIremote_winsize > 0)
    {
      while (CSEQDIFF (iIsendseq, iIremote_ack) > iIremote_winsize)
	{
	  DEBUG_MESSAGE0 (DEBUG_PROTO, "fisenddata: Waiting for ACK");
	  if (! fiwait_for_packet (qdaemon, cItimeout, cIretries,
				   (boolean *) NULL))
	    return FALSE;
	}
    }

  /* We only fill in IHDR_REMOTE now, since only now do know the
     correct value of iIrecseq.  */
  zhdr[IHDR_REMOTE] = IHDRWIN_SET (iIrecseq, iremote);
  iIlocal_ack = iIrecseq;
  zhdr[IHDR_CHECK] = IHDRCHECK_VAL (zhdr);

  DEBUG_MESSAGE2 (DEBUG_PROTO, "fisenddata: Sending packet %d (%d bytes)",
		  iIsendseq, (int) cdata);

  iIsendseq = INEXTSEQ (iIsendseq);
  ++cIsent_packets;

  fret = fsend_data (qdaemon->qconn, zhdr,
		     cdata + CHDRLEN + (cdata > 0 ? CCKSUMLEN : 0),
		     TRUE);

  iIsendpos += cdata;

  if (fret && iPrecstart != iPrecend)
    {
      boolean fexit;

      fret = fiprocess_data (qdaemon, &fexit, (boolean *) NULL,
			     (size_t *) NULL);
    }

  return fret;
}

/* Wait for data to come in.  */

boolean
fiwait (qdaemon)
     struct sdaemon *qdaemon;
{
  return fiwait_for_packet (qdaemon, cItimeout, cIretries,
			    (boolean *) NULL);
}

/* Send a NAK.  */

static boolean
finak (qdaemon, iseq)
     struct sdaemon *qdaemon;
     int iseq;
{
  char abnak[CHDRLEN];

  abnak[IHDR_INTRO] = IINTRO;
  abnak[IHDR_LOCAL] = IHDRWIN_SET (iseq, 0);
  abnak[IHDR_REMOTE] = IHDRWIN_SET (iIrecseq, 0);
  iIlocal_ack = iIrecseq;
  abnak[IHDR_CONTENTS1] = IHDRCON_SET1 (NAK, qdaemon->fcaller, 0);
  abnak[IHDR_CONTENTS2] = IHDRCON_SET2 (NAK, qdaemon->fcaller, 0);
  abnak[IHDR_CHECK] = IHDRCHECK_VAL (abnak);

  afInaked[iseq] = TRUE;

  DEBUG_MESSAGE1 (DEBUG_PROTO, "fiwait_for_packet: Sending NAK %d", iseq);

  return fsend_data (qdaemon->qconn, abnak, CHDRLEN, TRUE);
}

/* Wait for a packet.  Either there is no data to send, or the remote
   window is full.  */

static boolean
fiwait_for_packet (qdaemon, ctimeout, cretries, pftimedout)
     struct sdaemon *qdaemon;
     int ctimeout;
     int cretries;
     boolean *pftimedout;
{
  int cshort;
  int ctimeouts;

  if (pftimedout != NULL)
    *pftimedout = FALSE;

  cshort = 0;
  ctimeouts = 0;

  while (TRUE)
    {
      boolean fexit, ffound;
      size_t cneed;
      size_t crec;

      if (! fiprocess_data (qdaemon, &fexit, &ffound, &cneed))
	return FALSE;

      if (fexit)
	return TRUE;

      if (cneed == 0)
	continue;

      DEBUG_MESSAGE1 (DEBUG_PROTO, "fiwait_for_packet: Need %d bytes",
		      (int) cneed);

      if (! freceive_data (qdaemon->qconn, cneed, &crec, ctimeout, TRUE))
	return FALSE;

      if (crec != 0)
	{
	  /* If we didn't get enough data twice in a row, we may have
	     dropped some data and be waiting for the end of a large
	     packet.  Incrementing iPrecstart will force
	     fiprocess_data to skip the current packet and try to find
	     the next one.  */
	  if (crec >= cneed)
	    cshort = 0;
	  else
	    {
	      ++cshort;
	      if (cshort > 1)
		{
		  iPrecstart = (iPrecstart + 1) % CRECBUFLEN;
		  cshort = 0;
		}
	    }
	}
      else
	{
	  /* We timed out on the read.  If we have an unacknowledged
	     packet, resend it; otherwise, send a NAK for the packet
	     we want to see.  */
	  ++ctimeouts;
	  if (ctimeouts > cretries)
	    {
	      if (cretries > 0)
		ulog (LOG_ERROR, "Timed out waiting for packet");
	      if (pftimedout != NULL)
		*pftimedout = TRUE;
	      return FALSE;
	    }

	  if (INEXTSEQ (iIremote_ack) != iIsendseq)
	    {
	      int inext;
	      char *zhdr;
	      size_t clen;

	      inext = INEXTSEQ (iIremote_ack);

	      DEBUG_MESSAGE1 (DEBUG_PROTO | DEBUG_ABNORMAL,
			      "fgwait_for_packet: Resending packet %d",
			      inext);

	      /* Update the received sequence number.  */
	      zhdr = azIsendbuffers[inext] + CHDROFFSET;
	      if (IHDRWIN_GETSEQ (zhdr[IHDR_REMOTE]) != iIrecseq)
		{
		  int iremote;

		  iremote = IHDRWIN_GETCHAN (zhdr[IHDR_REMOTE]);
		  zhdr[IHDR_REMOTE] = IHDRWIN_SET (iIrecseq, iremote);
		  zhdr[IHDR_CHECK] = IHDRCHECK_VAL (zhdr);
		  iIlocal_ack = iIrecseq;
		}
	      
	      ++cIresent_packets;

	      clen = CHDRCON_GETBYTES (zhdr[IHDR_CONTENTS1],
				       zhdr[IHDR_CONTENTS2]);

	      if (! fsend_data (qdaemon->qconn, zhdr,
				CHDRLEN + clen + (clen > 0 ? CCKSUMLEN : 0),
				TRUE))
		return FALSE;
	    }
	  else
	    {
	      if (! finak (qdaemon, INEXTSEQ (iIrecseq)))
		return FALSE;
	    }
	}
    }
  /*NOTREACHED*/
}

/* Make sure we haven't overflowed the permissible error level.  */

static boolean
ficheck_errors ()
{
  if (cIerrors < 0)
    return TRUE;

  if (((cIbad_order + cIbad_hdr + cIbad_cksum + cIremote_rejects)
       - (cIreceived_packets / cIerror_decay))
      > cIerrors)
    {
      ulog (LOG_ERROR, "Too many 'i' protocol errors");
      return FALSE;
    }

  return TRUE;
}

/* Process data waiting in the receive buffer, passing to the
   fgot_data function.  */

static boolean
fiprocess_data (qdaemon, pfexit, pffound, pcneed)
     struct sdaemon *qdaemon;
     boolean *pfexit;
     boolean *pffound;
     size_t *pcneed;
{
  if (pfexit != NULL)
    *pfexit = FALSE;
  if (pffound != NULL)
    *pffound = FALSE;

  while (iPrecstart != iPrecend)
    {
      char ab[CHDRLEN];
      int cfirst, csecond;
      char *zfirst, *zsecond;
      int i;
      int iget;
      int ttype;
      int iseq;
      int csize;
      int iack;

      /* Look for the IINTRO character.  */
      if (abPrecbuf[iPrecstart] != IINTRO)
	{
	  char *zintro;
	  int cintro;

	  cintro = iPrecend - iPrecstart;
	  if (cintro < 0)
	    cintro = CRECBUFLEN - iPrecstart;

	  zintro = memchr (abPrecbuf + iPrecstart, IINTRO, cintro);

	  if (zintro == NULL)
	    {
	      iPrecstart = (iPrecstart + cintro) % CRECBUFLEN;
	      continue;
	    }

	  /* We don't need % CRECBUFLEN here because zintro - (abPrecbuf
	     + iPrecstart) < cintro <= CRECBUFLEN - iPrecstart.  */
	  iPrecstart += zintro - (abPrecbuf + iPrecstart);
	}

      /* Get the header into ab.  */
      for (i = 0, iget = iPrecstart;
	   i < CHDRLEN && iget != iPrecend;
	   i++, iget = (iget + 1) % CRECBUFLEN)
	ab[i] = abPrecbuf[iget];

      if (i < CHDRLEN)
	{
	  if (pcneed != NULL)
	    *pcneed = CHDRLEN - i;
	  return TRUE;
	}

      if ((ab[IHDR_CHECK] & 0xff) != IHDRCHECK_VAL (ab)
	  || (FHDRCON_GETCALLER (ab[IHDR_CONTENTS1], ab[IHDR_CONTENTS2])
	      ? qdaemon->fcaller : ! qdaemon->fcaller))
	{
	  DEBUG_MESSAGE0 (DEBUG_PROTO, "fiprocess_data: Bad header");

	  ++cIbad_hdr;
	  if (! ficheck_errors ())
	    return FALSE;

	  iPrecstart = (iPrecstart + 1) % CRECBUFLEN;
	  continue;
	}

      zfirst = zsecond = NULL;
      cfirst = csecond = 0;

      ttype = THDRCON_GETTYPE (ab[IHDR_CONTENTS1], ab[IHDR_CONTENTS2]);
      if (ttype == DATA || ttype == SPOS || ttype == CLOSE)
	iseq = IHDRWIN_GETSEQ (ab[IHDR_LOCAL]);
      else
	iseq = -1;
      csize = CHDRCON_GETBYTES (ab[IHDR_CONTENTS1], ab[IHDR_CONTENTS2]);

      if (iseq != -1)
	{
	  /* Make sure this packet is in our receive window.  The last
	     packet we have received is iIrecseq.  The last packet we
	     have acked is iIlocal_ack.  */
	  if (iIrequest_winsize > 0
	      && CSEQDIFF (iseq, iIlocal_ack) >= iIrequest_winsize)
	    {
	      DEBUG_MESSAGE1 (DEBUG_PROTO,
			      "fiprocess_data: Out of order packet %d",
			      iseq);

	      ++cIbad_order;
	      if (! ficheck_errors ())
		return FALSE;

	      iPrecstart = (iPrecstart + 1) % CRECBUFLEN;

	      continue;
	    }
	}

      if (csize > 0)
	{
	  int cinbuf;
	  char abcksum[CCKSUMLEN];
	  unsigned long ickdata;

	  cinbuf = iPrecend - iPrecstart;
	  if (cinbuf < 0)
	    cinbuf += CRECBUFLEN;
	  if (cinbuf < CHDRLEN + csize + CCKSUMLEN)
	    {
	      if (pcneed != NULL)
		*pcneed = CHDRLEN + csize + CCKSUMLEN - cinbuf;
	      return TRUE;
	    }

	  if (iPrecend > iPrecstart)
	    {
	      cfirst = csize;
	      zfirst = abPrecbuf + iPrecstart + CHDRLEN;
	    }
	  else
	    {
	      cfirst = CRECBUFLEN - (iPrecstart + CHDRLEN);
	      if (cfirst < 0)
		{
		  /* Here cfirst is non-positive, so subtracting from
		     abPrecbuf will actually skip the appropriate number
		     of bytes at the start of abPrecbuf.  */
		  zfirst = abPrecbuf - cfirst;
		  cfirst = csize;
		}
	      else
		{
		  if (cfirst >= csize)
		    cfirst = csize;
		  else
		    {
		      zsecond = abPrecbuf;
		      csecond = csize - cfirst;
		    }
		  zfirst = abPrecbuf + iPrecstart + CHDRLEN;
		}
	    }

	  /* Get the checksum into abcksum.  */
	  for (i = 0, iget = (iPrecstart + CHDRLEN + csize) % CRECBUFLEN;
	       i < CCKSUMLEN;
	       i++, iget = (iget + 1) % CRECBUFLEN)
	    abcksum[i] = abPrecbuf[iget];

	  ickdata = icrc (zfirst, cfirst, ICRCINIT);
	  if (csecond > 0)
	    ickdata = icrc (zsecond, csecond, ickdata);

	  if (ICKSUM_GET (abcksum) != ickdata)
	    {
	      DEBUG_MESSAGE2 (DEBUG_PROTO,
			      "fiprocess_data: Bad checksum; data %lu, frame %lu",
			      ickdata, ICKSUM_GET (abcksum));

	      ++cIbad_cksum;
	      if (! ficheck_errors ())
		return FALSE;

	      if (iseq != -1)
		{
		  if (! finak (qdaemon, iseq))
		    return FALSE;
		}

	      iPrecstart = (iPrecstart + 1) % CRECBUFLEN;
	      continue;
	    }
	}

      /* Here we know that this is a valid packet, so we can adjust
	 iPrecstart accordingly.  */
      if (csize == 0)
	iPrecstart = (iPrecstart + CHDRLEN) % CRECBUFLEN;
      else
	{
	  iPrecstart = ((iPrecstart + CHDRLEN + csize + CCKSUMLEN)
			% CRECBUFLEN);
	  ++cIreceived_packets;
	}

      /* Get the ack from the packet, if appropriate.  */
      iack = IHDRWIN_GETSEQ (ab[IHDR_REMOTE]);
      if (iIrequest_winsize > 0
	  && iseq != iIsendseq
	  && CSEQDIFF (iseq, iIremote_ack) <= iIrequest_winsize
	  && CSEQDIFF (iIsendseq, iseq) <= iIrequest_winsize)
	iIremote_ack = iack;

      /* If we haven't handled all previous packets, we must save off this
	 packet and deal with it later.  */
      if (iseq != -1)
	{
	  if (iseq != INEXTSEQ (iIrecseq))
	    {
	      /* If this is a duplicate packet, just ignore it.  */
	      if ((iIrequest_winsize > 0
		   && CSEQDIFF (iseq, iIrecseq) > iIrequest_winsize)
		  || azIrecbuffers[iseq] != NULL)
		{
		  DEBUG_MESSAGE1 (DEBUG_PROTO,
				  "fiprocess_data: Ignoring duplicate packet %d",
				  iseq);
		  continue;
		}

	      DEBUG_MESSAGE1 (DEBUG_PROTO,
			      "fiprocess_data: Saving unexpected packet %d",
			      iseq);

	      azIrecbuffers[iseq] = zbufalc (CHDRLEN + csize);
	      memcpy (azIrecbuffers[iseq], ab, CHDRLEN);
	      if (csize > 0)
		{
		  memcpy (azIrecbuffers[iseq] + CHDRLEN, zfirst, cfirst);
		  if (csecond > 0)
		    memcpy (azIrecbuffers[iseq] + CHDRLEN + cfirst,
			    zsecond, csecond);
		}

	      /* Send NAK's for each packet between the last one we
		 received and this one, avoiding any packets for which
		 we've already sent NAK's.  */
	      for (i = INEXTSEQ (iIrecseq);
		   i != iseq;
		   i = INEXTSEQ (i))
		{
		  if (! afInaked[i])
		    {
		      if (! finak (qdaemon, i))
			return FALSE;
		    }
		}

	      continue;
	    }

	  iIrecseq = iseq;
	}

      if (pffound != NULL)
	*pffound = TRUE;

      if (! fiprocess_packet (qdaemon, ab, zfirst, cfirst, zsecond, csecond,
			      pfexit))
	return FALSE;

      if (iseq != -1)
	{
	  int inext;

	  /* If we've already received the next packet(s), process
	     them.  */
	  inext = INEXTSEQ (iIrecseq);
	  while (azIrecbuffers[inext] != NULL)
	    {
	      char *z;
	      int c;

	      z = azIrecbuffers[inext];
	      c = CHDRCON_GETBYTES (z[IHDR_CONTENTS1], z[IHDR_CONTENTS2]);
	      iIrecseq = inext;
	      if (! fiprocess_packet (qdaemon, z, z + CHDRLEN, c,
				      (char *) NULL, 0, pfexit))
		return FALSE;
	      ubuffree (azIrecbuffers[inext]);
	      azIrecbuffers[inext] = NULL;
	      inext = INEXTSEQ (inext);
	    }
	}

      /* If we have received half the window size or more since the
	 last ACK, send one now.  Note that this is unlikely if we are
	 currently sending data, since each packet sent will ACK the
	 most recently received packet.  Sending an ACK for half the
	 window at a time should significantly cut the acknowledgement
	 traffic when only one side is sending.  */
      if (iIremote_winsize > 0
	  && CSEQDIFF (iIrecseq, iIlocal_ack) >= iIremote_winsize / 2)
	{
	  char aback[CHDRLEN];

	  for (i = iIlocal_ack; i <= iIrecseq; i++)
	    afInaked[i] = FALSE;

	  aback[IHDR_INTRO] = IINTRO;
	  aback[IHDR_LOCAL] = IHDRWIN_SET (0, 0);
	  aback[IHDR_REMOTE] = IHDRWIN_SET (iIrecseq, 0);
	  iIlocal_ack = iIrecseq;
	  aback[IHDR_CONTENTS1] = IHDRCON_SET1 (ACK, qdaemon->fcaller, 0);
	  aback[IHDR_CONTENTS2] = IHDRCON_SET2 (ACK, qdaemon->fcaller, 0);
	  aback[IHDR_CHECK] = IHDRCHECK_VAL (aback);

	  DEBUG_MESSAGE1 (DEBUG_PROTO, "fiprocess_data: Sending ACK %d",
			  iIrecseq);

	  if (! fsend_data (qdaemon->qconn, aback, CHDRLEN, TRUE))
	    return FALSE;
	}
    }

  if (pcneed != NULL)
    *pcneed = CHDRLEN;

  return TRUE;
}

/* Process a single packet.  */

static boolean
fiprocess_packet (qdaemon, zhdr, zfirst, cfirst, zsecond, csecond, pfexit)
     struct sdaemon *qdaemon;
     const char *zhdr;
     const char *zfirst;
     int cfirst;
     const char *zsecond;
     int csecond;
     boolean *pfexit;
{
  int ttype;

  ttype = THDRCON_GETTYPE (zhdr[IHDR_CONTENTS1], zhdr[IHDR_CONTENTS2]);
  switch (ttype)
    {
    case DATA:
      {
	boolean fret;

	DEBUG_MESSAGE2 (DEBUG_PROTO,
			"fiprocess_packet: Got DATA packet %d size %d",
			IHDRWIN_GETSEQ (zhdr[IHDR_LOCAL]),
			cfirst + csecond);
	fret = fgot_data (qdaemon, zfirst, (size_t) cfirst,
			  zsecond, (size_t) csecond,
			  IHDRWIN_GETCHAN (zhdr[IHDR_REMOTE]),
			  IHDRWIN_GETCHAN (zhdr[IHDR_LOCAL]),
			  iIrecpos, pfexit);
	iIrecpos += cfirst + csecond;
	return fret;
      }

    case SYNC:
      {
	int ipack, iwin;

	/* We accept a SYNC packet to adjust the packet and window
	   sizes at any time, although we don't currently generate
	   SYNC packets except when initializating the connection.  */
	if (cfirst + csecond < 3)
	  {
	    ulog (LOG_ERROR, "Bad SYNC packet");
	    return FALSE;
	  }
	ipack = (zfirst[0] & 0xff) << 8;
	if (cfirst > 1)
	  ipack |= zfirst[1] & 0xff;
	else
	  ipack |= zsecond[0];
	if (cfirst > 2)
	  iwin = zfirst[2];
	else
	  iwin = zsecond[2 - cfirst];

	DEBUG_MESSAGE2 (DEBUG_PROTO,
			"fiprocess_packet: Got SYNC packsize %d winsize %d",
			ipack, iwin);

	if (iIforced_remote_packsize == 0)
	  iIremote_packsize = ipack;
	if (iIforced_remote_winsize == 0)
	  iIremote_winsize = iwin;

	/* We increment a static variable to tell the initialization
	   code that a SYNC was received, and we set *pfexit to TRUE
	   to get out to the initialization code (this will do no harm
	   if we are called from elsewhere).  */
	++cIsyncs;
	*pfexit = TRUE;
	return TRUE;
      }

    case ACK:
      /* There is nothing to do here, since the ack was already
	 handled in fiprocess_data.  */
      DEBUG_MESSAGE1 (DEBUG_PROTO,
		      "fiprocess_packet: Got ACK %d",
		      IHDRWIN_GETSEQ (zhdr[IHDR_REMOTE]));
      return TRUE;

    case NAK:
      /* We must resend the requested packet.  */
      {      
	int iseq;
	char *zsend;
	size_t clen;

	++cIremote_rejects;
	if (! ficheck_errors ())
	  return FALSE;

	iseq = IHDRWIN_GETSEQ (zhdr[IHDR_LOCAL]);

	if (iIrequest_winsize > 0
	    && (iseq == iIsendseq
		|| CSEQDIFF (iseq, iIremote_ack) > iIrequest_winsize
		|| CSEQDIFF (iIsendseq, iseq) > iIrequest_winsize))
	  {
	    DEBUG_MESSAGE1 (DEBUG_PROTO,
			    "fiprocess_packet: Ignoring out of order NAK %d",
			    iseq);
	    return TRUE;
	  }

	DEBUG_MESSAGE1 (DEBUG_PROTO,
			"fiprocess_packet: Got NAK %d; resending packet",
			iseq);

	/* Update the received sequence number.  */
	zsend = azIsendbuffers[iseq] + CHDROFFSET;
	if (IHDRWIN_GETSEQ (zsend[IHDR_REMOTE]) != iIrecseq)
	  {
	    int iremote;

	    iremote = IHDRWIN_GETCHAN (zsend[IHDR_REMOTE]);
	    zsend[IHDR_REMOTE] = IHDRWIN_SET (iIrecseq, iremote);
	    zsend[IHDR_CHECK] = IHDRCHECK_VAL (zsend);
	    iIlocal_ack = iIrecseq;
	  }
	      
	++cIresent_packets;

	clen = CHDRCON_GETBYTES (zsend[IHDR_CONTENTS1],
				 zsend[IHDR_CONTENTS2]);

	return fsend_data (qdaemon->qconn, zsend,
			   CHDRLEN + clen + (clen > 0 ? CCKSUMLEN : 0),
			   TRUE);
      }

    case SPOS:
      /* Set the file position.  */
      {
	char abpos[CCKSUMLEN];
	const char *zpos;

	if (cfirst >= CCKSUMLEN)
	  zpos = zfirst;
	else
	  {
	    memcpy (abpos, zfirst, cfirst);
	    memcpy (abpos + cfirst, zsecond, CCKSUMLEN - cfirst);
	    zpos = abpos;
	  }
	iIrecpos = (long) ICKSUM_GET (zpos);
	DEBUG_MESSAGE1 (DEBUG_PROTO,
			"fiprocess_packet: Got SPOS %ld", iIrecpos);
	return TRUE;
      }

    case CLOSE:
      if (fLog_sighup && ! fIclosing)
	ulog (LOG_ERROR, "Received unexpected CLOSE packet");
      else
	DEBUG_MESSAGE0 (DEBUG_PROTO, "fiprocess_packet: Got CLOSE packet");

      *pfexit = TRUE;
      return ! fLog_sighup || fIclosing;

    default:
      /* Just ignore unrecognized packet types, for future protocol
	 enhancements.  */
      DEBUG_MESSAGE1 (DEBUG_PROTO, "fiprocess_packet: Got packet type %d",
		      ttype);
      return TRUE;
    }
  /*NOTREACHED*/
}