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

#include <sstream>

#include "command.h"
#include "celix/ServiceAdapter.h"
#include "celix/Constants.h"

#ifndef CXX_CELIX_SHELLCOMMANDSERVICEADAPTERS_H
#define CXX_CELIX_SHELLCOMMANDSERVICEADAPTERS_H

namespace celix {


    //From a C++ service to a C service when registering IShellCommand services.
    class ShellServiceRegistrationAdapterFactory : public celix::IServiceRegistrationAdapterFactory<celix::IShellCommand, command_service_t> {
    public:
        class ServiceAdapter : public celix::IServiceAdapter<command_service_t> {
        public:
            ServiceAdapter(celix::IShellCommand *svc) {
                cSvc.handle = static_cast<void*>(svc);
                cSvc.executeCommand = [](void* handle, char* commandLine, FILE *outStream, FILE *errStream) -> celix_status_t {
                    auto *local_cmd = static_cast<IShellCommand*>(handle);
                    std::ostringstream out;
                    std::ostringstream err;
                    int status = local_cmd->executeCommand(std::string{commandLine}, out, err);
                    std::fprintf(outStream, "%s", out.str().c_str());
                    std::fprintf(errStream, "%s", err.str().c_str());
                    return status;
                };
            }
            virtual ~ServiceAdapter() = default;
            command_service_t* adapt() override { return &cSvc; }
        private:
            command_service_t cSvc{};
        };

        virtual ~ShellServiceRegistrationAdapterFactory() = default;
        std::string serviceName() override { return OSGI_SHELL_COMMAND_SERVICE_NAME; }
        std::string serviceVersion() override { return ""; }
        std::string serviceLanguage() override { return celix::Constants::SERVICE_C_LANG; }
        celix::IServiceAdapter<command_service_t>* createAdapter(celix::IShellCommand* svc) override { return new ServiceAdapter{svc}; }
    };

    celix::IServiceRegistrationAdapterFactory<celix::IShellCommand, command_service_t>& serviceRegistrationAdapterFactoryFor(celix::IShellCommand */*dummy*/) {
        static ShellServiceRegistrationAdapterFactory factory{};
        return factory;
    }

    //From a C to a C++ services when using IShellCommand services.
    class ShellServiceUsageAdapterFactory : public celix::IServiceUsageAdapterFactory<celix::IShellCommand> {
    public:
        class ServiceAdapter : public celix::IServiceAdapter<celix::IShellCommand>, public celix::IShellCommand {
        public:
            ServiceAdapter(void *_svc) : cSvc{static_cast<command_service_t*>(_svc)} {}
            virtual ~ServiceAdapter() = default;

            ServiceAdapter(const ServiceAdapter&) = delete;
            ServiceAdapter& operator=(const ServiceAdapter&) = delete;

            celix::IShellCommand* adapt() override { return this; }

            int executeCommand(const std::string &commandLine, std::ostream &out, std::ostream &err) override {
                char *outBuf = nullptr;
                char *errBuf = nullptr;
                FILE *outStream = open_memstream(&outBuf, nullptr);
                FILE *errStream = open_memstream(&errBuf, nullptr);
                int status = cSvc->executeCommand(cSvc->handle, (char*)commandLine.c_str(), outStream, errStream);
                fflush(outStream);
                fclose(outStream);
                fflush(errStream);
                fclose(errStream);
                out << outBuf;
                err << errBuf;
                free(outBuf);
                free(errBuf);
                return status;
            }
        private:
            command_service_t* cSvc;
        };

        virtual ~ShellServiceUsageAdapterFactory() = default;
        std::string serviceName() override { return OSGI_SHELL_COMMAND_SERVICE_NAME; }
        std::string serviceVersionRange() override { return ""; }
        std::string serviceLanguage() override { return celix::Constants::SERVICE_C_LANG; }
        celix::IServiceAdapter<celix::IShellCommand>* createAdapter(void *registeredSvc) override { return new ServiceAdapter{registeredSvc}; }
    };

    celix::IServiceUsageAdapterFactory<celix::IShellCommand>& serviceUsageAdapterFactoryFor(celix::IShellCommand* /*dummy_used_for_infer*/) {
        static ShellServiceUsageAdapterFactory factory{};
        return factory;
    }
}


#endif //CXX_CELIX_SHELLCOMMANDSERVICEADAPTERS_H
