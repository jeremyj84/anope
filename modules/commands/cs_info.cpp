/* ChanServ core functions
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

static ServiceReference<ChanServService> chanserv("ChanServService", "ChanServ");

class CommandCSInfo : public Command
{
	void CheckOptStr(Anope::string &buf, const Anope::string &opt, const char *str, const ChannelInfo *ci, const NickCore *nc)
	{
		if (ci->HasExt(opt))
		{
			if (!buf.empty())
				buf += ", ";

			buf += Language::Translate(nc, str);
		}
	}

 public:
	CommandCSInfo(Module *creator) : Command(creator, "chanserv/info", 1, 2)
	{
		this->SetDesc(_("Lists information about the named registered channel"));
		this->SetSyntax(_("\037channel\037"));
		this->AllowUnregistered(true);
	}

	void Execute(CommandSource &source, const std::vector<Anope::string> &params) anope_override
	{
		const Anope::string &chan = params[0];

		NickCore *nc = source.nc;
		ChannelInfo *ci = ChannelInfo::Find(params[0]);
		if (ci == NULL)
		{
			source.Reply(CHAN_X_NOT_REGISTERED, params[0].c_str());
			return;
		}

		bool has_auspex = source.HasPriv("chanserv/auspex");
		bool show_all = false;

		/* Should we show all fields? Only for sadmins and identified users */
		if (has_auspex || source.AccessFor(ci).HasPriv("INFO"))
			show_all = true;

		InfoFormatter info(nc);

		source.Reply(CHAN_INFO_HEADER, chan.c_str());
		if (ci->GetFounder())
			info["Founder"] = ci->GetFounder()->display;

		if (show_all && ci->GetSuccessor())
			info["Successor"] = ci->GetSuccessor()->display;

		if (!ci->desc.empty())
			info["Description"] = ci->desc;

		info["Registered"] = Anope::strftime(ci->time_registered);
		info["Last used"] = Anope::strftime(ci->last_used);

		const ModeLock *secret = ci->GetMLock("SECRET");
		if (!ci->last_topic.empty() && (show_all || ((!secret || secret->set == false) && (!ci->c || !ci->c->HasMode("SECRET")))))
		{
			info["Last topic"] = ci->last_topic;
			info["Topic set by"] = ci->last_topic_setter;
		}

		if (show_all)
		{
			info["Ban type"] = stringify(ci->bantype);

			Anope::string optbuf;
			CheckOptStr(optbuf, "KEEPTOPIC", _("Topic Retention"), ci, nc);
			CheckOptStr(optbuf, "PEACE", _("Peace"), ci, nc);
			CheckOptStr(optbuf, "PRIVATE", _("Private"), ci, nc);
			CheckOptStr(optbuf, "RESTRICTED", _("Restricted Access"), ci, nc);
			CheckOptStr(optbuf, "SECURE", _("Secure"), ci, nc);
			CheckOptStr(optbuf, "SECUREFOUNDER", _("Secure Founder"), ci, nc);
			CheckOptStr(optbuf, "SECUREOPS", _("Secure Ops"), ci, nc);
			if (ci->HasExt("SIGNKICK"))
				CheckOptStr(optbuf, "SIGNKICK", _("Signed kicks"), ci, nc);
			else
				CheckOptStr(optbuf, "SIGNKICK_LEVEL", _("Signed kicks"), ci, nc);
			CheckOptStr(optbuf, "TOPICLOCK", _("Topic Lock"), ci, nc);
			CheckOptStr(optbuf, "PERSIST", _("Persistent"), ci, nc);
			CheckOptStr(optbuf, "NO_EXPIRE", _("No expire"), ci, nc);
			CheckOptStr(optbuf, "STATS", _("Chanstats"), ci, nc);

			info["Options"] = optbuf.empty() ? _("None") : optbuf;

			const Anope::string &ml = ci->GetMLockAsString(true);
			if (!ml.empty())
				info["Mode lock"] = ml;

			time_t chanserv_expire = Config->GetModule("chanserv")->Get<time_t>("expire", "14d");
			if (!ci->HasExt("NO_EXPIRE") && chanserv_expire && !Anope::NoExpire)
				info["Expires on"] = Anope::strftime(ci->last_used + chanserv_expire);
		}
		if (ci->HasExt("SUSPENDED"))
		{
			Anope::string *by = ci->GetExt<ExtensibleItemClass<Anope::string> *>("suspend_by"), *reason = ci->GetExt<ExtensibleItemClass<Anope::string> *>("suspend_reason");
			if (by != NULL)
				info["Suspended"] = Anope::printf("[%s] %s", by->c_str(), (reason && !reason->empty() ? reason->c_str() : NO_REASON));
		}

		FOREACH_MOD(I_OnChanInfo, OnChanInfo(source, ci, info, show_all));

		std::vector<Anope::string> replies;
		info.Process(replies);

		for (unsigned i = 0; i < replies.size(); ++i)
			source.Reply(replies[i]);

		return;
	}

	bool OnHelp(CommandSource &source, const Anope::string &subcommand) anope_override
	{
		this->SendSyntax(source);
		source.Reply(" ");
		source.Reply(_("Lists information about the named registered channel,\n"
				"including its founder, time of registration, and last\n"
				"time used. If the user issuing the command has the\n"
				"appropriate access for it, then the description, successor,\n"
				"last topic set, settings and expiration time will also\n"
				"be displayed when applicable."));
		return true;
	}
};

class CSInfo : public Module
{
	CommandCSInfo commandcsinfo;

 public:
	CSInfo(const Anope::string &modname, const Anope::string &creator) : Module(modname, creator, VENDOR),
		commandcsinfo(this)
	{

	}
};

MODULE_INIT(CSInfo)
