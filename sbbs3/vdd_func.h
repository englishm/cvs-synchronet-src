/* VDD_FUNC.H */

/* Synchronet WinNT VDD FOSSIL constant/type definitions */

/* $Id$ */

/****************************************************************************
 * @format.tab-size 4		(Plain Text/Source Code File Header)			*
 * @format.use-tabs true	(see http://www.synchro.net/ptsc_hdr.html)		*
 *																			*
 * Copyright 2000 Rob Swindell - http://www.synchro.net/copyright.html		*
 *																			*
 * This program is free software; you can redistribute it and/or			*
 * modify it under the terms of the GNU General Public License				*
 * as published by the Free Software Foundation; either version 2			*
 * of the License, or (at your option) any later version.					*
 * See the GNU General Public License for more details: gpl.txt or			*
 * http://www.fsf.org/copyleft/gpl.html										*
 *																			*
 * Anonymous FTP access to the most recent released source is available at	*
 * ftp://vert.synchro.net, ftp://cvs.synchro.net and ftp://ftp.synchro.net	*
 *																			*
 * Anonymous CVS access to the development source and modification history	*
 * is available at cvs.synchro.net:/cvsroot/sbbs, example:					*
 * cvs -d :pserver:anonymous@cvs.synchro.net:/cvsroot/sbbs login			*
 *     (just hit return, no password is necessary)							*
 * cvs -d :pserver:anonymous@cvs.synchro.net:/cvsroot/sbbs checkout src		*
 *																			*
 * For Synchronet coding style and modification guidelines, see				*
 * http://www.synchro.net/source.html										*
 *																			*
 * You are encouraged to submit any modifications (preferably in Unix diff	*
 * format) via e-mail to mods@synchro.net									*
 *																			*
 * Note: If this box doesn't appear square, then you need to fix your tabs.	*
 ****************************************************************************/


enum {
	 VDD_OPEN
	,VDD_CLOSE
	,VDD_READ
	,VDD_PEEK
	,VDD_WRITE
	,VDD_STATUS
	,VDD_INBUF_PURGE
	,VDD_INBUF_FULL
	,VDD_INBUF_SIZE
	,VDD_OUTBUF_PURGE
	,VDD_OUTBUF_FULL
	,VDD_OUTBUF_SIZE
	,VDD_ONLINE
	,VDD_YIELD
};

typedef struct {
	DWORD online;
	DWORD inbuf_size;
	DWORD inbuf_full;
	DWORD outbuf_size;
	DWORD outbuf_full;
} vdd_status_t;