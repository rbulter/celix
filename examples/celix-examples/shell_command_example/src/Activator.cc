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

#include <iostream>
#include <thread>
#include <atomic>
#include <vector>

#include "celix/BundleActivator.h"
#include "celix/IShellCommand.h"

namespace {
    class BundleActivator : public celix::IBundleActivator, public celix::IShellCommand {
    public:
        celix_status_t start(celix::BundleContext &_ctx)  override {
            this->ctx = &_ctx;
            celix::Properties props{};
            props[OSGI_SHELL_COMMAND_NAME] = "cxx_exmpl";
            svcId = ctx->registerService<celix::IShellCommand>(this, props);
            return  CELIX_SUCCESS;
        }

        celix_status_t  stop(celix::BundleContext &ctx) override {
            ctx.unregisterService(svcId);
            return CELIX_SUCCESS;
        }

        virtual int executeCommand(const std::string &, std::ostream &out, std::ostream &) override {
            ctx->useServices<celix::IShellCommand>([&out](celix::IShellCommand &) {
                out << "found a IShellCommandService" << std::endl;
                //TODO use useServicesWithOptionts -> useWithProperties !
            });
            return 0;
        }

    private:
        celix::BundleContext *ctx;
        long svcId{-1};
    };
}

CELIX_GEN_CXX_BUNDLE_ACTIVATOR(BundleActivator)
