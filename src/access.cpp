/*
 *
 * (C) 2003-2013 Anope Team
 * Contact us at team@anope.org
 *
 * Please read COPYING and README for further details.
 *
 * Based on the original code of Epona by Lara.
 * Based on the original code of Services by Andy Church.
 *
 */

#include "service.h"
#include "access.h"
#include "regchannel.h"
#include "users.h"
#include "account.h"

static struct
{
	Anope::string name;
	Anope::string desc;
} descriptions[] = {
	{"ACCESS_CHANGE", _("Allowed to modify the access list")},
	{"ACCESS_LIST", _("Allowed to view the access list")},
	{"AKICK", _("Allowed to use the AKICK command")},
	{"ASSIGN", _("Allowed to assign/unassign a bot")},
	{"AUTOHALFOP", _("Automatic halfop upon join")},
	{"AUTOOP", _("Automatic channel operator status upon join")},
	{"AUTOOWNER", _("Automatic owner upon join")},
	{"AUTOPROTECT", _("Automatic protect upon join")},
	{"AUTOVOICE", _("Automatic voice on join")},
	{"BADWORDS", _("Allowed to modify channel badwords list")},
	{"BAN", _("Allowed to ban users")},
	{"FANTASIA", _("Allowed to use fantasy commands")},
	{"FOUNDER", _("Allowed to issue commands restricted to channel founders")},
	{"GETKEY", _("Allowed to use GETKEY command")},
	{"GREET", _("Greet message displayed on join")},
	{"HALFOP", _("Allowed to (de)halfop users")},
	{"HALFOPME", _("Allowed to (de)halfop him/herself")},
	{"INFO", _("Allowed to get full INFO output")},
	{"INVITE", _("Allowed to use the INVITE command")},
	{"KICK", _("Allowed to use the KICK command")},
	{"MEMO", _("Allowed to read channel memos")},
	{"MODE", _("Allowed to use the MODE command")},
	{"NOKICK", _("Prevents users being kicked by Services")},
	{"OP", _("Allowed to (de)op users")},
	{"OPME", _("Allowed to (de)op him/herself")},
	{"OWNER", _("Allowed to (de)owner users")},
	{"OWNERME", _("Allowed to (de)owner him/herself")},
	{"PROTECT", _("Allowed to (de)protect users")},
	{"PROTECTME", _("Allowed to (de)protect him/herself")},
	{"SAY", _("Allowed to use SAY and ACT commands")},
	{"SET", _("Allowed to set channel settings")},
	{"SIGNKICK", _("No signed kick when SIGNKICK LEVEL is used")},
	{"TOPIC", _("Allowed to change channel topics")},
	{"UNBAN", _("Allowed to unban users")},
	{"VOICE", _("Allowed to (de)voice users")},
	{"VOICEME", _("Allowed to (de)voice him/herself")}
};

Privilege::Privilege(const Anope::string &n, const Anope::string &d, int r) : name(n), desc(d), rank(r)
{
	if (this->desc.empty())
		for (unsigned j = 0; j < sizeof(descriptions) / sizeof(*descriptions); ++j)
			if (descriptions[j].name.equals_ci(name))
				this->desc = descriptions[j].desc;
}

bool Privilege::operator==(const Privilege &other) const
{
	return this->name.equals_ci(other.name);
}

std::vector<Privilege> PrivilegeManager::Privileges;

void PrivilegeManager::AddPrivilege(Privilege p)
{
	unsigned i;
	for (i = 0; i < Privileges.size(); ++i)
	{
		Privilege &priv = Privileges[i];

		if (priv.rank > p.rank)
			break;
	}
	
	Privileges.insert(Privileges.begin() + i, p);
}

void PrivilegeManager::RemovePrivilege(Privilege &p)
{
	std::vector<Privilege>::iterator it = std::find(Privileges.begin(), Privileges.end(), p);
	if (it != Privileges.end())
		Privileges.erase(it);

	for (registered_channel_map::const_iterator cit = RegisteredChannelList->begin(), cit_end = RegisteredChannelList->end(); cit != cit_end; ++cit)
	{
		cit->second->QueueUpdate();
		cit->second->RemoveLevel(p.name);
	}
}

Privilege *PrivilegeManager::FindPrivilege(const Anope::string &name)
{
	for (unsigned i = Privileges.size(); i > 0; --i)
		if (Privileges[i - 1].name.equals_ci(name))
			return &Privileges[i - 1];
	return NULL;
}

std::vector<Privilege> &PrivilegeManager::GetPrivileges()
{
	return Privileges;
}

void PrivilegeManager::ClearPrivileges()
{
	Privileges.clear();
}

AccessProvider::AccessProvider(Module *o, const Anope::string &n) : Service(o, "AccessProvider", n)
{
	Providers.push_back(this);
}

AccessProvider::~AccessProvider()
{
	std::list<AccessProvider *>::iterator it = std::find(Providers.begin(), Providers.end(), this);
	if (it != Providers.end())
		Providers.erase(it);
}

std::list<AccessProvider *> AccessProvider::Providers;

const std::list<AccessProvider *>& AccessProvider::GetProviders()
{
	return Providers;
}

ChanAccess::ChanAccess(AccessProvider *p) : Serializable("ChanAccess"), provider(p)
{
}

ChanAccess::~ChanAccess()
{
	if (this->ci)
	{
		std::vector<ChanAccess *>::iterator it = std::find(this->ci->access->begin(), this->ci->access->end(), this);
		if (it != this->ci->access->end())
			this->ci->access->erase(it);

		const NickAlias *na = NickAlias::Find(this->mask);
		if (na != NULL)
			na->nc->RemoveChannelReference(this->ci);
	}
}

void ChanAccess::Serialize(Serialize::Data &data) const
{
	data["provider"] << this->provider->name;
	data["ci"] << this->ci->name;
	data["mask"] << this->mask;
	data["creator"] << this->creator;
	data.SetType("last_seen", Serialize::Data::DT_INT); data["last_seen"] << this->last_seen;
	data.SetType("created", Serialize::Data::DT_INT); data["created"] << this->created;
	data["data"] << this->AccessSerialize();
}

Serializable* ChanAccess::Unserialize(Serializable *obj, Serialize::Data &data)
{
	Anope::string provider, chan;

	data["provider"] >> provider;
	data["ci"] >>chan;

	ServiceReference<AccessProvider> aprovider("AccessProvider", provider);
	ChannelInfo *ci = ChannelInfo::Find(chan);
	if (!aprovider || !ci)
		return NULL;

	ChanAccess *access;
	if (obj)
		access = anope_dynamic_static_cast<ChanAccess *>(obj);
	else
		access = aprovider->Create();
	access->ci = ci;
	data["mask"] >> access->mask;
	data["creator"] >> access->creator;
	data["last_seen"] >> access->last_seen;
	data["created"] >> access->created;

	Anope::string adata;
	data["data"] >> adata;
	access->AccessUnserialize(adata);

	if (!obj)
		ci->AddAccess(access);
	return access;
}

bool ChanAccess::Matches(const User *u, const NickCore *acc) const
{
	bool is_mask = this->mask.find_first_of("!@?*") != Anope::string::npos;
	if (u && is_mask && Anope::Match(u->nick, this->mask))
		return true;
	else if (u && Anope::Match(u->GetDisplayedMask(), this->mask))
		return true;
	else if (acc)
		for (unsigned i = 0; i < acc->aliases->size(); ++i)
		{
			const NickAlias *na = acc->aliases->at(i);
			if (Anope::Match(na->nick, this->mask))
				return true;
		}
	return false;
}

bool ChanAccess::operator>(const ChanAccess &other) const
{
	const std::vector<Privilege> &privs = PrivilegeManager::GetPrivileges();
	for (unsigned i = privs.size(); i > 0; --i)
	{
		bool this_p = this->HasPriv(privs[i - 1].name),
			other_p = other.HasPriv(privs[i - 1].name);

		if (!this_p && !other_p)
			continue;
		else if (this_p && !other_p)
			return true;
		else
			return false;
	}

	return false;
}

bool ChanAccess::operator<(const ChanAccess &other) const
{
	const std::vector<Privilege> &privs = PrivilegeManager::GetPrivileges();
	for (unsigned i = privs.size(); i > 0; --i)
	{
		bool this_p = this->HasPriv(privs[i - 1].name),
			other_p = other.HasPriv(privs[i - 1].name);

		if (!this_p && !other_p)
			continue;
		else if (!this_p && other_p)
			return true;
		else
			return false;
	}	

	return false;
}

bool ChanAccess::operator>=(const ChanAccess &other) const
{
	const std::vector<Privilege> &privs = PrivilegeManager::GetPrivileges();
	for (unsigned i = privs.size(); i > 0; --i)
	{
		bool this_p = this->HasPriv(privs[i - 1].name),
			other_p = other.HasPriv(privs[i - 1].name);

		if (!this_p && !other_p)
			continue;
		else if (!this_p && other_p)
			return false;
		else
			return true;
	}

	return true;
}

bool ChanAccess::operator<=(const ChanAccess &other) const
{
	const std::vector<Privilege> &privs = PrivilegeManager::GetPrivileges();
	for (unsigned i = privs.size(); i > 0; --i)
	{
		bool this_p = this->HasPriv(privs[i - 1].name),
			other_p = other.HasPriv(privs[i - 1].name);

		if (!this_p && !other_p)
			continue;
		else if (this_p && !other_p)
			return false;
		else
			return true;
	}

	return true;
}

AccessGroup::AccessGroup() : std::vector<ChanAccess *>()
{
	this->ci = NULL;
	this->nc = NULL;
	this->super_admin = this->founder = false;
}

bool AccessGroup::HasPriv(const Anope::string &name) const
{
	if (this->super_admin)
		return true;
	else if (ci->GetLevel(name) == ACCESS_INVALID)
		return false;
	else if (this->founder)
		return true;
	EventReturn MOD_RESULT;
	FOREACH_RESULT(I_OnGroupCheckPriv, OnGroupCheckPriv(this, name));
	if (MOD_RESULT != EVENT_CONTINUE)
		return MOD_RESULT == EVENT_ALLOW;
	for (unsigned i = this->size(); i > 0; --i)
	{
		ChanAccess *access = this->at(i - 1);
		FOREACH_RESULT(I_OnCheckPriv, OnCheckPriv(access, name));
		if (MOD_RESULT == EVENT_ALLOW || access->HasPriv(name))
			return true;
	}
	return false;
}

const ChanAccess *AccessGroup::Highest() const
{
	const std::vector<Privilege> &privs = PrivilegeManager::GetPrivileges();
	for (unsigned i = privs.size(); i > 0; --i)
		for (unsigned j = this->size(); j > 0; --j)
			if (this->at(j - 1)->HasPriv(privs[i - 1].name))
				return this->at(j - 1);
	return NULL;
}

bool AccessGroup::operator>(const AccessGroup &other) const
{
	if (this->super_admin)
		return true;
	else if (other.super_admin)
		return false;
	else if (this->founder && !other.founder)
		return true;
	else if (!this->founder && other.founder)
		return false;
	const std::vector<Privilege> &privs = PrivilegeManager::GetPrivileges();
	for (unsigned i = privs.size(); i > 0; --i)
	{
		bool this_p = this->HasPriv(privs[i - 1].name),
			other_p = other.HasPriv(privs[i - 1].name);

		if (!this_p && !other_p)
			continue;
		else if (this_p && !other_p)
			return true;
		else
			return false;
	}

	return false;
}

bool AccessGroup::operator<(const AccessGroup &other) const
{
	if (other.super_admin)
		return true;
	else if (this->super_admin)
		return false;
	else if (other.founder && !this->founder)
		return true;
	else if (this->founder && !other.founder)
		return false;
	const std::vector<Privilege> &privs = PrivilegeManager::GetPrivileges();
	for (unsigned i = privs.size(); i > 0; --i)
	{
		bool this_p = this->HasPriv(privs[i - 1].name),
			other_p = other.HasPriv(privs[i - 1].name);

		if (!this_p && !other_p)
			continue;
		else if (!this_p && other_p)
			return true;
		else
			return false;
	}

	return false;
}

bool AccessGroup::operator>=(const AccessGroup &other) const
{
	if (this->super_admin)
		return true;
	else if (other.super_admin)
		return false;
	else if (this->founder)
		return true;
	else if (other.founder)
		return false;
	const std::vector<Privilege> &privs = PrivilegeManager::GetPrivileges();
	for (unsigned i = privs.size(); i > 0; --i)
	{
		bool this_p = this->HasPriv(privs[i - 1].name),
			other_p = other.HasPriv(privs[i - 1].name);

		if (!this_p && !other_p)
			continue;
		else if (other_p && !this_p)
			return false;
		else
			return true;
	}

	return true;
}

bool AccessGroup::operator<=(const AccessGroup &other) const
{
	if (other.super_admin)
		return true;
	else if (this->super_admin)
		return false;
	else if (other.founder)
		return true;
	else if (this->founder)
		return false;
	const std::vector<Privilege> &privs = PrivilegeManager::GetPrivileges();
	for (unsigned i = privs.size(); i > 0; --i)
	{
		bool this_p = this->HasPriv(privs[i - 1].name),
			other_p = other.HasPriv(privs[i - 1].name);

		if (!this_p && !other_p)
			continue;
		else if (this_p && !other_p)
			return false;
		else
			return true;
	}

	return true;
}

