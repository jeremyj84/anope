/* MemoServ functions.
*
* (C) 2003-2010 Anope Team
* Contact us at team@anope.org
*
* Please read COPYING and README for further details.
*
* Based on the original code of Epona by Lara.
* Based on the original code of Services by Andy Church.
*
* $Id$
*
*/

#include "services.h"
#include "pseudo.h"

/*************************************************************************/
/* *INDENT-OFF* */

E void moduleAddMemoServCmds();
static void new_memo_mail(NickCore *nc, Memo *m);
E void rsend_notify(User *u, Memo *m, const char *chan);

/*************************************************************************/

void moduleAddMemoServCmds() {
	ModuleManager::LoadModuleList(Config.MemoServCoreModules);
}

/*************************************************************************/
/*************************************************************************/
/* *INDENT-ON* */

/**
 * MemoServ initialization.
 * @return void
 */
void ms_init()
{
	moduleAddMemoServCmds();
}

/*************************************************************************/

/**
 * memoserv:  Main MemoServ routine.
 *			Note that the User structure passed to the do_* routines will
 *			always be valid (non-NULL) and will always have a valid
 *			NickInfo pointer in the `ni' field.
 * @param u User Struct
 * @param buf Buffer containing the privmsg
 * @return void
 */
void memoserv(User * u, char *buf)
{
	const char *cmd, *s;

	cmd = strtok(buf, " ");
	if (!cmd) {
		return;
	} else if (stricmp(cmd, "\1PING") == 0) {
		if (!(s = strtok(NULL, ""))) {
			s = "";
		}
		ircdproto->SendCTCP(findbot(Config.s_MemoServ), u->nick.c_str(), "PING %s", s);
	} else {
		mod_run_cmd(Config.s_MemoServ, u, MEMOSERV, cmd);
	}
}

/*************************************************************************/

/**
 * check_memos:  See if the given user has any unread memos, and send a
 *			   NOTICE to that user if so (and if the appropriate flag is
 *			   set).
 * @param u User Struct
 * @return void
 */
void check_memos(User * u)
{
	NickCore *nc;
	unsigned i, newcnt = 0;

	if (!u)
	{
		Alog(LOG_DEBUG) << "check_memos called with NULL values";
		return;
	}

	if (!(nc = u->Account()) || !u->IsRecognized() ||
		!(nc->HasFlag(NI_MEMO_SIGNON))) {
		return;
	}

	for (i = 0; i < nc->memos.memos.size(); i++) {
		if (nc->memos.memos[i]->HasFlag(MF_UNREAD))
			newcnt++;
	}
	if (newcnt > 0) {
		notice_lang(Config.s_MemoServ, u,
					newcnt == 1 ? MEMO_HAVE_NEW_MEMO : MEMO_HAVE_NEW_MEMOS,
					newcnt);
		if (newcnt == 1 && (nc->memos.memos[i - 1]->HasFlag(MF_UNREAD))) {
			notice_lang(Config.s_MemoServ, u, MEMO_TYPE_READ_LAST, Config.s_MemoServ);
		} else if (newcnt == 1) {
			for (i = 0; i < nc->memos.memos.size(); i++) {
				if (nc->memos.memos[i]->HasFlag(MF_UNREAD))
					break;
			}
			notice_lang(Config.s_MemoServ, u, MEMO_TYPE_READ_NUM, Config.s_MemoServ,
						nc->memos.memos[i]->number);
		} else {
			notice_lang(Config.s_MemoServ, u, MEMO_TYPE_LIST_NEW, Config.s_MemoServ);
		}
	}
	if (nc->memos.memomax > 0 && nc->memos.memos.size() >= nc->memos.memomax) {
		if (nc->memos.memos.size() > nc->memos.memomax)
			notice_lang(Config.s_MemoServ, u, MEMO_OVER_LIMIT, nc->memos.memomax);
		else
			notice_lang(Config.s_MemoServ, u, MEMO_AT_LIMIT, nc->memos.memomax);
	}
}

/*************************************************************************/
/*********************** MemoServ private routines ***********************/
/*************************************************************************/

/**
 * Return the MemoInfo corresponding to the given nick or channel name.
 * @param name Name to check
 * @param ischan - the result its a channel will be stored in here
 * @param isforbid - the result if its forbidden will be stored in here
 * @return `ischan' 1 if the name was a channel name, else 0.
 * @return `isforbid' 1 if the name is forbidden, else 0.
 */
MemoInfo *getmemoinfo(const char *name, int *ischan, int *isforbid)
{
	if (*name == '#') {
		ChannelInfo *ci;
		if (ischan)
			*ischan = 1;
		ci = cs_findchan(name);
		if (ci) {
			if (!(ci->HasFlag(CI_FORBIDDEN))) {
				*isforbid = 0;
				return &ci->memos;
			} else {
				*isforbid = 1;
				return NULL;
			}
		} else {
			*isforbid = 0;
			return NULL;
		}
	} else {
		NickAlias *na;
		if (ischan)
			*ischan = 0;
		na = findnick(name);
		if (na) {
			if (!na->HasFlag(NS_FORBIDDEN)) {
				*isforbid = 0;
				return &na->nc->memos;
			} else {
				*isforbid = 1;
				return NULL;
			}
		} else {
			*isforbid = 0;
			return NULL;
		}
	}
}

/*************************************************************************/

/**
 * Split from do_send, this way we can easily send a memo from any point
 * @param u User Struct
 * @param name Target of the memo
 * @param text Memo Text
 * @param z type see info
 *	0 - reply to user
 *	1 - silent
 *	2 - silent with no delay timer
 *	3 - reply to user and request read receipt
 * @return void
 */
void memo_send(User * u, const char *name, const char *text, int z)
{
	int ischan;
	int isforbid;
	Memo *m;
	MemoInfo *mi;
	time_t now = time(NULL);
	char *source = u->Account()->display;
	int is_servoper = u->Account() && u->Account()->IsServicesOper();

	if (readonly) {
		notice_lang(Config.s_MemoServ, u, MEMO_SEND_DISABLED);
	} else if (!text) {
		if (z == 0)
			syntax_error(Config.s_MemoServ, u, "SEND", MEMO_SEND_SYNTAX);

		if (z == 3)
			syntax_error(Config.s_MemoServ, u, "RSEND", MEMO_RSEND_SYNTAX);

	} else if (!u->IsIdentified() && !u->IsRecognized()) {
		if (z == 0 || z == 3)
			notice_lang(Config.s_MemoServ, u, NICK_IDENTIFY_REQUIRED, Config.s_NickServ);

	} else if (!(mi = getmemoinfo(name, &ischan, &isforbid))) {
		if (z == 0 || z == 3) {
			if (isforbid) {
				notice_lang(Config.s_MemoServ, u,
							ischan ? CHAN_X_FORBIDDEN :
							NICK_X_FORBIDDEN, name);
			} else {
				notice_lang(Config.s_MemoServ, u,
							ischan ? CHAN_X_NOT_REGISTERED :
							NICK_X_NOT_REGISTERED, name);
			}
		}
	} else if (z != 2 && Config.MSSendDelay > 0 &&
			   u && u->lastmemosend + Config.MSSendDelay > now) {
		u->lastmemosend = now;
		if (z == 0)
			notice_lang(Config.s_MemoServ, u, MEMO_SEND_PLEASE_WAIT, Config.MSSendDelay);

		if (z == 3)
			notice_lang(Config.s_MemoServ, u, MEMO_RSEND_PLEASE_WAIT,
						Config.MSSendDelay);

	} else if (mi->memomax == 0 && !is_servoper) {
		if (z == 0 || z == 3)
			notice_lang(Config.s_MemoServ, u, MEMO_X_GETS_NO_MEMOS, name);

	} else if (mi->memomax > 0 && mi->memos.size() >= mi->memomax
			   && !is_servoper) {
		if (z == 0 || z == 3)
			notice_lang(Config.s_MemoServ, u, MEMO_X_HAS_TOO_MANY_MEMOS, name);

	} else {
		u->lastmemosend = now;
		m = new Memo;
		mi->memos.push_back(m);
		m->sender = source;
		if (mi->memos.size() > 1)
		{
			m->number = mi->memos[mi->memos.size() - 2]->number + 1;
			if (m->number < 1)
			{
				for (unsigned i = 0; i < mi->memos.size(); i++)
				{
					mi->memos[i]->number = i + 1;
				}
			}
		}
		else
			m->number = 1;
		m->time = time(NULL);
		m->text = sstrdup(text);
		m->SetFlag(MF_UNREAD);
		/* Set notify sent flag - DrStein */
		if (z == 2) {
			m->SetFlag(MF_NOTIFYS);
		}
		/* Set receipt request flag */
		if (z == 3)
			m->SetFlag(MF_RECEIPT);
		if (z == 0 || z == 3)
			notice_lang(Config.s_MemoServ, u, MEMO_SENT, name);
		if (!ischan) {
			NickAlias *na;
			NickCore *nc = (findnick(name))->nc;

			FOREACH_MOD(I_OnMemoSend, OnMemoSend(u, nc, m));

			if (Config.MSNotifyAll) {
				if ((nc->HasFlag(NI_MEMO_RECEIVE))
					&& get_ignore(name) == NULL) {
					int i;

					for (i = 0; i < nc->aliases.count; i++) {
						na = static_cast<NickAlias *>(nc->aliases.list[i]);
						User *user = finduser(na->nick);
						if (user && user->IsIdentified())
							notice_lang(Config.s_MemoServ, user,
										MEMO_NEW_MEMO_ARRIVED, source,
										Config.s_MemoServ, m->number);
					}
				} else {
					if ((u = finduser(name)) && u->IsIdentified() 
						&& (nc->HasFlag(NI_MEMO_RECEIVE)))
						notice_lang(Config.s_MemoServ, u, MEMO_NEW_MEMO_ARRIVED,
									source, Config.s_MemoServ, m->number);
				}			   /* if (flags & MEMO_RECEIVE) */
			}
			/* if (MSNotifyAll) */
			/* let's get out the mail if set in the nickcore - certus */
			if (nc->HasFlag(NI_MEMO_MAIL))
				new_memo_mail(nc, m);
		} else {
			Channel *c;

			FOREACH_MOD(I_OnMemoSend, OnMemoSend(u, cs_findchan(name), m));

			if (Config.MSNotifyAll && (c = findchan(name)))
			{
				for (CUserList::iterator it = c->users.begin(); it != c->users.end(); ++it)
				{
					UserContainer *cu = *it;

					if (check_access(cu->user, c->ci, CA_MEMO)) {
						if (cu->user->Account()
							&& (cu->user->Account()->HasFlag(NI_MEMO_RECEIVE))
							&& get_ignore(cu->user->nick.c_str()) == NULL) {
							notice_lang(Config.s_MemoServ, cu->user,
										MEMO_NEW_X_MEMO_ARRIVED,
										c->ci->name.c_str(), Config.s_MemoServ,
										c->ci->name.c_str(), m->number);
						}
					}
				}
			}				   /* MSNotifyAll */
		}					   /* if (!ischan) */
	}						   /* if command is valid */
}

/*************************************************************************/
/**
 * Delete a memo by number.
 * @param mi Memoinfo struct
 * @param num Memo number to delete
 * @return int 1 if the memo was found, else 0.
 */
int delmemo(MemoInfo * mi, int num)
{
	unsigned i;
	if (mi->memos.empty()) return 0;

	for (i = 0; i < mi->memos.size(); i++) {
		if (mi->memos[i]->number == num)
			break;
	}
	if (i < mi->memos.size()) {
		delete [] mi->memos[i]->text;		/* Deallocate memo text memory */
		delete mi->memos[i];	/* Deallocate the memo itself */
		mi->memos.erase(mi->memos.begin() + i);	/* Remove the memo pointer from the vector */
		return 1;
	} else {
		return 0;
	}
}

/*************************************************************************/

static void new_memo_mail(NickCore * nc, Memo * m)
{
	MailInfo *mail = NULL;

	if (!nc || !m)
		return;

	mail = MailMemoBegin(nc);
	if (!mail) {
		return;
	}
	fprintf(mail->pipe, getstring(MEMO_MAIL_TEXT1), nc->display);
	fprintf(mail->pipe, "\n");
	fprintf(mail->pipe, getstring(MEMO_MAIL_TEXT2), m->sender.c_str(),
			m->number);
	fprintf(mail->pipe, "\n\n");
	fprintf(mail->pipe, "%s", getstring(MEMO_MAIL_TEXT3));
	fprintf(mail->pipe, "\n\n");
	fprintf(mail->pipe, "%s", m->text);
	fprintf(mail->pipe, "\n");
	MailEnd(mail);
	return;
}


/*************************************************************************/
/* Send receipt notification to sender. */

void rsend_notify(User * u, Memo * m, const char *chan)
{
	NickAlias *na;
	NickCore *nc;
	char text[256];
	const char *fmt;

	/* Only send receipt if memos are allowed */
	if ((!readonly))
	{

		/* Get nick alias for sender */
		na = findnick(m->sender);

		if (!na) {
			return;
		}

		/* Get nick core for sender */
		nc = na->nc;

		if (!nc) {
			return;
		}

		/* Text of the memo varies if the recepient was a
		   nick or channel */
		if (chan) {
			fmt = getstring(na, MEMO_RSEND_CHAN_MEMO_TEXT);
			snprintf(text, sizeof(text), fmt, chan);
		} else {
			fmt = getstring(na, MEMO_RSEND_NICK_MEMO_TEXT);
			snprintf(text, sizeof(text), "%s", fmt);
		}

		/* Send notification */
		memo_send(u, m->sender.c_str(), text, 2);

		/* Notify recepient of the memo that a notification has
		   been sent to the sender */
		notice_lang(Config.s_MemoServ, u, MEMO_RSEND_USER_NOTIFICATION,
					nc->display);
	}

	/* Remove receipt flag from the original memo */
	m->UnsetFlag(MF_RECEIPT);

	return;
}
