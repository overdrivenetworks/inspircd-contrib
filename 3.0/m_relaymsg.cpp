/*
 * InspIRCd -- Internet Relay Chat Daemon
 *
 *   Copyright (C) 2020 James Lu <james@overdrivenetworks.com>
 *
 * This file is a module for InspIRCd.  InspIRCd is free software: you can
 * redistribute it and/or modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation, version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/// $ModAuthor: jlu5
/// $ModAuthorMail: james@overdrivenetworks.com
/// $ModDepends: core 3
/// $ModDesc: Provides the RELAYMSG command for stateless bridging

#include "inspircd.h"

class CommandRelayMsg : public Command {
private:
    std::bitset<UCHAR_MAX> invalid_chars_map;
    std::string invalid_chars = "!+%@&#$:'\"?*,.";

public:
    std::string fake_host;
    std::string fake_ident;
    std::string nick_glob;

    CommandRelayMsg(Module* parent) : Command(parent, "RELAYMSG", 3, 3)
    {
        syntax = "<channel> <nick> <text>";
        allow_empty_last_param = false;

        // Populate a map of disallowed nick characters. Based off m_chghost code
        for (std::string::const_iterator n = invalid_chars.begin(); n != invalid_chars.end(); n++) {
            invalid_chars_map.set(static_cast<unsigned char>(*n));
        }
    }

    std::string GetFakeHostmask(std::string& nick) {
        return InspIRCd::Format("%s!%s@%s", nick.c_str(), fake_ident.c_str(), fake_host.c_str());
    }

    CmdResult Handle(User* user, const CommandBase::Params& parameters)
    {
        std::string channame = parameters[0];
        std::string nick = parameters[1];
        std::string text = parameters[2];

        if (!user->HasPrivPermission("users/relaymsg"))
        {
            user->WriteNotice("*** You need the users/relaymsg permission to use this command.");
            return CMD_FAILURE;
        }

        Channel* channel = ServerInstance->FindChan(channame);
        // Make sure the channel exists and the sender is in the channel
        if (!channel)
        {
            user->WriteNumeric(Numerics::NoSuchChannel(channame));
            return CMD_FAILURE;
        }
        if (!channel->HasUser(user))
        {
            user->WriteNumeric(Numerics::CannotSendTo(channel, "You must be in the channel to use this command."));
            return CMD_FAILURE;
        }

        // Check that target nick is not already in use
        if (ServerInstance->FindNick(nick))
        {
            user->WriteNumeric(ERR_NICKNAMEINUSE, nick, "Nickname is already in use.");
            return CMD_FAILURE;
        }

        // Make sure the nick doesn't include any core IRC characters (e.g. *, !)
        // This should still be more flexible than regular nick checking - in particular
        // we want to allow "/" and "~" for relayers
        for (std::string::const_iterator x = nick.begin(); x != nick.end(); x++)
        {
            if (invalid_chars_map.test(static_cast<unsigned char>(*x)))
            {
                user->WriteNumeric(ERR_ERRONEUSNICKNAME, nick, "Invalid characters in spoofed nick");
                return CMD_FAILURE;
            }
        }

        // Check that the target nick matches relay nick glob
        if (!InspIRCd::Match(nick, nick_glob))
        {
            user->WriteNumeric(ERR_ERRONEUSNICKNAME, nick, InspIRCd::Format("Fake nickname must match nickglob %s", nick_glob.c_str()));
            return CMD_FAILURE;
        }

        // Send the message to everyone in the channel
        std::string fakeSource = GetFakeHostmask(nick);
        // TODO: preserve message tags sent by caller?
        ClientProtocol::Messages::Privmsg privmsg(fakeSource, channel, text);
        channel->Write(ServerInstance->GetRFCEvents().privmsg, privmsg);

        if (IS_LOCAL(user))
        {
            // Pass the message on to other servers
            CommandBase::Params params;
            params.push_back(channame);
            params.push_back(nick);
            params.push_back(text);

            ServerInstance->PI->SendEncapsulatedData("*", "RELAYMSG", params);
        }

        return CMD_SUCCESS;
    };
};

class ModuleRelayMsg : public Module
{
    CommandRelayMsg cmd;

public:
    ModuleRelayMsg() :
        cmd(this)
    {
    }

    void On005Numeric(std::map<std::string, std::string>& tokens) CXX11_OVERRIDE
    {
        tokens["RELAYMSG"];
    }

    void ReadConfig(ConfigStatus& status) CXX11_OVERRIDE
    {
        ConfigTag* tag = ServerInstance->Config->ConfValue("relaymsg");
        cmd.nick_glob = tag->getString("nickglob", "*/*");
        cmd.fake_ident = tag->getString("ident", "relay");
        cmd.fake_host = tag->getString("host", ServerInstance->Config->ServerName);

        if (!ServerInstance->IsIdent(cmd.fake_ident))
        {
            throw ModuleException("Invalid ident value for <relaymsg>");
        }
        if (!ServerInstance->IsIdent(cmd.fake_host))
        {
            throw ModuleException("Invalid host value for <relaymsg>");
        }
    }

    Version GetVersion() CXX11_OVERRIDE
    {
        return Version("Provides the RELAYMSG command for stateless bridging");
    }
};

MODULE_INIT(ModuleRelayMsg)
