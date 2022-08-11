/* Copyright (C) 2015-2022, Wazuh Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it
 * and/or modify it under the terms of the GNU General Public
 * License (version 2) as published by the FSF - Free Software
 * Foundation.
 */
#include "opBuilderHelperNetInfoAddress.hpp"

#include <optional>
#include <string>
#include <variant>

#include "syntax.hpp"

#include "protocolHandler.hpp"
#include <baseHelper.hpp>
#include <kvdb/kvdbManager.hpp>
#include <wdb/wdb.hpp>

namespace
{

// TODO: remove pre release or leave it just for testing
constexpr std::string_view STREAM_SOCK_PATH = "/tmp/testStream.socket";

enum class Name
{
    ADDRESS,
    NETMASK,
    BROADCAST
};

constexpr std::string_view getPath(Name field)
{
    switch (field)
    {
        case Name::ADDRESS: return "/address";
        case Name::NETMASK: return "/netmask";
        case Name::BROADCAST: return "/broadcast";
        default: return "";
    }
}

// when isIPv6 == true the third field will be set to 0, otherwise 1 for IPv4
// agent <agent_id> netaddr save <scan_ID>|<name>|isIPv6=false|add[i]|netm[i]|broad[i]
// agent 0001 netaddr save 1234|name|0|add0|netm0|broad0
bool sysNetAddresTableFill(base::Event event,
                           const std::string& agentId,
                           const std::string& scan_id,
                           const std::string& name,
                           const std::string& ipObjectPath,
                           std::shared_ptr<wazuhdb::WazuhDB> wdb,
                           bool isIPv6)
{
    // Cheking if AddresArray exists
    std::optional<std::vector<json::Json>> address_ar;
    try
    {
        address_ar = event->getArray(ipObjectPath + getPath(Name::ADDRESS).data());
    }
    catch (const std::runtime_error& e)
    {
        return false;
    }

    if (!address_ar.has_value())
    {
        return false;
    }

    std::string middleFieldName = isIPv6 ? "IPv6" : "IPv4";

    std::optional<std::vector<json::Json>> netmaskI;
    try
    {
        netmaskI = event->getArray(ipObjectPath + getPath(Name::NETMASK).data())
                       .value_or(std::vector<json::Json>());
    }
    catch (const std::runtime_error& e)
    {
        netmaskI = std::nullopt;
    }

    std::optional<std::vector<json::Json>> broadcastI;
    try
    {
        broadcastI = event->getArray(ipObjectPath + getPath(Name::BROADCAST).data())
                         .value_or(std::vector<json::Json>());
    }
    catch (const std::runtime_error& e)
    {
        broadcastI = std::nullopt;
    }

    for (size_t i = 0; i != address_ar.value().size(); ++i)
    {
        std::optional<std::string> addressIValue {};
        std::optional<std::string> netmaskIValue {"NULL"};
        std::optional<std::string> broadcastIValue {"NULL"};

        try
        {
            addressIValue = address_ar.value().at(i).getString();
        }
        catch (const std::out_of_range& e)
        {
            return false;
        }

        if (netmaskI.has_value())
        {
            try
            {
                netmaskIValue = netmaskI.value().at(i).getString();
            }
            catch (const std::out_of_range& e)
            {
                netmaskIValue = "NULL";
            }
        }

        if (broadcastI.has_value())
        {
            try
            {
                broadcastIValue = broadcastI.value().at(i).getString().value_or("NULL");
            }
            catch (const std::out_of_range& e)
            {
                broadcastIValue = "NULL";
            }
        }

        // We should still check if it's empty because value_or only checks nullopt
        std::string msg = fmt::format("agent {} netaddr save {}|{}|{}|{}|{}|{}",
                                      agentId,
                                      scan_id,
                                      name,
                                      isIPv6 ? "1" : "0",
                                      addressIValue.value(),
                                      netmaskIValue.value(),
                                      broadcastIValue.value());

        auto findEventResponse = wdb->tryQueryAndParseResult(msg);
        if (std::get<0>(findEventResponse) != wazuhdb::QueryResultCodes::OK)
        {
            return false;
        }
    }

    return true;
}

base::Expression opBuilderHelperNetInfoAddress(const std::any& definition, bool isIPv6)
{
    auto [targetField, name, rawParameters] = helper::base::extractDefinition(definition);
    auto parameters = helper::base::processParameters(rawParameters);

    // Assert expected number of parameters
    helper::base::checkParametersSize(parameters, 4);
    // Parameter type check
    // Agent_id
    helper::base::checkParameterType(parameters[0],
                                     helper::base::Parameter::Type::REFERENCE);
    // scan_id
    helper::base::checkParameterType(parameters[1],
                                     helper::base::Parameter::Type::REFERENCE);
    // name
    helper::base::checkParameterType(parameters[2],
                                     helper::base::Parameter::Type::REFERENCE);
    // array (IPv4 or IPv6)
    helper::base::checkParameterType(parameters[3],
                                     helper::base::Parameter::Type::REFERENCE);

    name = helper::base::formatHelperFilterName(name, targetField, parameters);

    // Tracing
    const auto successTrace = fmt::format("[{}] -> Success", name);
    const auto failureTrace = fmt::format(
        "[{}] -> Failure: Parameter doesn't exist or it has the wrong type: ", name);
    const auto failureTrace1 = fmt::format(
        "[{}] -> Failure: [{}] sysNetAddressTableFill error", name, targetField);
    const auto failureTrace2 = fmt::format(
        "[{}] -> Failure: [{}] couldn't assign result value", name, targetField);

    // EventPaths and mappedPaths can be set in buildtime
    auto wdb = std::make_shared<wazuhdb::WazuhDB>(STREAM_SOCK_PATH);

    // Return Term
    return base::Term<base::EngineOp>::create(
        name,
        [=,
         targetField = std::move(targetField),
         agent_id_path = parameters[0].m_value,
         scan_id_path = parameters[1].m_value,
         name_path = parameters[2].m_value,
         ipObject_path = parameters[3].m_value,
         wdb = std::move(wdb)](base::Event event) -> base::result::Result<base::Event>
        {
            // Checking values and saving them
            if (!event->exists(agent_id_path) || !event->isString(agent_id_path))
            {
                return base::result::makeFailure(event, failureTrace + agent_id_path);
            }
            const auto& agent_id = event->getString(agent_id_path).value_or("NULL");

            if (!event->exists(scan_id_path) || !event->isString(scan_id_path))
            {
                return base::result::makeFailure(event, failureTrace + scan_id_path);
            }
            const auto& scan_id = event->getString(scan_id_path).value_or("NULL");

            if (!event->exists(name_path) || !event->isString(name_path))
            {
                return base::result::makeFailure(event, failureTrace + name);
            }
            const auto& name = event->getString(name_path).value_or("NULL");

            // Cheking base object existence
            if (!event->exists(ipObject_path))
            {
                return base::result::makeFailure(event, failureTrace + ipObject_path);
            }

            auto resultExecution = sysNetAddresTableFill(
                event, agent_id, scan_id, name, ipObject_path, wdb, isIPv6);

            if (!resultExecution)
            {
                return base::result::makeFailure(event, failureTrace1);
            }

            try
            {
                event->setBool(resultExecution, targetField);
            }
            catch (const std::exception& e)
            {
                return base::result::makeFailure(event, failureTrace2);
            }

            return base::result::makeSuccess(event, successTrace);
        });
}

} // namespace

namespace builder::internals::builders
{

base::Expression opBuilderHelperSaveNetInfoIPv4(const std::any& definition)
{
    return opBuilderHelperNetInfoAddress(definition, false);
}

base::Expression opBuilderHelperSaveNetInfoIPv6(const std::any& definition)
{
    return opBuilderHelperNetInfoAddress(definition, true);
}

} // namespace builder::internals::builders
