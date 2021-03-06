/* HostServ core functions
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

class HostServCore : public Module
{
 public:
	HostServCore(const Anope::string &modname, const Anope::string &creator) : Module(modname, creator, PSEUDOCLIENT | VENDOR)
	{

		if (!IRCD || !IRCD->CanSetVHost)
			throw ModuleException("Your IRCd does not support vhosts");
	
		Implementation i[] = { I_OnReload, I_OnBotDelete, I_OnNickIdentify, I_OnNickUpdate, I_OnPreHelp };
		ModuleManager::Attach(i, this, sizeof(i) / sizeof(Implementation));
	}

	~HostServCore()
	{
		HostServ = NULL;
	}

	void OnReload(Configuration::Conf *conf) anope_override
	{
		const Anope::string &hsnick = conf->GetModule(this)->Get<const Anope::string &>("client");

		if (hsnick.empty())
			throw ConfigException(this->name + ": <client> must be defined");

		BotInfo *bi = BotInfo::Find(hsnick, true);
		if (!bi)
			throw ConfigException(this->name + ": no bot named " + hsnick);

		HostServ = bi;
	}

	void OnBotDelete(BotInfo *bi) anope_override
	{
		if (bi == HostServ)
			HostServ = NULL;
	}

	void OnNickIdentify(User *u) anope_override
	{
		const NickAlias *na = NickAlias::Find(u->nick);
		if (!na || !na->HasVhost())
			na = NickAlias::Find(u->Account()->display);
		if (!IRCD->CanSetVHost || !na || !na->HasVhost())
			return;

		if (u->vhost.empty() || !u->vhost.equals_cs(na->GetVhostHost()) || (!na->GetVhostIdent().empty() && !u->GetVIdent().equals_cs(na->GetVhostIdent())))
		{
			IRCD->SendVhost(u, na->GetVhostIdent(), na->GetVhostHost());

			u->vhost = na->GetVhostHost();
			u->UpdateHost();

			if (IRCD->CanSetVIdent && !na->GetVhostIdent().empty())
				u->SetVIdent(na->GetVhostIdent());

			if (HostServ)
			{
				if (!na->GetVhostIdent().empty())
					u->SendMessage(HostServ, _("Your vhost of \002%s\002@\002%s\002 is now activated."), na->GetVhostIdent().c_str(), na->GetVhostHost().c_str());
				else
					u->SendMessage(HostServ, _("Your vhost of \002%s\002 is now activated."), na->GetVhostHost().c_str());
			}
		}
	}

	void OnNickUpdate(User *u) anope_override
	{
		this->OnNickIdentify(u);
	}

	EventReturn OnPreHelp(CommandSource &source, const std::vector<Anope::string> &params) anope_override
	{
		if (!params.empty() || source.c || source.service != HostServ)
			return EVENT_CONTINUE;
		source.Reply(_("%s commands:"), HostServ->nick.c_str());
		return EVENT_CONTINUE;
	}
};

MODULE_INIT(HostServCore)

