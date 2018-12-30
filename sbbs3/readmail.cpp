/* readmail.cpp */

/* Synchronet private mail reading function */

/* $Id: readmail.cpp,v 1.84 2018/12/30 08:40:28 rswindell Exp $ */

/****************************************************************************
 * @format.tab-size 4		(Plain Text/Source Code File Header)			*
 * @format.use-tabs true	(see http://www.synchro.net/ptsc_hdr.html)		*
 *																			*
 * Copyright Rob Swindell - http://www.synchro.net/copyright.html			*
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

#include "sbbs.h"

static char mail_listing_flag(smbmsg_t* msg)
{
	if(msg->hdr.attr&MSG_DELETE)				return '-';
	if(msg->hdr.attr&MSG_SPAM)					return 'S';
	if(msg->hdr.attr&MSG_REPLIED)				return 'R';
	if(msg->hdr.attr&MSG_READ)					return ' ';
	if(msg->hdr.attr&MSG_PERMANENT)				return 'p';
	if(msg->hdr.attr&MSG_LOCKED)				return 'L';
	if(msg->hdr.attr&MSG_KILLREAD)				return 'K';
	if(msg->hdr.attr&MSG_NOREPLY)				return '#';
	if(msg->from_net.type || msg->to_net.type)	return 'N';
	if(msg->hdr.attr&MSG_ANONYMOUS)				return 'A';
	return '*';
}

/****************************************************************************/
/* Reads mail waiting for usernumber.                                       */
/****************************************************************************/
void sbbs_t::readmail(uint usernumber, int which, long lm_mode)
{
	char	str[256],str2[256],str3[256],done=0,domsg=1
			,*p,*tp,*sp,ch;
	char	path[MAX_PATH+1];
	char 	tmp[512];
	int		i;
	uint32_t u,v;
	int		error;
	int		mismatches=0,act;
	uint	unum;
    long    length,l,last_mode;
	ulong	last;
	bool	replied;
	file_t	fd;
	mail_t	*mail;
	smbmsg_t msg;
	char search_str[128] = "";

	if(cfg.readmail_mod[0] && !readmail_inside) {
		char cmdline[256];

		readmail_inside = true;
		safe_snprintf(cmdline, sizeof(cmdline), "%s %d %u %lu", cfg.readmail_mod, which, usernumber, lm_mode);
		exec_bin(cmdline, &main_csi);
		readmail_inside = false;
		return;
	}

	if(which==MAIL_SENT && useron.rest&FLAG('K')) {
		bputs(text[R_ReadSentMail]);
		return;
	}

	msg.total_hfields=0;			/* init to NULL, cause not allocated yet */

	fd.dir=cfg.total_dirs+1;			/* temp dir for file attachments */

	if((i=smb_stack(&smb,SMB_STACK_PUSH))!=0) {
		errormsg(WHERE,ERR_OPEN,"MAIL",i);
		return; 
	}
	sprintf(smb.file,"%smail",cfg.data_dir);
	smb.retry_time=cfg.smb_retry_time;
	smb.subnum=INVALID_SUB;
	if((i=smb_open(&smb))!=0) {
		smb_stack(&smb,SMB_STACK_POP);
		errormsg(WHERE,ERR_OPEN,smb.file,i,smb.last_error);
		return; 
	}

	if(cfg.sys_misc&SM_SYSVDELM && (SYSOP || cfg.sys_misc&SM_USRVDELM))
		lm_mode |= LM_INCDEL;
	mail=loadmail(&smb,&smb.msgs,usernumber,which,lm_mode);
	last_mode = lm_mode;
	if(!smb.msgs) {
		if(which==MAIL_SENT)
			bputs(text[NoMailSent]);
		else if(which==MAIL_ALL)
			bputs(text[NoMailOnSystem]);
		else
			bprintf(text[NoMailWaiting], lm_mode&LM_UNREAD ? "un-read mail" : "mail");
		smb_close(&smb);
		smb_stack(&smb,SMB_STACK_POP);
		return; 
	}

	last=smb.status.last_msg;

	if(which==MAIL_SENT)
		act=NODE_RSML;
	else if(which==MAIL_ALL)
		act=NODE_SYSP;
	else
		act=NODE_RMAL;
	action=act;
	char* order = (lm_mode&LM_REVERSE) ? "newest" : "oldest";
	if(smb.msgs>1 && which!=MAIL_ALL) {
		if(which==MAIL_SENT)
			bprintf(text[MailSentLstHdr], order);
		else
			bprintf(text[MailWaitingLstHdr], order);

		for(smb.curmsg=0;smb.curmsg<smb.msgs && !msgabort();smb.curmsg++) {
			if(msg.total_hfields)
				smb_freemsgmem(&msg);
			msg.total_hfields=0;
			msg.idx.offset=mail[smb.curmsg].offset;
			if(loadmsg(&msg,mail[smb.curmsg].number) < 1)
				continue;
			smb_unlockmsghdr(&smb,&msg);
			bprintf(text[MailWaitingLstFmt],smb.curmsg+1
				,which==MAIL_SENT ? msg.to
				: (msg.hdr.attr&MSG_ANONYMOUS) && !SYSOP ? text[Anonymous]
				: msg.from
				,mail_listing_flag(&msg)
				,msg.subj);
			smb_freemsgmem(&msg);
			msg.total_hfields=0; 
		}

		ASYNC;
		if(sys_status&SS_ABORT) {
			domsg=0;
			smb.curmsg=0;
		} else {
			bprintf(text[StartWithN],1L);
			l=getnum(smb.msgs);
			if(l>0)
				smb.curmsg=l-1;
			else if(l==-1) {
				free(mail);
				smb_close(&smb);
				smb_stack(&smb,SMB_STACK_POP);
				return;
			}
			else
				smb.curmsg=l;
		}
		sys_status&=~SS_ABORT; 
	}
	else {
		smb.curmsg=0;
		if(which==MAIL_ALL)
			domsg=0; 
	}
	if(which==MAIL_SENT) {
		logline("E","read sent mail");
	} else if(which==MAIL_ALL) {
		logline("S+","read all mail");
	} else {
		logline("E","read mail");
	}
	const char* menu_file = (which == MAIL_ALL ? "allmail" : which==MAIL_SENT ? "sentmail" : "mailread");
	if(useron.misc&RIP)
		menu(menu_file); 
	current_msg=&msg;	/* For MSG_* @-codes and bbs.msg_* property values */
	while(online && !done) {
		action=act;

		if(msg.total_hfields)
			smb_freemsgmem(&msg);
		msg.total_hfields=0;

		msg.idx.offset=mail[smb.curmsg].offset;
		msg.idx.number=mail[smb.curmsg].number;
		msg.idx.to=mail[smb.curmsg].to;
		msg.idx.from=mail[smb.curmsg].from;
		msg.idx.subj=mail[smb.curmsg].subj;

		if((i=smb_locksmbhdr(&smb))!=0) {
			errormsg(WHERE,ERR_LOCK,smb.file,i,smb.last_error);
			break; 
		}

		if((i=smb_getstatus(&smb))!=0) {
			smb_unlocksmbhdr(&smb);
			errormsg(WHERE,ERR_READ,smb.file,i,smb.last_error);
			break; 
		}
		smb_unlocksmbhdr(&smb);

		if(smb.status.last_msg!=last || lm_mode != last_mode) { 	/* New messages */
			last=smb.status.last_msg;
			free(mail);
			order = (lm_mode&LM_REVERSE) ? "newest" : "oldest";
			mail=loadmail(&smb,&smb.msgs,usernumber,which,lm_mode);   /* So re-load */
			if(!smb.msgs)
				break;
			if(lm_mode != last_mode)
				smb.curmsg = 0;
			else {
				for(smb.curmsg=0;smb.curmsg<smb.msgs;smb.curmsg++)
					if(mail[smb.curmsg].number==msg.idx.number)
						break;
				if(smb.curmsg>=smb.msgs)
					smb.curmsg=(smb.msgs-1);
			}
			last_mode = lm_mode;
			continue; 
		}

		if(loadmsg(&msg,mail[smb.curmsg].number) < 0) {	/* Message header gone */
			if(mismatches>5) {	/* We can't do this too many times in a row */
				errormsg(WHERE,ERR_CHK,"message number",mail[smb.curmsg].number);
				break; 
			}
			free(mail);
			mail=loadmail(&smb,&smb.msgs,usernumber,which,lm_mode);
			if(!smb.msgs)
				break;
			last_mode = lm_mode;
			if(smb.curmsg>(smb.msgs-1))
				smb.curmsg=(smb.msgs-1);
			mismatches++;
			continue; 
		}
		smb_unlockmsghdr(&smb,&msg);
		msg.idx.attr=msg.hdr.attr;

		mismatches=0;

		if(domsg && !(sys_status&SS_ABORT)) {

			show_msg(&msg
				,msg.from_ext && msg.idx.from==1 && !msg.from_net.type
					? 0:P_NOATCODES);

			char* txt;
			int attachment_index = 0;
			bool found = true;
			while((txt=smb_getmsgtxt(&smb,&msg, 0)) != NULL && found) {
				char filename[MAX_PATH+1] = {0};
				uint32_t filelen = 0;
				uint8_t* filedata;
				if((filedata = smb_getattachment(&msg, txt, filename, &filelen, attachment_index++)) != NULL 
					&& filename[0] != 0 && filelen > 0) {
					char tmp[32];
					sprintf(str3, text[DownloadAttachedFileQ], filename, ultoac(filelen,tmp));
					if(!noyes(str3)) {
						char fpath[MAX_PATH+1];
						SAFEPRINTF2(fpath, "%s%s", cfg.temp_dir, filename);
						FILE* fp = fopen(fpath, "wb");
						if(fp == NULL)
							errormsg(WHERE, ERR_OPEN, fpath, 0);
						else {
							int result = fwrite(filedata, filelen, 1, fp);
							fclose(fp);
							if(!result)
								errormsg(WHERE, ERR_WRITE, fpath, filelen);
							else
								sendfile(fpath, useron.prot, "attachment");
						}
					}
				} else
					found = false;
				smb_freemsgtxt(txt);
			}

			if(msg.hdr.auxattr&MSG_FILEATTACH) {  /* Attached file */
				smb_getmsgidx(&smb,&msg);
				SAFECOPY(str, msg.subj);					/* filenames (multiple?) in title */
				tp=str;
				while(online) {
					p=strchr(tp,' ');
					if(p) *p=0;
					sp=strrchr(tp,'/');              /* sp is slash pointer */
					if(!sp) sp=strrchr(tp,'\\');
					if(sp) tp=sp+1;
					padfname(tp,fd.name);
					SAFEPRINTF3(path,"%sfile/%04u.in/%s"  /* path is path/fname */
						,cfg.data_dir,msg.idx.to,tp);
					if(!fexistcase(path) && msg.idx.from)
						SAFEPRINTF3(path,"%sfile/%04u.out/%s"  /* path is path/fname */
							,cfg.data_dir,msg.idx.from,tp);
					length=(long)flength(path);
					if(length<1)
						bprintf(text[FileDoesNotExist], tp);
					else if(!(useron.exempt&FLAG('T')) && cur_cps && !SYSOP
						&& length/(long)cur_cps>(time_t)timeleft)
						bputs(text[NotEnoughTimeToDl]);
					else {
						sprintf(str3,text[DownloadAttachedFileQ]
							,tp,ultoac(length,tmp));
						if(length>0L && text[DownloadAttachedFileQ][0] && yesno(str3)) {
							{	/* Remote User */
								xfer_prot_menu(XFER_DOWNLOAD);
								mnemonics(text[ProtocolOrQuit]);
								strcpy(str3,"Q");
								for(i=0;i<cfg.total_prots;i++)
									if(cfg.prot[i]->dlcmd[0]
										&& chk_ar(cfg.prot[i]->ar,&useron,&client)) {
										sprintf(tmp,"%c",cfg.prot[i]->mnemonic);
										strcat(str3,tmp); 
									}
								ch=(char)getkeys(str3,0);
								for(i=0;i<cfg.total_prots;i++)
									if(cfg.prot[i]->dlcmd[0] && ch==cfg.prot[i]->mnemonic
										&& chk_ar(cfg.prot[i]->ar,&useron,&client))
										break;
								if(i<cfg.total_prots) {
									error=protocol(cfg.prot[i],XFER_DOWNLOAD,path,nulstr,false);
									if(checkprotresult(cfg.prot[i],error,&fd)) {
										if(which==MAIL_YOUR)
											remove(path);
										logon_dlb+=length;	/* Update stats */
										logon_dls++;
										useron.dls=(ushort)adjustuserrec(&cfg,useron.number
											,U_DLS,5,1);
										useron.dlb=adjustuserrec(&cfg,useron.number
											,U_DLB,10,length);
										bprintf(text[FileNBytesSent]
											,fd.name,ultoac(length,tmp));
										sprintf(str3
											,"downloaded attached file: %s"
											,fd.name);
										logline("D-",str3); 
									}
									autohangup(); 
								} 
							} 
						} 
					}
					if(!p)
						break;
					tp=p+1;
					while(*tp==' ') tp++; 
				}
				sprintf(str,"%sfile/%04u.in",cfg.data_dir,usernumber);
				rmdir(str); 
			}
			if(which==MAIL_YOUR && !(msg.hdr.attr&MSG_READ)) {
				mail[smb.curmsg].attr|=MSG_READ;
				if(thisnode.status==NODE_INUSE)
					telluser(&msg);
				if(msg.total_hfields)
					smb_freemsgmem(&msg);
				msg.total_hfields=0;
				msg.idx.offset=0;						/* Search by number */
				if(smb_locksmbhdr(&smb)==SMB_SUCCESS) {	/* Lock the entire base */
					if(loadmsg(&msg,msg.idx.number) >= 0) {
						msg.hdr.attr|=MSG_READ;
						if(msg.hdr.attr&MSG_KILLREAD)
							msg.hdr.attr|=MSG_DELETE;
						msg.idx.attr=msg.hdr.attr;
						if((i=smb_putmsg(&smb,&msg))!=0)
							errormsg(WHERE,ERR_WRITE,smb.file,i,smb.last_error);
						smb_unlockmsghdr(&smb,&msg); 
					}
					smb_unlocksmbhdr(&smb); 
				}
				if(!msg.total_hfields) {				/* unsuccessful reload */
					domsg=0;
					continue; 
				}
			}
		}
		else domsg=1;

		if(useron.misc&WIP)
			menu(menu_file); 

		ASYNC;
		if(which==MAIL_SENT)
			bprintf(text[ReadingSentMail],smb.curmsg+1,smb.msgs);
		else if(which==MAIL_ALL)
			bprintf(text[ReadingAllMail],smb.curmsg+1,smb.msgs);
		else
			bprintf(text[ReadingMail],smb.curmsg+1,smb.msgs);
		sprintf(str,"ADFLNQRT?<>[]{}()-+/!");
		if(SYSOP)
			strcat(str,"CUSPH");
		if(which == MAIL_YOUR)
			strcat(str,"K");	// kill all (visible)
		else
			strcat(str,"E");	// edit msg
		if(which != MAIL_SENT)
			strcat(str,"V");	// View SPAM toggle
		l=getkeys(str,smb.msgs);
		if(l&0x80000000L) {
			if(l==-1)	/* ctrl-c */
				break;
			smb.curmsg=(l&~0x80000000L)-1;
			continue; 
		}
		switch(l) {
			case '!':
				lm_mode ^= LM_REVERSE;
				domsg=0;
				break;
			case 'A':   /* Auto-reply to last piece */
			case 'R':
				if(l==(cfg.sys_misc&SM_RA_EMU ? 'A' : 'R'))  /* re-read last message */
					break;

				if(which==MAIL_SENT)
					break;
				if((msg.hdr.attr&(MSG_NOREPLY|MSG_ANONYMOUS)) && !SYSOP) {
					bputs(text[CantReplyToMsg]);
					break; 
				}

				quotemsg(&msg,/* include tails: */TRUE);

				if(msg.from_net.addr==NULL)
					SAFECOPY(str,msg.from);
				else if(msg.from_net.type==NET_FIDO) 	/* FidoNet type */
					SAFEPRINTF2(str,"%s@%s",msg.from
						,smb_faddrtoa((faddr_t *)msg.from_net.addr,tmp));
				else if(msg.from_net.type==NET_INTERNET || strchr((char*)msg.from_net.addr,'@')!=NULL) {
					if(msg.replyto_net.type==NET_INTERNET)
						SAFECOPY(str,(char *)msg.replyto_net.addr);
					else
						SAFECOPY(str,(char *)msg.from_net.addr);
				} else
					SAFEPRINTF2(str,"%s@%s",msg.from,(char*)msg.from_net.addr);

				SAFECOPY(str2,str);

				bputs(text[Email]);
				if(!getstr(str,64,K_EDIT|K_AUTODEL))
					break;
				msg.hdr.number=msg.idx.number;
				smb_getmsgidx(&smb,&msg);

				if(!stricmp(str2,str))		/* Reply to sender */
					sprintf(str2,text[Regarding],msg.subj);
				else						/* Reply to other */
					sprintf(str2,text[RegardingByOn],msg.subj,msg.from
						,timestr(msg.hdr.when_written.time));

				p=strrchr(str,'@');
				if(p) { 							/* name @addr */
					replied=netmail(str,msg.subj,WM_QUOTE);
					sprintf(str2,text[DeleteMailQ],msg.from); 
				}
				else {
					if(!msg.from_net.type && !stricmp(str,msg.from))
						replied=email(msg.idx.from,str2,msg.subj,WM_EMAIL|WM_QUOTE);
					else if(!stricmp(str,"SYSOP"))
						replied=email(1,str2,msg.subj,WM_EMAIL|WM_QUOTE);
					else if((i=finduser(str))!=0)
						replied=email(i,str2,msg.subj,WM_EMAIL|WM_QUOTE);
					else
						replied=false;
					sprintf(str2,text[DeleteMailQ],msg.from); 
				}

				if(replied==true && !(msg.hdr.attr&MSG_REPLIED)) {
					if(msg.total_hfields)
						smb_freemsgmem(&msg);
					msg.total_hfields=0;
					msg.idx.offset=0;
					if(smb_locksmbhdr(&smb)==SMB_SUCCESS) {	/* Lock the entire base */
						if(loadmsg(&msg,msg.idx.number) >= 0) {
							msg.hdr.attr|=MSG_REPLIED;
							msg.idx.attr=msg.hdr.attr;
							if((i=smb_putmsg(&smb,&msg))!=0)
								errormsg(WHERE,ERR_WRITE,smb.file,i,smb.last_error);
							smb_unlockmsghdr(&smb,&msg); 
						}
						smb_unlocksmbhdr(&smb);
					}
				}

				if(msg.hdr.attr&MSG_DELETE || noyes(str2)) {
					if(smb.curmsg<smb.msgs-1) smb.curmsg++;
					else done=1;
					break;	}
				/* Case 'D': must follow! */
			case 'D':   /* Delete last piece (toggle) */
				if(msg.hdr.attr&MSG_PERMANENT) {
					bputs(text[CantDeleteMsg]);
					domsg=0;
					break; 
				}
				if(msg.total_hfields)
					smb_freemsgmem(&msg);
				msg.total_hfields=0;
				msg.idx.offset=0;
				if(smb_locksmbhdr(&smb)==SMB_SUCCESS) {	/* Lock the entire base */
					if(loadmsg(&msg,msg.idx.number) >= 0) {
						msg.hdr.attr^=MSG_DELETE;
						msg.idx.attr=msg.hdr.attr;
		//				  mail[smb.curmsg].attr=msg.hdr.attr;
						if((i=smb_putmsg(&smb,&msg))!=0)
							errormsg(WHERE,ERR_WRITE,smb.file,i,smb.last_error);
						smb_unlockmsghdr(&smb,&msg); 
					}
					smb_unlocksmbhdr(&smb);
				}
				if(smb.curmsg<smb.msgs-1) smb.curmsg++;
				else done=1;
				break;
			case 'K':	/* Kill All Mail */
				SAFEPRINTF(str,text[DeleteMailQ],"everyone");
				if(!noyes(str))
					delallmail(usernumber, which, /* permanent: */false, lm_mode);
				domsg=false;
				break;
			case 'F':  /* Forward last piece */
				domsg=0;
				bputs(text[ForwardMailTo]);
				if(!getstr(str,LEN_ALIAS,cfg.uq&UQ_NOUPRLWR ? K_NONE:K_UPRLWR))
					break;
				i=finduser(str);
				if(!i)
					break;
				domsg=1;
				if(smb.curmsg<smb.msgs-1) smb.curmsg++;
				else done=1;
				smb_getmsgidx(&smb,&msg);
				forwardmail(&msg,i);
				if(msg.hdr.attr&MSG_PERMANENT)
					break;
				sprintf(str2,text[DeleteMailQ],msg.from);
				if(!yesno(str2))
					break;
				if(msg.total_hfields)
					smb_freemsgmem(&msg);
				msg.total_hfields=0;
				msg.idx.offset=0;
				if(smb_locksmbhdr(&smb)==SMB_SUCCESS) {	/* Lock the entire base */
					if(loadmsg(&msg,msg.idx.number) >= 0) {
						msg.hdr.attr|=MSG_DELETE;
						msg.idx.attr=msg.hdr.attr;
		//				  mail[smb.curmsg].attr=msg.hdr.attr;
						if((i=smb_putmsg(&smb,&msg))!=0)
							errormsg(WHERE,ERR_WRITE,smb.file,i,smb.last_error);
						smb_unlockmsghdr(&smb,&msg); 
					}
					smb_unlocksmbhdr(&smb);
				}

				break;
			case 'H':
				domsg=0;
				msghdr(&msg);
				break;
			case 'L':     /* List mail */
				domsg=0;
				bprintf(text[StartWithN],(long)smb.curmsg+1);
				if((i=getnum(smb.msgs))>0)
					i--;
				else if(i==-1)
					break;
				else
					i=smb.curmsg;
				if(which==MAIL_SENT)
					bprintf(text[MailSentLstHdr], order);
				else if(which==MAIL_ALL)
					bprintf(text[MailOnSystemLstHdr], order);
				else
					bprintf(text[MailWaitingLstHdr], order);
				for(u=i;u<smb.msgs && !msgabort();u++) {
					if(msg.total_hfields)
						smb_freemsgmem(&msg);
					msg.total_hfields=0;
					msg.idx.offset=mail[u].offset;
					if(loadmsg(&msg,mail[u].number) < 0)
						continue;
					smb_unlockmsghdr(&smb,&msg);
					if(which==MAIL_ALL)
						bprintf(text[MailOnSystemLstFmt]
							,u+1,msg.from,msg.to
							,mail_listing_flag(&msg)
							,msg.subj);
					else
						bprintf(text[MailWaitingLstFmt],u+1
							,which==MAIL_SENT ? msg.to
							: (msg.hdr.attr&MSG_ANONYMOUS) && !SYSOP
							? text[Anonymous] : msg.from
							,mail_listing_flag(&msg)
							,msg.subj);
					smb_freemsgmem(&msg);
					msg.total_hfields=0; 
				}
				break;
			case 'Q':
				done=1;
				break;
			case 'C':   /* Change attributes of last piece */
				i=chmsgattr(msg);
				if(msg.hdr.attr==i)
					break;
				if(msg.total_hfields)
					smb_freemsgmem(&msg);
				msg.total_hfields=0;
				msg.idx.offset=0;
				if(smb_locksmbhdr(&smb)==SMB_SUCCESS) {	/* Lock the entire base */
					if(loadmsg(&msg,msg.idx.number) >= 0) {
						msg.hdr.attr=msg.idx.attr=(ushort)i;
						if((i=smb_putmsg(&smb,&msg))!=0)
							errormsg(WHERE,ERR_WRITE,smb.file,i,smb.last_error);
						smb_unlockmsghdr(&smb,&msg); 
					}
					smb_unlocksmbhdr(&smb);
				}
				break;
			case '>':
				for(u=smb.curmsg+1;u<smb.msgs;u++)
					if(mail[u].subj==msg.idx.subj)
						break;
				if(u<smb.msgs)
					smb.curmsg=u;
				else {
					domsg=0;
					bputs(text[NoMessagesFound]);
				}
				break;
			case '<':   /* Search Title backward */
				for(i=smb.curmsg-1;i>-1;i--)
					if(mail[i].subj==msg.idx.subj)
						break;
				if(i>-1)
					smb.curmsg=i;
				else {
					domsg=0;
					bputs(text[NoMessagesFound]);
				}
				break;
			case ')':
			case '}':   /* Search Author forward */
				strcpy(str,msg.from);
				for(u=smb.curmsg+1;u<smb.msgs;u++)
					if(mail[u].from==msg.idx.from)
						break;
				if(u<smb.msgs)
					smb.curmsg=u;
				else {
					domsg=0;
					bputs(text[NoMessagesFound]);
				}
				break;
			case 'N':   /* Got to next un-read message */
				for(u=smb.curmsg+1;u<smb.msgs;u++)
					if(!(mail[u].attr&MSG_READ))
						break;
				if(u<smb.msgs)
					smb.curmsg=u;
				else {
					domsg=0;
					bputs(text[NoMessagesFound]);
				}
				break;
			case '(':
			case '{':   /* Search Author backward */
				strcpy(str,msg.from);
				if(smb.curmsg > 0) {
					for(u=smb.curmsg-1;;u--) {
						if(mail[u].from==msg.idx.from) {
							smb.curmsg=u;
							break;
						}
						if(u==0) {
							domsg=0;
							bputs(text[NoMessagesFound]);
							break;
						}
					}
				}
				break;
			case ']':   /* Search To User forward */
				strcpy(str,msg.to);
				for(u=smb.curmsg+1;u<smb.msgs;u++)
					if(mail[u].to==msg.idx.to)
						break;
				if(u<smb.msgs)
					smb.curmsg=u;
				else {
					domsg=0;
					bputs(text[NoMessagesFound]);
				}
				break;
			case '[':   /* Search To User backward */
				strcpy(str,msg.to);
				if(smb.curmsg > 0) {
					for(u=smb.curmsg-1;;u--) {
						if(mail[u].to==msg.idx.to) {
							smb.curmsg=u;
							break;
						}
						if(u==0) {
							domsg=0;
							bputs(text[NoMessagesFound]);
							break;
						}
					}
				}
				break;
			case 0:
			case '+':
				if(smb.curmsg<smb.msgs-1) smb.curmsg++;
				else done=1;
				break;
			case '-':
				if(smb.curmsg>0) smb.curmsg--;
				break;
			case 'S':
				domsg=0;
	/*
				if(!yesno(text[SaveMsgToFile]))
					break;
	*/
				bputs(text[FileToWriteTo]);
				if(getstr(str,50,K_LINE))
					msgtotxt(&msg,str, /* header: */true, /* mode: */GETMSGTXT_ALL);
				break;
			case 'E':
				editmsg(&msg,INVALID_SUB);
				break;
			case 'T':
				domsg=0;
				u=smb.curmsg;
				if(u) u++;
				v=u+10;
				if(v>smb.msgs)
					v=smb.msgs;

				if(which==MAIL_SENT)
					bprintf(text[MailSentLstHdr], order);
				else if(which==MAIL_ALL)
					bprintf(text[MailOnSystemLstHdr], order);
				else
					bprintf(text[MailWaitingLstHdr], order);
				for(;u<v;u++) {
					if(msg.total_hfields)
						smb_freemsgmem(&msg);
					msg.total_hfields=0;
					msg.idx.offset=mail[u].offset;
					if(loadmsg(&msg,mail[u].number) < 0)
						continue;
					smb_unlockmsghdr(&smb,&msg);
					if(which==MAIL_ALL)
						bprintf(text[MailOnSystemLstFmt]
							,u+1,msg.from,msg.to
							,mail_listing_flag(&msg)
							,msg.subj);
					else
						bprintf(text[MailWaitingLstFmt],u+1
							,which==MAIL_SENT ? msg.to
							: (msg.hdr.attr&MSG_ANONYMOUS) && !SYSOP
							? text[Anonymous] : msg.from
							,mail_listing_flag(&msg)
							,msg.subj);
					smb_freemsgmem(&msg);
					msg.total_hfields=0; 
				}
				smb.curmsg=(u-1);
				break;
			case 'U':   /* user edit */
				msg.hdr.number=msg.idx.number;
				smb_getmsgidx(&smb,&msg);
				unum = msg.idx.from;
				if(unum == 0)
					unum = matchuser(&cfg, msg.from, /*sysop_alias: */FALSE);
				if(unum == 0 && which != MAIL_YOUR)
					unum = msg.idx.to;
				if(unum == 0 || unum > lastuser(&cfg)) {
					bputs(text[UnknownUser]);
					domsg=false;
				} else
					useredit(unum);
				break;
#if 0
			case 'U':	/* View Unread-Only (toggle) */
			{
				domsg = false;
				if(!(lm_mode&LM_UNREAD)) {
					if(getmail(&cfg, usernumber, /* Sent: */FALSE, /* attr: */0)
						== getmail(&cfg, usernumber, /* Sent: */FALSE, /* attr: */MSG_READ)) {
						bprintf(text[NoMailWaiting], "un-read mail");
						break;
					}
				}
				lm_mode ^= LM_UNREAD;
				bprintf("%s: %s"
					,text[DisplayUnreadMessagesOnlyQ]
					,(lm_mode&LM_UNREAD) ? text[On] : text[Off]);
				CRLF;
				break;
			}
#endif
			case 'V':	/* View SPAM (toggle) */
			{
				domsg = false;
				int spam = getmail(&cfg, usernumber, /* Sent: */FALSE, /* attr: */MSG_SPAM);
				if(!spam) {
					bprintf(text[NoMailWaiting], "SPAM");
					break;
				}
				if(spam >= getmail(&cfg, usernumber, /* Sent: */FALSE, /* attr: */0)) {
					bprintf(text[NoMailWaiting], "HAM");
					break;
				}
				bputs(text[SPAMVisibilityIsNow]);
				switch(lm_mode&(LM_SPAMONLY | LM_NOSPAM)) {
					case 0:
						lm_mode |= LM_NOSPAM;
						bputs(text[Off]);
						break;
					case LM_NOSPAM:
						lm_mode ^= (LM_SPAMONLY | LM_NOSPAM);
						bputs(text[Only]);
						break;
					case LM_SPAMONLY:
						lm_mode &= ~LM_SPAMONLY;
						bputs(text[On]);
						break;
				}
				CRLF;
				break;
			}
			case 'P':   /* Purge author and all mail to/from */
				if(noyes(text[UeditDeleteQ]))
					break;
				msg.hdr.number=msg.idx.number;
				smb_getmsgidx(&smb,&msg);
				if((which==MAIL_SENT ? msg.idx.to : msg.idx.from) == 0) {
					bputs(text[UnknownUser]);
					domsg=false;
				} else {
					purgeuser(msg.idx.from);
					if(smb.curmsg<smb.msgs-1) smb.curmsg++;
				}
				break;
			case '/':
				domsg = false;
				int64_t i64;
				if((i64=get_start_msgnum(&smb))<0)
					break;
				bputs(text[SearchStringPrompt]);
				if(!getstr(search_str,40,K_LINE|K_UPPER|K_EDIT|K_AUTODEL))
					break;
				searchmail(mail, (long)i64, smb.msgs, which, search_str, order);
				break;
			case '?':
				menu(menu_file);
				if(SYSOP && which==MAIL_SENT)
					menu("syssmail");
				else if(SYSOP && which==MAIL_YOUR)
					menu("sysmailr");   /* Sysop Mail Read */
				domsg=0;
				break;
				
		} 
	}

	if(msg.total_hfields)
		smb_freemsgmem(&msg);

	if(smb.msgs)
		free(mail);

	/***************************************/
	/* Delete messages marked for deletion */
	/***************************************/

	if(cfg.sys_misc&SM_DELEMAIL) {
		if((i=smb_locksmbhdr(&smb))!=0) 			/* Lock the base, so nobody */
			errormsg(WHERE,ERR_LOCK,smb.file,i,smb.last_error);	/* messes with the index */
		else
			delmail(usernumber,which); 
	}

	smb_close(&smb);
	smb_stack(&smb,SMB_STACK_POP);
	current_msg=NULL;
}

long sbbs_t::searchmail(mail_t *mail, long start, long msgs, int which, const char *search, const char* order)
{
	char*	buf;
	char	subj[128];
	long	l,found=0;
	smbmsg_t msg;

	msg.total_hfields=0;
	for(l=start;l<msgs && !msgabort();l++) {
		msg.idx.offset=mail[l].offset;
		if(loadmsg(&msg,mail[l].number) < 0)
			continue;
		smb_unlockmsghdr(&smb,&msg);
		buf=smb_getmsgtxt(&smb,&msg,GETMSGTXT_ALL);
		if(!buf) {
			smb_freemsgmem(&msg);
			continue; 
		}
		strupr(buf);
		strip_ctrl(buf, buf);
		SAFECOPY(subj,msg.subj);
		strupr(subj);
		if(strstr(buf,search) || strstr(subj,search)) {
			if(!found) {
				if(which==MAIL_SENT)
					bprintf(text[MailSentLstHdr], order);
				else if(which==MAIL_ALL)
					bprintf(text[MailOnSystemLstHdr], order);
				else
					bprintf(text[MailWaitingLstHdr], order);
			}
			if(which==MAIL_ALL)
				bprintf(text[MailOnSystemLstFmt]
					,l+1,msg.from,msg.to
					,mail_listing_flag(&msg)
					,msg.subj);
			else
				bprintf(text[MailWaitingLstFmt],l+1
					,which==MAIL_SENT ? msg.to
					: (msg.hdr.attr&MSG_ANONYMOUS) && !SYSOP
					? text[Anonymous] : msg.from
					,mail_listing_flag(&msg)
					,msg.subj);
			found++; 
		}
		free(buf);
		smb_freemsgmem(&msg); 
	}

	return(found);
}
