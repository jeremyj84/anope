/*
 *   IRC - Internet Relay Chat, db-merger.c
 *   (C) Copyright 2005-2006, Florian Schulze (Certus)
 *
 *   Based on the original code of Anope, (C) 2003-2005 Anope Team
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License (see it online
 *   at http://www.gnu.org/copyleft/gpl.html) as published by the Free
 *   Software Foundation;
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   This program tries to merge two standard anope-1.7.7+ database sets,
 *   that includes chanserv, nickserv, botserv and memoserv databases.
 *
 *   - Certus
 *	  February 11, 2005
 *
 *
 *   0.2 beta:
 *   Fixed some overflows in the ->flags and the ->status fields. Use db-fix
 *   to fix your broken nick dbs.
 *
 *   - Certus
 *	  August 04, 2005
 *
 *
 *   0.3 beta:
 *   Fixed win32 stuff. Merging should now work fine under windows.
 *
 *   - Certus
 *	  July 20, 2006
 *
 *
 *   0.4 beta:
 *   Fixed some compiler warnings about signed/unsigned ints.
 *
 *   - Certus
 *	  August 8, 2006

 */


#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <ctype.h>
#include <time.h>

#ifndef _WIN32
#include <unistd.h>
#else
#include "sysconf.h"
#include <windows.h>
#endif

/* Some SUN fixs */
#ifdef __sun
/* Solaris specific code, types that do not exist in Solaris'
 * sys/types.h
 **/
#undef u_int8_t
#undef u_int16_t
#undef u_int32_t
#undef u_int_64_t
#define u_int8_t uint8_t
#define u_int16_t uint16_t
#define u_int32_t uint32_t
#define u_int64_t uint64_t

#ifndef INADDR_NONE
#define INADDR_NONE (-1)
#endif

#endif


#include <fstream>

#ifndef _WIN32
#define C_LBLUE "\033[1;34m"
#define C_NONE "\033[m"
#else
#define C_LBLUE ""
#define C_NONE ""
#endif

#define getc_db(f)		(fgetc((f)->fp))
#define HASH(nick)	((tolower((nick)[0])&31)<<5 | (tolower((nick)[1])&31))
#define HASH2(chan)	((chan)[1] ? ((chan)[1]&31)<<5 | ((chan)[2]&31) : 0)
#define read_buffer(buf,f)	(read_db((f),(buf),sizeof(buf)) == sizeof(buf))
#define write_buffer(buf,f)	(write_db((f),(buf),sizeof(buf)) == sizeof(buf))
#define read_db(f,buf,len)	(fread((buf),1,(len),(f)->fp))
#define write_db(f,buf,len)	(fwrite((buf),1,(len),(f)->fp))
#define read_int8(ret,f)	((*(ret)=fgetc((f)->fp))==EOF ? -1 : 0)
#define write_int8(val,f)	(fputc((val),(f)->fp)==EOF ? -1 : 0)
#define SAFE(x) do { \
	if ((x) < 0) { \
	printf("Error, the database is broken, trying to continue... no guarantee.\n"); \
	} \
} while (0)
#define READ(x) do { \
	if ((x) < 0) { \
		printf("Error, the database is broken, trying to continue... no guarantee.\n"); \
		exit(0); \
	} \
} while (0)

typedef int16_t int16;
typedef u_int16_t uint16;
typedef int32_t int32;
typedef u_int32_t uint32;
typedef struct memo_ Memo;
typedef struct dbFILE_ dbFILE;
typedef struct nickalias_ NickAlias;
typedef struct nickcore_ NickCore;
typedef struct chaninfo_ ChannelInfo;
typedef struct botinfo_ BotInfo;
typedef struct badword_ BadWord;
typedef struct hostcore_ HostCore;

struct memo_ {
	uint32 number;	  /* Index number -- not necessarily array position! */
	uint16 flags;			/* Flags */
	time_t time;		  /* When was it sent? */
	char sender[32];   /* Name of the sender */
	char *text;
};

struct dbFILE_ {
	int mode;				   /* 'r' for reading, 'w' for writing */
	FILE *fp;					/* The normal file descriptor */
	char filename[1024];  /* Name of the database file */
};

typedef struct {
	int16 memocount;  /* Current # of memos */
	int16 memomax;	/* Max # of memos one can hold*/
	Memo *memos;	 /* Pointer to original memos */
} MemoInfo;

typedef struct {
	uint16 in_use;		 /* 1 if this entry is in use, else 0 */
	int16 level;
	NickCore *nc;	   /* Guaranteed to be non-NULL if in use, NULL if not */
	time_t last_seen;
} ChanAccess;

typedef struct {
	int16 in_use;		 /* Always 0 if not in use */
	int16 is_nick;		 /* 1 if a regged nickname, 0 if a nick!user@host mask */
	uint16 flags;
	union {
		char *mask;	  /* Guaranteed to be non-NULL if in use, NULL if not */
		NickCore *nc;   /* Same */
	} u;
	char *reason;
	char *creator;
	time_t addtime;
} AutoKick;

struct nickalias_ {
	NickAlias *next, *prev;
	char *nick;					/* Nickname */
	time_t time_registered;  /* When the nick was registered */
	time_t last_seen;		   /* When it was seen online for the last time */
	uint16 status;				  /* See NS_* below */
	NickCore *nc;			   /* I'm an alias of this */
};

struct nickcore_ {
	NickCore *next, *prev;

	char *display;		   /* How the nick is displayed */
	char pass[32];			   /* Password of the nicks */
	char *email;			  /* E-mail associated to the nick */
	char *greet;			  /* Greet associated to the nick */
	uint32 icq;				 /* ICQ # associated to the nick */
	char *url;				  /* URL associated to the nick */
	uint32 flags;				/* See NI_* below */
	uint16 language;		/* Language selected by nickname owner (LANG_*) */
	uint16 accesscount;	 /* # of entries */
	char **access;		  /* Array of strings */
	MemoInfo memos;	 /* Memo information */
	uint16 channelcount;  /* Number of channels currently registered */
	int unused;				/* Used for nick collisions */
	int aliascount;			/* How many aliases link to us? Remove the core if 0 */

	char *last_quit;			 /* Last quit message */
	char *last_realname;	 /* Last realname */
	char *last_usermask;	 /* Last usermask */
};

struct chaninfo_ {
	ChannelInfo *next, *prev;

	char name[64];					 /* Channel name */
	char *founder;					  /* Who registered the channel */
	char *successor;				   /* Who gets the channel if the founder nick is dropped or expires */
	char founderpass[32];			/* Channel password */
	char *desc;						   /* Description */
	char *url;							  /* URL */
	char *email;						  /* Email address */
	time_t time_registered;		  /* When was it registered */
	time_t last_used;				   /* When was it used hte last time */
	char *last_topic;						/* Last topic on the channel */
	char last_topic_setter[32];	  /* Who set the last topic */
	time_t last_topic_time;		   /* When the last topic was set */
	uint32 flags;						  /* Flags */
	char *forbidby;					  /* if forbidden: who did it */
	char *forbidreason;				/* if forbidden: why */
	int16 bantype;					   /* Bantype */
	int16 *levels;						/* Access levels for commands */
	uint16 accesscount;				 /* # of pple with access */
	ChanAccess *access;			 /* List of authorized users */
	uint16 akickcount;					/* # of akicked pple */
	AutoKick *akick;					/* List of users to kickban */
	uint32 mlock_on, mlock_off;   /* See channel modes below */
	uint32 mlock_limit;				/* 0 if no limit */
	char *mlock_key;				  /* NULL if no key */
	char *mlock_flood;					/* NULL if no +f */
	char *mlock_redirect;			/* NULL if no +L */
	char *entry_message;		   /* Notice sent on entering channel */
	MemoInfo memos;				 /* Memos */
	char *bi;							   /* Bot used on this channel */
	uint32 botflags;					  /* BS_* below */
	int16 *ttb;							 /* Times to ban for each kicker */
	uint16 bwcount;					   /* Badword count */
	BadWord *badwords;			 /* For BADWORDS kicker */
	int16 capsmin, capspercent;	/* For CAPS kicker */
	int16 floodlines, floodsecs;	  /* For FLOOD kicker */
	int16 repeattimes;				 /* For REPEAT kicker */
};

struct botinfo_ {
 	BotInfo *next, *prev;
 	char *nick;			/* Nickname of the bot */
 	char *user;			/* Its user name */
 	char *host;			/* Its hostname */
 	char *real;			/* Its real name */
 	int16 flags;			/* Bot flags */
 	time_t created;	  /* Birth date */
	int16 chancount;		/* Number of channels that use the bot. */
};

struct badword_ {
	uint16 in_use;
	char *word;
	uint16 type;	
};

struct hostcore_ {
	HostCore *next, *last;
	char *nick;		/* Owner of the vHost */
	char *vIdent;	/* vIdent for the user */
	char *vHost;	 /* Vhost for this user */
	char *creator;   /* Oper Nick of the oper who set the vhost */
	time_t time;	  /* Date/Time vHost was set */
};

dbFILE *open_db_write(const char *service, const char *filename, int version);
dbFILE *open_db_read(const char *service, const char *filename, int version);
NickCore *findcore(const char *nick, int version);
NickAlias *findnick(const char *nick);
BotInfo *findbot(char *nick);
ChannelInfo *cs_findchan(const char *chan);
char *strscpy(char *d, const char *s, size_t len);
int write_file_version(dbFILE * f, uint32 version);
int mystricmp(const char *s1, const char *s2);
int delnick(NickAlias *na, int donttouchthelist);
int write_string(const char *s, dbFILE * f);
int write_ptr(const void *ptr, dbFILE * f);
int read_int16(int16 * ret, dbFILE * f);
int read_int32(int32 * ret, dbFILE * f);
int read_uint16(uint16 * ret, dbFILE * f);
int read_uint32(uint32 * ret, dbFILE * f);
int read_string(char **ret, dbFILE * f);
int write_int16(uint16 val, dbFILE * f);
int write_int32(uint32 val, dbFILE * f);
int read_ptr(void **ret, dbFILE * f);
int delcore(NickCore *nc);
void alpha_insert_chan(ChannelInfo * ci);
void insert_bot(BotInfo * bi);
void close_db(dbFILE * f);

ChannelInfo *chanlists[256];
NickAlias *nalists[1024];
NickCore *nclists[1024];
BotInfo *botlists[256];

int main(int argc, char *argv[])
{
	dbFILE *f;
	HostCore *firsthc = NULL;
	std::ofstream fs;

	printf("\n"C_LBLUE"Anope 1.8.x -> 1.9.x database converter"C_NONE"\n\n");

	fs.open("anope.db");
	if (!fs.is_open())
	{
		printf("\n"C_LBLUE"Could not open anope.db for write"C_NONE"\n\n");
		exit(1);	
	}
	
	// VERSHUN ONE
	fs << "VER 1" << std::endl;

	/* Section I: Nicks */
	/* Ia: First database */
	if ((f = open_db_read("NickServ", "nick.db", 14)))
	{

		NickAlias *na, **nalast, *naprev;
		NickCore *nc, **nclast, *ncprev;
		int16 tmp16;
		int32 tmp32;
		int i, j, c;

		printf("Trying to merge nicks...\n");

		/* Nick cores */
		for (i = 0; i < 1024; i++) {
			nclast = &nclists[i];
			ncprev = NULL;

			while ((c = getc_db(f)) == 1) {
				if (c != 1) {
					printf("Invalid format in nickserv db.\n");
					exit(0);
				}

				nc = (NickCore *)calloc(1, sizeof(NickCore));
				nc->aliascount = 0;
				nc->unused = 0;

				*nclast = nc;
				nclast = &nc->next;
				nc->prev = ncprev;
				ncprev = nc;

				READ(read_string(&nc->display, f));
				READ(read_buffer(nc->pass, f));
				READ(read_string(&nc->email, f));
				READ(read_string(&nc->greet, f));
				READ(read_uint32(&nc->icq, f));
				READ(read_string(&nc->url, f));
				READ(read_uint32(&nc->flags, f));
				READ(read_uint16(&nc->language, f));
				READ(read_uint16(&nc->accesscount, f));
				if (nc->accesscount) {
					char **access;
					access = (char **)calloc(sizeof(char *) * nc->accesscount, 1);
					nc->access = access;
					for (j = 0; j < nc->accesscount; j++, access++)
						READ(read_string(access, f));
				}
				READ(read_int16(&nc->memos.memocount, f));
				READ(read_int16(&nc->memos.memomax, f));
				if (nc->memos.memocount) {
					Memo *memos;
					memos = (Memo *)calloc(sizeof(Memo) * nc->memos.memocount, 1);
					nc->memos.memos = memos;
					for (j = 0; j < nc->memos.memocount; j++, memos++) {
						READ(read_uint32(&memos->number, f));
						READ(read_uint16(&memos->flags, f));
						READ(read_int32(&tmp32, f));
						memos->time = tmp32;
						READ(read_buffer(memos->sender, f));
						READ(read_string(&memos->text, f));
					}
				}
				READ(read_uint16(&nc->channelcount, f));
				READ(read_int16(&tmp16, f));
			} /* getc_db() */
			*nclast = NULL;
		} /* for() loop */

		/* Nick aliases */
		for (i = 0; i < 1024; i++) {
			char *s = NULL;

			nalast = &nalists[i];
			naprev = NULL;

			while ((c = getc_db(f)) == 1) {
				if (c != 1) {
					printf("Invalid format in nick db.\n");
					exit(0);
				}

				na = (NickAlias *)calloc(1, sizeof(NickAlias));

				READ(read_string(&na->nick, f));
				char *mask;
				char *real;
				char *quit;
				READ(read_string(&mask, f));
				READ(read_string(&real, f));
				READ(read_string(&quit, f));

				READ(read_int32(&tmp32, f));
				na->time_registered = tmp32;
				READ(read_int32(&tmp32, f));
				na->last_seen = tmp32;
				READ(read_uint16(&na->status, f));
				READ(read_string(&s, f));
				na->nc = findcore(s, 0);
				na->nc->aliascount++;
				free(s);
				
				if (!na->nc->last_quit && quit)
					na->nc->last_quit = strdup(quit);
				if (!na->nc->last_realname && real)
					na->nc->last_realname = strdup(real);
				if (!na->nc->last_usermask && mask)
					na->nc->last_usermask = strdup(mask);
				
				// Convert nick NOEXPIRE to group NOEXPIRE	
				if (na->status & 0x0004)
				{
					na->nc->flags & 0x00100000;
				}
				
				// Convert nick VERBOTEN to group FORBIDDEN
				if (na->status & 0x0002)
				{
					na->nc->flags & 0x80000000;	
				}
				
				free(mask);
				free(real);
				free(quit);

				*nalast = na;
				nalast = &na->next;
				na->prev = naprev;
				naprev = na;
			} /* getc_db() */
			*nalast = NULL;
		} /* for() loop */
		close_db(f); /* End of section Ia */
	}

	/* CLEAN THE CORES */
	int i;
	for (i = 0; i < 1024; i++) {
		NickCore *ncnext;
		for (NickCore *nc = nclists[i]; nc; nc = ncnext) {
			ncnext = nc->next;
			if (nc->aliascount < 1) {
				printf("Deleting core %s (%s).\n", nc->display, nc->email); 
				delcore(nc);
			}
		}
	}

	/* Nick cores */
	for (i = 0; i < 1024; i++)
	{
		NickAlias *na;
		NickCore *nc;
		char **access;
		Memo *memos;
		int j;
		for (nc = nclists[i]; nc; nc = nc->next)
		{
			fs << "NC " << nc->display << std::endl;
/*
			SAFE(write_int8(1, f));
			SAFE(write_string(nc->display, f));
			SAFE(write_buffer(nc->pass, f));
			SAFE(write_string(nc->email, f));
			SAFE(write_string(nc->greet, f));
			SAFE(write_int32(nc->icq, f));
			SAFE(write_string(nc->url, f));
			SAFE(write_int32(nc->flags, f));
			SAFE(write_int16(nc->language, f));
			SAFE(write_int16(nc->accesscount, f));
			for (j = 0, access = nc->access; j < nc->accesscount; j++, access++)
				SAFE(write_string(*access, f));

			SAFE(write_int16(nc->memos.memocount, f));
			SAFE(write_int16(nc->memos.memomax, f));
			memos = nc->memos.memos;
			for (j = 0; j < nc->memos.memocount; j++, memos++) {
				SAFE(write_int32(memos->number, f));
				SAFE(write_int16(memos->flags, f));
				SAFE(write_int32(memos->time, f));
				SAFE(write_buffer(memos->sender, f));
				SAFE(write_string(memos->text, f));
			}
			SAFE(write_int16(nc->channelcount, f));
			SAFE(write_int16(nc->channelcount, f)); // BK with anope1.7, hack alert XXX
*/
		} /* for (nc) */
	} /* for (i) */

	/* Nick aliases */
	/*for (i = 0; i < 1024; i++) {
		for (na = nalists[i]; na; na = na->next) {
			SAFE(write_int8(1, f));
			SAFE(write_string(na->nick, f));
			SAFE(write_string(na->last_usermask, f));
			SAFE(write_string(na->last_realname, f));
			SAFE(write_string(na->last_quit, f));
			SAFE(write_int32(na->time_registered, f));
			SAFE(write_int32(na->last_seen, f));
			SAFE(write_int16(na->status, f));
			SAFE(write_string(na->nc->display, f));

		}
	}
	*/


	/* Section II: Chans */
	/* IIa: First database 
	if ((f = open_db_read("ChanServ", "chan.db", 16)))
	{
		ChannelInfo *ci, **last, *prev;
		int c;

		printf("Trying to merge channels...\n");

		for (i = 0; i < 256; i++) {
			int16 tmp16;
			int32 tmp32;
			int n_levels;
			char *s;
			int n_ttb;

			last = &chanlists[i];
			prev = NULL;
		
			while ((c = getc_db(f)) == 1) {
				int j;

				if (c != 1) {
					printf("Invalid format in %s.\n", CHAN_DB_1);
					exit(0);
				}

				ci = (ChannelInfo *)calloc(sizeof(ChannelInfo), 1);
				*last = ci;
				last = &ci->next;
				ci->prev = prev;
				prev = ci;
				READ(read_buffer(ci->name, f));
				READ(read_string(&ci->founder, f));
				READ(read_string(&ci->successor, f));
				READ(read_buffer(ci->founderpass, f));
				READ(read_string(&ci->desc, f));
				if (!ci->desc)
					ci->desc = strdup("");
				READ(read_string(&ci->url, f));
				READ(read_string(&ci->email, f));
				READ(read_int32(&tmp32, f));
				ci->time_registered = tmp32;
				READ(read_int32(&tmp32, f));
				ci->last_used = tmp32;
				READ(read_string(&ci->last_topic, f));
				READ(read_buffer(ci->last_topic_setter, f));
				READ(read_int32(&tmp32, f));
				ci->last_topic_time = tmp32;
				READ(read_uint32(&ci->flags, f));
				// Temporary flags cleanup
				ci->flags &= ~0x80000000;
				READ(read_string(&ci->forbidby, f));
				READ(read_string(&ci->forbidreason, f));
				READ(read_int16(&tmp16, f));
				ci->bantype = tmp16;
				READ(read_int16(&tmp16, f));
				n_levels = tmp16;
				ci->levels = (int16 *)calloc(36 * sizeof(*ci->levels), 1);
				for (j = 0; j < n_levels; j++) {
					if (j < 36)
						READ(read_int16(&ci->levels[j], f));
					else
						READ(read_int16(&tmp16, f));
				}
				READ(read_uint16(&ci->accesscount, f));
				if (ci->accesscount) {
					ci->access = (ChanAccess *)calloc(ci->accesscount, sizeof(ChanAccess));
					for (j = 0; j < ci->accesscount; j++) {
						READ(read_uint16(&ci->access[j].in_use, f));
						if (ci->access[j].in_use) {
							READ(read_int16(&ci->access[j].level, f));
							READ(read_string(&s, f));
							if (s) {
								ci->access[j].nc = findcore(s, 0);
								free(s);
							}
							if (ci->access[j].nc == NULL)
								ci->access[j].in_use = 0;
							READ(read_int32(&tmp32, f));
							ci->access[j].last_seen = tmp32;
						}
					}
				} else {
					ci->access = NULL;
				}
				READ(read_uint16(&ci->akickcount, f));
				if (ci->akickcount) {
					ci->akick = (AutoKick *)calloc(ci->akickcount, sizeof(AutoKick));
					for (j = 0; j < ci->akickcount; j++) {
						SAFE(read_uint16(&ci->akick[j].flags, f));
						if (ci->akick[j].flags & 0x0001) {
							SAFE(read_string(&s, f));
							if (ci->akick[j].flags & 0x0002) {
								ci->akick[j].u.nc = findcore(s, 0);
								if (!ci->akick[j].u.nc)
									ci->akick[j].flags &= ~0x0001;
								free(s);
							} else {
								ci->akick[j].u.mask = s;
							}
							SAFE(read_string(&s, f));
							if (ci->akick[j].flags & 0x0001)
								ci->akick[j].reason = s;
							else if (s)
								free(s);
							SAFE(read_string(&s, f));
							if (ci->akick[j].flags & 0x0001) {
								ci->akick[j].creator = s;
							} else if (s) {
								free(s);
							}
							SAFE(read_int32(&tmp32, f));
							if (ci->akick[j].flags & 0x0001)
								ci->akick[j].addtime = tmp32;
						}
					}
				} else {
					ci->akick = NULL;
				}
				READ(read_uint32(&ci->mlock_on, f));
				READ(read_uint32(&ci->mlock_off, f));
				READ(read_uint32(&ci->mlock_limit, f));
				READ(read_string(&ci->mlock_key, f));
				READ(read_string(&ci->mlock_flood, f));
				READ(read_string(&ci->mlock_redirect, f));
				READ(read_int16(&ci->memos.memocount, f));
				READ(read_int16(&ci->memos.memomax, f));
				if (ci->memos.memocount) {
					Memo *memos;
					memos = (Memo *)calloc(sizeof(Memo) * ci->memos.memocount, 1);
					ci->memos.memos = memos;
					for (j = 0; j < ci->memos.memocount; j++, memos++) {
						READ(read_uint32(&memos->number, f));
						READ(read_uint16(&memos->flags, f));
						READ(read_int32(&tmp32, f));
						memos->time = tmp32;
						READ(read_buffer(memos->sender, f));
						READ(read_string(&memos->text, f));
					}
				}
				READ(read_string(&ci->entry_message, f));

				// BotServ options
				READ(read_string(&ci->bi, f));
				READ(read_int32(&tmp32, f));
				ci->botflags = tmp32;
				READ(read_int16(&tmp16, f));
				n_ttb = tmp16;
				ci->ttb = (int16 *)calloc(2 * 8, 1);
				for (j = 0; j < n_ttb; j++) {
					if (j < 8)
						READ(read_int16(&ci->ttb[j], f));
					else
						READ(read_int16(&tmp16, f));
				}
				for (j = n_ttb; j < 8; j++)
					ci->ttb[j] = 0;
				READ(read_int16(&tmp16, f));
				ci->capsmin = tmp16;
				READ(read_int16(&tmp16, f));
				ci->capspercent = tmp16;
				READ(read_int16(&tmp16, f));
				ci->floodlines = tmp16;
				READ(read_int16(&tmp16, f));
				ci->floodsecs = tmp16;
				READ(read_int16(&tmp16, f));
				ci->repeattimes = tmp16;

				READ(read_uint16(&ci->bwcount, f));
				if (ci->bwcount) {
					ci->badwords = (BadWord *)calloc(ci->bwcount, sizeof(BadWord));
					for (j = 0; j < ci->bwcount; j++) {
						SAFE(read_uint16(&ci->badwords[j].in_use, f));
						if (ci->badwords[j].in_use) {
							SAFE(read_string(&ci->badwords[j].word, f));
							SAFE(read_uint16(&ci->badwords[j].type, f));
						}
					}
				} else {
					ci->badwords = NULL;
				}
			}
			*last = NULL;
		}
		close_db(f);
	}*/
	
	#if 0

	/* IIc: Saving */
	if ((f = open_db_write("ChanServ", CHAN_DB_NEW, 16))) {
		ChannelInfo *ci;
		Memo *memos;

		for (i = 0; i < 256; i++) {
			int16 tmp16;
			for (ci = chanlists[i]; ci; ci = ci->next) {
				int j;
				SAFE(write_int8(1, f));
				SAFE(write_buffer(ci->name, f));
				if (ci->founder)
					SAFE(write_string(ci->founder, f));
				else
					SAFE(write_string(NULL, f));
				if (ci->successor)
					SAFE(write_string(ci->successor, f));
				else
					SAFE(write_string(NULL, f));
				SAFE(write_buffer(ci->founderpass, f));
				SAFE(write_string(ci->desc, f));
				SAFE(write_string(ci->url, f));
				SAFE(write_string(ci->email, f));
				SAFE(write_int32(ci->time_registered, f));
				SAFE(write_int32(ci->last_used, f));
				SAFE(write_string(ci->last_topic, f));
				SAFE(write_buffer(ci->last_topic_setter, f));
				SAFE(write_int32(ci->last_topic_time, f));
				SAFE(write_int32(ci->flags, f));
				SAFE(write_string(ci->forbidby, f));
				SAFE(write_string(ci->forbidreason, f));
				SAFE(write_int16(ci->bantype, f));
				tmp16 = 36;
				SAFE(write_int16(tmp16, f));
				for (j = 0; j < 36; j++)
					SAFE(write_int16(ci->levels[j], f));

				SAFE(write_int16(ci->accesscount, f));
				for (j = 0; j < ci->accesscount; j++) {
					SAFE(write_int16(ci->access[j].in_use, f));
					if (ci->access[j].in_use) {
						SAFE(write_int16(ci->access[j].level, f));
						SAFE(write_string(ci->access[j].nc->display, f));
						SAFE(write_int32(ci->access[j].last_seen, f));
					}
				}
				SAFE(write_int16(ci->akickcount, f));
				for (j = 0; j < ci->akickcount; j++) {
					SAFE(write_int16(ci->akick[j].flags, f));
					if (ci->akick[j].flags & 0x0001) {
						if (ci->akick[j].flags & 0x0002)
							SAFE(write_string(ci->akick[j].u.nc->display, f));
						else
							SAFE(write_string(ci->akick[j].u.mask, f));
						SAFE(write_string(ci->akick[j].reason, f));
						SAFE(write_string(ci->akick[j].creator, f));
						SAFE(write_int32(ci->akick[j].addtime, f));
					}
				}

				SAFE(write_int32(ci->mlock_on, f));
				SAFE(write_int32(ci->mlock_off, f));
				SAFE(write_int32(ci->mlock_limit, f));
				SAFE(write_string(ci->mlock_key, f));
				SAFE(write_string(ci->mlock_flood, f));
				SAFE(write_string(ci->mlock_redirect, f));
				SAFE(write_int16(ci->memos.memocount, f));
				SAFE(write_int16(ci->memos.memomax, f));
				memos = ci->memos.memos;
				for (j = 0; j < ci->memos.memocount; j++, memos++) {
					SAFE(write_int32(memos->number, f));
					SAFE(write_int16(memos->flags, f));
					SAFE(write_int32(memos->time, f));
					SAFE(write_buffer(memos->sender, f));
					SAFE(write_string(memos->text, f));
				}
				SAFE(write_string(ci->entry_message, f));
				if (ci->bi)
					SAFE(write_string(ci->bi, f));
				else
					SAFE(write_string(NULL, f));
				SAFE(write_int32(ci->botflags, f));
				tmp16 = 8;
				SAFE(write_int16(tmp16, f));
				for (j = 0; j < 8; j++)
					SAFE(write_int16(ci->ttb[j], f));
				SAFE(write_int16(ci->capsmin, f));
				SAFE(write_int16(ci->capspercent, f));
				SAFE(write_int16(ci->floodlines, f));
				SAFE(write_int16(ci->floodsecs, f));
				SAFE(write_int16(ci->repeattimes, f));

				SAFE(write_int16(ci->bwcount, f));
				for (j = 0; j < ci->bwcount; j++) {
					SAFE(write_int16(ci->badwords[j].in_use, f));
					if (ci->badwords[j].in_use) {
						SAFE(write_string(ci->badwords[j].word, f));
						SAFE(write_int16(ci->badwords[j].type, f));
					}
				}
			} /* for (chanlists[i]) */
			SAFE(write_int8(0, f));
		} /* for (i) */
		close_db(f);
		printf("Chan merging done. New database saved as %s.\n", CHAN_DB_NEW);
	}

	/* Section III: Bots */
	/* IIIa: First database */
	if ((f = open_db_read("Botserv", BOT_DB_1, 10))) {
		BotInfo *bi;
		int c;
		int32 tmp32;
		int16 tmp16;

		printf("Trying to merge bots...\n");

		while ((c = getc_db(f)) == 1) {
			if (c != 1) {
				printf("Invalid format in %s.\n", BOT_DB_1);
				exit(0);
			}

			bi = (BotInfo *)calloc(sizeof(BotInfo), 1);
			READ(read_string(&bi->nick, f));
			READ(read_string(&bi->user, f));
			READ(read_string(&bi->host, f));
			READ(read_string(&bi->real, f));
			SAFE(read_int16(&tmp16, f));
			bi->flags = tmp16;
			READ(read_int32(&tmp32, f));
			bi->created = tmp32;
			READ(read_int16(&tmp16, f));
			bi->chancount = tmp16;
			insert_bot(bi);
		}
	}

	/* IIIc: Saving */
	if ((f = open_db_write("Botserv", BOT_DB_NEW, 10))) {
		BotInfo *bi;
		for (i = 0; i < 256; i++) {
			for (bi = botlists[i]; bi; bi = bi->next) {
				SAFE(write_int8(1, f));
				SAFE(write_string(bi->nick, f));
				SAFE(write_string(bi->user, f));
				SAFE(write_string(bi->host, f));
				SAFE(write_string(bi->real, f));
				SAFE(write_int16(bi->flags, f));
				SAFE(write_int32(bi->created, f));
				SAFE(write_int16(bi->chancount, f));
			}
		}
		SAFE(write_int8(0, f));
		close_db(f);
		printf("Bot merging done. New database saved as %s.\n", BOT_DB_NEW);
	}

	/* Section IV: Hosts */
	/* IVa: First database */
	if ((f = open_db_read("HostServ", HOST_DB_1, 3))) {
		HostCore *hc;
		int c;
		int32 tmp32;

		printf("Trying to merge hosts...\n");

		while ((c = getc_db(f)) == 1) {
			if (c != 1) {
				printf("Invalid format in %s.\n", HOST_DB_1);
				exit(0);
			}
			hc = (HostCore *)calloc(1, sizeof(HostCore));
			READ(read_string(&hc->nick, f));
			READ(read_string(&hc->vIdent, f));
			READ(read_string(&hc->vHost, f));
			READ(read_string(&hc->creator, f));
			READ(read_int32(&tmp32, f));
			hc->time = tmp32;
			hc->next = firsthc;
			if (firsthc)
				firsthc->last = hc;
			hc->last = NULL;
			firsthc = hc;
		}
	}
	
	if ((f = open_db_write("HostServ", HOST_DB_NEW, 3))) {
		HostCore *hcptr;
		for (hcptr = firsthc; hcptr; hcptr = hcptr->next) {
			SAFE(write_int8(1, f));
			SAFE(write_string(hcptr->nick, f));
			SAFE(write_string(hcptr->vIdent, f));
			SAFE(write_string(hcptr->vHost, f));
			SAFE(write_string(hcptr->creator, f));
			SAFE(write_int32(hcptr->time, f));
		}
		SAFE(write_int8(0, f));
		close_db(f);
		printf("Host merging done. New database saved as %s.\n", HOST_DB_NEW);
	}

#endif
	/* MERGING DONE \o/ HURRAY! */

	printf("\n\nMerging is now done. I give NO guarantee for your DBs.\n");
	return 0;
} /* End of main() */

/* Open a database file for reading and check for the version */
dbFILE *open_db_read(const char *service, const char *filename, int version)
{
	dbFILE *f;
	FILE *fp;
	int myversion;

	f = (dbFILE *)calloc(sizeof(*f), 1);
	if (!f) {
		printf("Can't allocate memory for %s database %s.\n", service, filename);
		exit(0);
	}
	strscpy(f->filename, filename, sizeof(f->filename));
	f->mode = 'r';
	fp = fopen(f->filename, "rb");
	if (!fp) {
		printf("Can't read %s database %s.\n", service, f->filename);
		free(f);
		return NULL;
	}
	f->fp = fp;
	myversion = fgetc(fp) << 24 | fgetc(fp) << 16 | fgetc(fp) << 8 | fgetc(fp);
	if (feof(fp)) {
		printf("Error reading version number on %s: End of file detected.\n", f->filename);
		exit(0);
	} else if (myversion < version) {
		printf("Unsuported database version (%d) on %s.\n", myversion, f->filename);
		exit(0);
	}
	return f;
}

/* Open a database file for reading and check for the version */
dbFILE *open_db_write(const char *service, const char *filename, int version)
{
	dbFILE *f;
	int fd;

	f = (dbFILE *)calloc(sizeof(*f), 1);
	if (!f) {
		printf("Can't allocate memory for %s database %s.\n", service, filename);
		exit(0);
	}
	strscpy(f->filename, filename, sizeof(f->filename));
	filename = f->filename;
#ifndef _WIN32
	unlink(filename);
#else
	DeleteFile(filename);
#endif
	f->mode = 'w';
#ifndef _WIN32
	fd = open(filename, O_WRONLY | O_CREAT | O_EXCL, 0666);
#else
	fd = open(filename, O_WRONLY | O_CREAT | O_EXCL | _O_BINARY, 0666);
#endif
	f->fp = fdopen(fd, "wb");   /* will fail and return NULL if fd < 0 */
	if (!f->fp || !write_file_version(f, version)) {
		printf("Can't write to %s database %s.\n", service, filename);
		if (f->fp) {
			fclose(f->fp);
#ifndef _WIN32
			unlink(filename);
#else
			DeleteFile(filename);
#endif
		}
		free(f);
		return NULL;
	}
	return f;
}

/* Close it */
void close_db(dbFILE * f)
{
	fclose(f->fp);
	free(f);
}

int read_int16(int16 * ret, dbFILE * f)
{
	int c1, c2;

	c1 = fgetc(f->fp);
	c2 = fgetc(f->fp);
	if (c1 == EOF || c2 == EOF)
		return -1;
	*ret = c1 << 8 | c2;
	return 0;
}

int read_uint16(uint16 * ret, dbFILE * f)
{
	int c1, c2;

	c1 = fgetc(f->fp);
	c2 = fgetc(f->fp);
	if (c1 == EOF || c2 == EOF)
		return -1;
	*ret = c1 << 8 | c2;
	return 0;
}


int write_int16(uint16 val, dbFILE * f)
{
	if (fputc((val >> 8) & 0xFF, f->fp) == EOF
		|| fputc(val & 0xFF, f->fp) == EOF)
		return -1;
	return 0;
}

int read_int32(int32 * ret, dbFILE * f)
{
	int c1, c2, c3, c4;

	c1 = fgetc(f->fp);
	c2 = fgetc(f->fp);
	c3 = fgetc(f->fp);
	c4 = fgetc(f->fp);
	if (c1 == EOF || c2 == EOF || c3 == EOF || c4 == EOF)
		return -1;
	*ret = c1 << 24 | c2 << 16 | c3 << 8 | c4;
	return 0;
}

int read_uint32(uint32 * ret, dbFILE * f)
{
	int c1, c2, c3, c4;

	c1 = fgetc(f->fp);
	c2 = fgetc(f->fp);
	c3 = fgetc(f->fp);
	c4 = fgetc(f->fp);
	if (c1 == EOF || c2 == EOF || c3 == EOF || c4 == EOF)
		return -1;
	*ret = c1 << 24 | c2 << 16 | c3 << 8 | c4;
	return 0;
}

int write_int32(uint32 val, dbFILE * f)
{
	if (fputc((val >> 24) & 0xFF, f->fp) == EOF)
		return -1;
	if (fputc((val >> 16) & 0xFF, f->fp) == EOF)
		return -1;
	if (fputc((val >> 8) & 0xFF, f->fp) == EOF)
		return -1;
	if (fputc((val) & 0xFF, f->fp) == EOF)
		return -1;
	return 0;
}


int read_ptr(void **ret, dbFILE * f)
{
	int c;

	c = fgetc(f->fp);
	if (c == EOF)
		return -1;
	*ret = (c ? (void *) 1 : (void *) 0);
	return 0;
}

int write_ptr(const void *ptr, dbFILE * f)
{
	if (fputc(ptr ? 1 : 0, f->fp) == EOF)
		return -1;
	return 0;
}


int read_string(char **ret, dbFILE * f)
{
	char *s;
	uint16 len;

	if (read_uint16(&len, f) < 0)
		return -1;
	if (len == 0) {
		*ret = NULL;
		return 0;
	}
	s = (char *)calloc(len, 1);
	if (len != fread(s, 1, len, f->fp)) {
		free(s);
		return -1;
	}
	*ret = s;
	return 0;
}

int write_string(const char *s, dbFILE * f)
{
	uint32 len;

	if (!s)
		return write_int16(0, f);
	len = strlen(s);
	if (len > 65534)
		len = 65534;
	if (write_int16((uint16) (len + 1), f) < 0)
		return -1;
	if (len > 0 && fwrite(s, 1, len, f->fp) != len)
		return -1;
	if (fputc(0, f->fp) == EOF)
		return -1;
	return 0;
}

NickCore *findcore(const char *nick, int unused)
{
	NickCore *nc;

	for (nc = nclists[HASH(nick)]; nc; nc = nc->next) {
		if (!mystricmp(nc->display, nick))
			if ((nc->unused && unused) || (!nc->unused && !unused))
				return nc;
	}

	return NULL;
}

NickAlias *findnick(const char *nick)
{
	NickAlias *na;

	for (na = nalists[HASH(nick)]; na; na = na->next) {
		if (!mystricmp(na->nick, nick))
			return na;
	}

	return NULL;
}

int write_file_version(dbFILE * f, uint32 version)
{
	FILE *fp = f->fp;
	if (fputc(version >> 24 & 0xFF, fp) < 0 ||
		fputc(version >> 16 & 0xFF, fp) < 0 ||
		fputc(version >> 8 & 0xFF, fp) < 0 ||
		fputc(version & 0xFF, fp) < 0) {
			printf("Error writing version number on %s.\n", f->filename);
			exit(0);
	}
	return 1;
}

/* strscpy:  Copy at most len-1 characters from a string to a buffer, and
 *		   add a null terminator after the last character copied.
 */

char *strscpy(char *d, const char *s, size_t len)
{
	char *d_orig = d;

	if (!len)
		return d;
	while (--len && (*d++ = *s++));
	*d = '\0';
	return d_orig;
}

int mystricmp(const char *s1, const char *s2)
{
	register int c;

	while ((c = tolower(*s1)) == tolower(*s2)) {
		if (c == 0)
			return 0;
		s1++;
		s2++;
	}
	if (c < tolower(*s2))
		return -1;
	return 1;
}

int delnick(NickAlias *na, int donttouchthelist)
{
	if (!donttouchthelist) {
		/* Remove us from the aliases list */
		if (na->next)
			na->next->prev = na->prev;
		if (na->prev)
			na->prev->next = na->next;
		else
			nalists[HASH(na->nick)] = na->next;
	}

	/* free() us */
	free(na->nick);
	free(na);
	return 1;
}

int delcore(NickCore *nc)
{
	int i;
	/* Remove the core from the list */
	if (nc->next)
		nc->next->prev = nc->prev;
	if (nc->prev)
		nc->prev->next = nc->next;
	else
		nclists[HASH(nc->display)] = nc->next;

	free(nc->display);
	if (nc->pass)
		free(nc->pass);
	if (nc->email)
		free(nc->email);
	if (nc->greet)
		free(nc->greet);
	if (nc->url)
		free(nc->url);
	if (nc->last_usermask)
		free(nc->last_usermask);
	if (nc->last_realname)
		free(nc->last_realname);
	if (nc->last_quit)
		free(nc->last_quit);
	if (nc->access) {
		for (i = 0; i < nc->accesscount; i++) {
			if (nc->access[i])
				free(nc->access[i]);
		}
		free(nc->access);
	}
	if (nc->memos.memos) {
		for (i = 0; i < nc->memos.memocount; i++) {
			if (nc->memos.memos[i].text)
				free(nc->memos.memos[i].text);
		}
		free(nc->memos.memos);
	}
	free(nc);
	return 1;
}

void insert_bot(BotInfo * bi)
{
	BotInfo *ptr, *prev;

	for (prev = NULL, ptr = botlists[tolower(*bi->nick)];
		 ptr != NULL && mystricmp(ptr->nick, bi->nick) < 0;
		 prev = ptr, ptr = ptr->next);
	bi->prev = prev;
	bi->next = ptr;
	if (!prev)
		botlists[tolower(*bi->nick)] = bi;
	else
		prev->next = bi;
	if (ptr)
		ptr->prev = bi;
}

BotInfo *findbot(char *nick)
{
	BotInfo *bi;

	for (bi = botlists[tolower(*nick)]; bi; bi = bi->next)
		if (!mystricmp(nick, bi->nick))
			return bi;

	return NULL;
}

ChannelInfo *cs_findchan(const char *chan)
{
	ChannelInfo *ci;
	for (ci = chanlists[tolower(chan[1])]; ci; ci = ci->next) {
		if (!mystricmp(ci->name, chan))
			return ci;
	}
	return NULL;
}

void alpha_insert_chan(ChannelInfo * ci)
{
	ChannelInfo *ptr, *prev;
	char *chan = ci->name;

	for (prev = NULL, ptr = chanlists[tolower(chan[1])];
		 ptr != NULL && mystricmp(ptr->name, chan) < 0;
		 prev = ptr, ptr = ptr->next);
	ci->prev = prev;
	ci->next = ptr;
	if (!prev)
		chanlists[tolower(chan[1])] = ci;
	else
		prev->next = ci;
	if (ptr)
		ptr->prev = ci;
}
