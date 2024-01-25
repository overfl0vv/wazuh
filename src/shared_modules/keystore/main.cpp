/*
 * Wazuh keystore
 * Copyright (C) 2015, Wazuh Inc.
 * January 25, 2024.
 *
 * This program is free software; you can redistribute it
 * and/or modify it under the terms of the GNU General Public
 * License (version 2) as published by the FSF - Free Software
 * Foundation.
 */

#include "keyStore.hpp"
#include "argsParser.hpp"

int main(const int argc, const char* argv[])
{
    std::string family;
    std::string key;
    std::string value;

    try
    {
        CmdLineArgs args(argc, argv);

        family = args.getColumnFamily();
        key = args.getKey();
        value = args.getValue();
    }
    catch (const std::exception& e)
    {
        CmdLineArgs::showHelp();
        return 1;
    }

    Keystore keystore;
    keystore.put(family, key, value);

    return 0;
}
