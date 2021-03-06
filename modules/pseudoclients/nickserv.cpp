/* NickServ core functions
 *
 * (C) 2003-2013 Anope Team
 * Contact us at team@anope.org
 *
 * Please read COPYING and README for further details.
 *
 * Based on the original code of Epona by Lara.
 * Based on the original code of Services by Andy Church.
 */

/*************************************************************************/

#include "module.h"

class NickServCollide;

typedef std::map<Anope::string, NickServCollide *> nickservcollides_map;

static nickservcollides_map NickServCollides;

/** Timer for colliding nicks to force people off of nicknames
 */
class NickServCollide : public Timer
{
	Reference<User> u;
	Anope::string nick;

 public:
	/** Constructor
	 * @param nick The nick we're colliding
	 * @param delay How long to delay before kicking the user off the nick
	 */
	NickServCollide(User *user, time_t delay) : Timer(delay), u(user), nick(u->nick)
	{
		/* Erase the current collide and use the new one */
		nickservcollides_map::iterator nit = NickServCollides.find(user->nick);
		if (nit != NickServCollides.end())
			delete nit->second;

		NickServCollides.insert(std::make_pair(nick, this));
	}

	virtual ~NickServCollide()
	{
		NickServCollides.erase(this->nick);
	}

	/** Called when the delay is up
	 * @param t The current time
	 */
	void Tick(time_t t) anope_override
	{
		if (!u)
			return;
		/* If they identified or don't exist anymore, don't kill them. */
		NickAlias *na = NickAlias::Find(u->nick);
		if (!na || u->Account() == na->nc || u->timestamp > this->GetSetTime())
			return;

		u->Collide(na);
	}
};

class MyNickServService : public NickServService
{
 public:
	MyNickServService(Module *m) : NickServService(m) { }

	void Validate(User *u) anope_override
	{
		NickAlias *na = NickAlias::Find(u->nick);
		if (!na)
			return;

		if (na->nc->HasExt("SUSPENDED"))
		{
			u->SendMessage(NickServ, NICK_X_SUSPENDED, u->nick.c_str());
			u->Collide(na);
			return;
		}

		if (!(u->Account() == na->nc) && !u->fingerprint.empty() && na->nc->FindCert(u->fingerprint))
		{
			u->SendMessage(NickServ, _("SSL Fingerprint accepted, you are now identified."));
			Log(u) << "automatically identified for account " << na->nc->display << " using a valid SSL fingerprint.";
			u->Identify(na);
			return;
		}

		if (!na->nc->HasExt("SECURE") && u->IsRecognized())
		{
			na->last_seen = Anope::CurTime;
			Anope::string last_usermask = u->GetIdent() + "@" + u->GetDisplayedHost();
			na->last_usermask = last_usermask;
			na->last_realname = u->realname;
			return;
		}

		if (Config->GetBlock("options")->Get<bool>("nonicknameownership"))
			return;

		if (u->IsRecognized(false) || !na->nc->HasExt("KILL_IMMED"))
		{
			if (na->nc->HasExt("SECURE"))
				u->SendMessage(NickServ, NICK_IS_SECURE, Config->StrictPrivmsg.c_str(), NickServ->nick.c_str());
			else
				u->SendMessage(NickServ, NICK_IS_REGISTERED, Config->StrictPrivmsg.c_str(), NickServ->nick.c_str());
		}

		if (na->nc->HasExt("KILLPROTECT") && !u->IsRecognized(false))
		{
			if (na->nc->HasExt("KILL_IMMED"))
			{
				u->SendMessage(NickServ, FORCENICKCHANGE_NOW);
				u->Collide(na);
			}
			else if (na->nc->HasExt("KILL_QUICK"))
			{
				time_t killquick = Config->GetModule("nickserv")->Get<time_t>("killquick", "60s");
				u->SendMessage(NickServ, _("If you do not change within %s, I will change your nick."), Anope::Duration(killquick, u->Account()).c_str());
				new NickServCollide(u, killquick);
			}
			else
			{
				time_t kill = Config->GetModule("nickserv")->Get<time_t>("kill", "20s");
				u->SendMessage(NickServ, _("If you do not change within %s, I will change your nick."), Anope::Duration(kill, u->Account()).c_str());
				new NickServCollide(u, kill);
			}
		}

	}

	void Login(User *user, NickAlias *na) anope_override
	{
		const NickAlias *u_na = NickAlias::Find(user->nick);
		user->Login(na->nc);
		if (u_na && *u_na->nc == *na->nc && !Config->GetBlock("options")->Get<bool>("nonicknameownership") && na->nc->HasExt("UNCONFIRMED") == false)
			user->SetMode(NickServ, "REGISTERED");
	}
};

class ExpireCallback : public Timer
{
 public:
	ExpireCallback(Module *o) : Timer(o, Config->GetBlock("options")->Get<time_t>("expiretimeout"), Anope::CurTime, true) { }

	void Tick(time_t) anope_override
	{
		if (Anope::NoExpire || Anope::ReadOnly)
			return;

		time_t unconfirmed_expire = Config->GetModule(this->GetOwner())->Get<time_t>("unconfirmedexpire", "1d");
		time_t nickserv_expire = Config->GetModule(this->GetOwner())->Get<time_t>("expire");

		for (nickalias_map::const_iterator it = NickAliasList->begin(), it_end = NickAliasList->end(); it != it_end; )
		{
			NickAlias *na = it->second;
			++it;

			User *u = User::Find(na->nick);
			if (u && (na->nc->HasExt("SECURE") ? u->IsIdentified(true) : u->IsRecognized()))
				na->last_seen = Anope::CurTime;

			bool expire = false;

			if (na->nc->HasExt("UNCONFIRMED"))
				if (unconfirmed_expire && Anope::CurTime - na->time_registered >= unconfirmed_expire)
					expire = true;
			if (nickserv_expire && Anope::CurTime - na->last_seen >= nickserv_expire)
				expire = true;
			if (na->HasExt("NO_EXPIRE"))
				expire = false;

			FOREACH_MOD(I_OnPreNickExpire, OnPreNickExpire(na, expire));
		
			if (expire)
			{
				Anope::string extra;
				if (na->nc->HasExt("SUSPENDED"))
					extra = "suspended ";
				Log(LOG_NORMAL, "expire") << "Expiring " << extra << "nickname " << na->nick << " (group: " << na->nc->display << ") (e-mail: " << (na->nc->email.empty() ? "none" : na->nc->email) << ")";
				FOREACH_MOD(I_OnNickExpire, OnNickExpire(na));
				delete na;
			}
		}
	}
};

class NickServCore : public Module
{
	MyNickServService nickserv;
	ExpireCallback expires;
	std::vector<Anope::string> defaults;

 public:
	NickServCore(const Anope::string &modname, const Anope::string &creator) : Module(modname, creator, PSEUDOCLIENT | VENDOR), nickserv(this), expires(this)
	{
		Implementation i[] = { I_OnReload, I_OnBotDelete, I_OnDelNick, I_OnDelCore, I_OnChangeCoreDisplay, I_OnNickIdentify, I_OnNickGroup,
				I_OnNickUpdate, I_OnUserConnect, I_OnPostUserLogoff, I_OnServerSync, I_OnUserNickChange, I_OnPreHelp, I_OnPostHelp,
				I_OnNickCoreCreate, I_OnUserQuit };
		ModuleManager::Attach(i, this, sizeof(i) / sizeof(Implementation));
	}

	~NickServCore()
	{
		NickServ = NULL;
	}

	void OnReload(Configuration::Conf *conf) anope_override
	{
		const Anope::string &nsnick = conf->GetModule(this)->Get<const Anope::string &>("client");

		if (nsnick.empty())
			throw ConfigException(this->name + ": <client> must be defined");

		BotInfo *bi = BotInfo::Find(nsnick, true);
		if (!bi)
			throw ConfigException(this->name + ": no bot named " + nsnick);

		NickServ = bi;

		spacesepstream(conf->GetModule(this)->Get<const Anope::string &>("defaults", "secure memo_signon memo_receive")).GetTokens(defaults);
		if (defaults.empty())
		{
			defaults.push_back("SECURE");
			defaults.push_back("MEMO_SIGNON");
			defaults.push_back("MEMO_RECEIVE");
		}
	}

	void OnBotDelete(BotInfo *bi) anope_override
	{
		if (bi == NickServ)
			NickServ = NULL;
	}

	void OnDelNick(NickAlias *na) anope_override
	{
		User *u = User::Find(na->nick);
		if (u && u->Account() == na->nc)
		{
			IRCD->SendLogout(u);
			u->RemoveMode(NickServ, "REGISTERED");
			u->Logout();
		}
	}

	void OnDelCore(NickCore *nc) anope_override
	{
		Log(NickServ, "nick") << "deleting nickname group " << nc->display;

		/* Clean up this nick core from any users online */
		for (std::list<User *>::iterator it = nc->users.begin(); it != nc->users.end();)
		{
			User *user = *it++;
			IRCD->SendLogout(user);
			user->RemoveMode(NickServ, "REGISTERED");
			user->Logout();
			FOREACH_MOD(I_OnNickLogout, OnNickLogout(user));
		}
		nc->users.clear();
	}

	void OnChangeCoreDisplay(NickCore *nc, const Anope::string &newdisplay) anope_override
	{
		Log(LOG_NORMAL, "nick", NickServ) << "Changing " << nc->display << " nickname group display to " << newdisplay;
	}

	void OnNickIdentify(User *u) anope_override
	{
		Configuration::Block *block = Config->GetModule(this);

		if (!Config->GetBlock("options")->Get<bool>("nonicknameownership"))
		{
			const NickAlias *this_na = NickAlias::Find(u->nick);
			if (this_na && this_na->nc == u->Account() && u->Account()->HasExt("UNCONFIRMED") == false)
				u->SetMode(NickServ, "REGISTERED");
		}

		if (block->Get<bool>("modeonid", "yes"))
			for (User::ChanUserList::iterator it = u->chans.begin(), it_end = u->chans.end(); it != it_end; ++it)
			{
				ChanUserContainer *cc = it->second;
				Channel *c = cc->chan;
				if (c)
					c->SetCorrectModes(u, true, true);
			}

		const Anope::string &modesonid = block->Get<const Anope::string &>("modesonid");
		if (!modesonid.empty())
			u->SetModes(NickServ, "%s", modesonid.c_str());

		if (block->Get<bool>("forceemail") && u->Account()->email.empty())
		{
			u->SendMessage(NickServ, _("You must now supply an e-mail for your nick.\n"
							"This e-mail will allow you to retrieve your password in\n"
							"case you forget it."));
			u->SendMessage(NickServ, _("Type \002%s%s SET EMAIL \037e-mail\037\002 in order to set your e-mail.\n"
							"Your privacy is respected; this e-mail won't be given to\n"
							"any third-party person."), Config->StrictPrivmsg.c_str(), NickServ->nick.c_str());
		}

		if (u->Account()->HasExt("UNCONFIRMED"))
		{
			u->SendMessage(NickServ, _("Your email address is not confirmed. To confirm it, follow the instructions that were emailed to you when you registered."));
			const NickAlias *this_na = NickAlias::Find(u->Account()->display);
			time_t time_registered = Anope::CurTime - this_na->time_registered;
			time_t unconfirmed_expire = Config->GetModule(this)->Get<time_t>("unconfirmedexpire", "1d");
			if (unconfirmed_expire > time_registered)
				u->SendMessage(NickServ, _("Your account will expire, if not confirmed, in %s"), Anope::Duration(unconfirmed_expire - time_registered).c_str());
		}
	}

	void OnNickGroup(User *u, NickAlias *target) anope_override
	{
		if (target->nc->HasExt("UNCONFIRMED") == false)
			u->SetMode(NickServ, "REGISTERED");
	}

	void OnNickUpdate(User *u) anope_override
	{
		for (User::ChanUserList::iterator it = u->chans.begin(), it_end = u->chans.end(); it != it_end; ++it)
		{
			ChanUserContainer *cc = it->second;
			Channel *c = cc->chan;
			if (c)
				c->SetCorrectModes(u, true, true);
		}
	}

	void OnUserConnect(User *u, bool &exempt) anope_override
	{
		if (u->Quitting() || !u->server->IsSynced())
			return;

		const NickAlias *na = NickAlias::Find(u->nick);
		const Anope::string &unregistered_notice = Config->GetModule(this)->Get<const Anope::string &>("unregistered_notice");
		if (!Config->GetBlock("options")->Get<bool>("nonicknameownership") && !unregistered_notice.empty() && !na)
			u->SendMessage(NickServ, unregistered_notice);
		else if (na)
			this->nickserv.Validate(u);
	}

	void OnPostUserLogoff(User *u) anope_override
	{
		NickAlias *na = NickAlias::Find(u->nick);
		if (na)
			na->OnCancel(u);
	}

	void OnServerSync(Server *s) anope_override
	{
		for (user_map::const_iterator it = UserListByNick.begin(); it != UserListByNick.end(); ++it)
		{
			User *u = it->second;
			if (u->server == s && !u->IsIdentified())
				this->nickserv.Validate(u);
		}
	}

	void OnUserNickChange(User *u, const Anope::string &oldnick) anope_override
	{
		NickAlias *old_na = NickAlias::Find(oldnick), *na = NickAlias::Find(u->nick);
		/* If the new nick isnt registerd or its registerd and not yours */
		if (!na || na->nc != u->Account())
		{
			/* Remove +r, but keep an account associated with the user */
			u->RemoveMode(NickServ, "REGISTERED");

			this->nickserv.Validate(u);
		}
		else
		{
			/* Reset +r and re-send account (even though it really should be set at this point) */
			IRCD->SendLogin(u);
			if (!Config->GetBlock("options")->Get<bool>("nonicknameownership") && na->nc == u->Account() && na->nc->HasExt("UNCONFIRMED") == false)
				u->SetMode(NickServ, "REGISTERED");
			Log(NickServ) << u->GetMask() << " automatically identified for group " << u->Account()->display;
		}

		if (!u->nick.equals_ci(oldnick) && old_na)
			old_na->OnCancel(u);
	}

	void OnUserModeSet(User *u, const Anope::string &mname) anope_override
	{
		if (mname == "REGISTERED" && !u->IsIdentified())
			u->RemoveMode(NickServ, mname);
	}

	EventReturn OnPreHelp(CommandSource &source, const std::vector<Anope::string> &params) anope_override
	{
		if (!params.empty() || source.c || source.service != NickServ)
			return EVENT_CONTINUE;
		if (!Config->GetBlock("options")->Get<bool>("nonicknameownership"))
			source.Reply(_("\002%s\002 allows you to register a nickname and\n"
				"prevent others from using it. The following\n"
				"commands allow for registration and maintenance of\n"
				"nicknames; to use them, type \002%s%s \037command\037\002.\n"
				"For more information on a specific command, type\n"
				"\002%s%s %s \037command\037\002.\n "), NickServ->nick.c_str(), Config->StrictPrivmsg.c_str(), NickServ->nick.c_str(), Config->StrictPrivmsg.c_str(), NickServ->nick.c_str(), source.command.c_str());
		else
			source.Reply(_("\002%s\002 allows you to register an account.\n"
				"The following commands allow for registration and maintenance of\n"
				"accounts; to use them, type \002%s%s \037command\037\002.\n"
				"For more information on a specific command, type\n"
				"\002%s%s %s \037command\037\002.\n "), NickServ->nick.c_str(), Config->StrictPrivmsg.c_str(), NickServ->nick.c_str(), Config->StrictPrivmsg.c_str(), NickServ->nick.c_str(), source.command.c_str());
		return EVENT_CONTINUE;
	}

	void OnPostHelp(CommandSource &source, const std::vector<Anope::string> &params) anope_override
	{
		if (!params.empty() || source.c || source.service != NickServ)
			return;
		if (source.IsServicesOper())
			source.Reply(_(" \n"
				"Services Operators can also drop any nickname without needing\n"
				"to identify for the nick, and may view the access list for\n"
				"any nickname."));
		time_t nickserv_expire = Config->GetModule(this)->Get<time_t>("expire");
		if (nickserv_expire >= 86400)
			source.Reply(_(" \n"
				"Accounts that are not used anymore are subject to \n"
				"the automatic expiration, i.e. they will be deleted\n"
				"after %d days if not used."), nickserv_expire / 86400);
		source.Reply(_(" \n"
			"\002NOTICE:\002 This service is intended to provide a way for\n"
			"IRC users to ensure their identity is not compromised.\n"
			"It is \002NOT\002 intended to facilitate \"stealing\" of\n"
			"nicknames or other malicious actions. Abuse of %s\n"
			"will result in, at minimum, loss of the abused\n"
			"nickname(s)."), NickServ->nick.c_str());
	}

	void OnNickCoreCreate(NickCore *nc)
	{
		/* Set default flags */
		for (unsigned i = 0; i < defaults.size(); ++i)
			this->ExtendMetadata(defaults[i].upper());
	}

	void OnUserQuit(User *u, const Anope::string &msg)
	{
		if (u->server && !u->server->GetQuitReason().empty() && Config->GetModule(this)->Get<bool>("hidenetsplitquit"))
			return;

		/* Update last quit and last seen for the user */
		NickAlias *na = NickAlias::Find(u->nick);
		if (na && !na->nc->HasExt("SUSPENDED") && (u->IsRecognized() || u->IsIdentified(true)))
		{
			na->last_seen = Anope::CurTime;
			na->last_quit = msg;
		}
	}
};

MODULE_INIT(NickServCore)

