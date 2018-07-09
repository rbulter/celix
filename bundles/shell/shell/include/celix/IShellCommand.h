/**
 *Licensed to the Apache Software Foundation (ASF) under one
 *or more contributor license agreements.  See the NOTICE file
 *distributed with this work for additional information
 *regarding copyright ownership.  The ASF licenses this file
 *to you under the Apache License, Version 2.0 (the
 *"License"); you may not use this file except in compliance
 *with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *Unless required by applicable law or agreed to in writing,
 *software distributed under the License is distributed on an
 *"AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 *specific language governing permissions and limitations
 *under the License.
 */

#include <string>
#include <iostream>

#include "command.h"

#ifndef CXX_CELIX_ISHELLCOMMAND_H
#define CXX_CELIX_ISHELLCOMMAND_H

namespace celix {

    class IShellCommand {
    public:
        static constexpr const char * const COMMAND_NAME = OSGI_SHELL_COMMAND_NAME;
        static constexpr const char * const COMMAND_USAGE = OSGI_SHELL_COMMAND_USAGE;
        static constexpr const char * const COMMAND_DESCRIPTION = OSGI_SHELL_COMMAND_DESCRIPTION;

        virtual ~IShellCommand() = default;
        virtual int executeCommand(const std::string &commandLine, std::ostream &out, std::ostream &err) = 0;
    };

}

#include "ShellCommandServiceAdapters.h"


#endif //CXX_CELIX_ISHELLCOMMAND_H
