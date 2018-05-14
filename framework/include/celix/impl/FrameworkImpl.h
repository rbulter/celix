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

#ifndef CXX_CELIX_IMPL_FRAMEWORKIMPL_H
#define CXX_CELIX_IMPL_FRAMEWORKIMPL_H

#include "celix_framework_factory.h"
#include "framework.h"

#include "celix/impl/BundleContextImpl.h"
#include "celix/impl/BundleImpl.h"

namespace celix {

    namespace impl {

        class FrameworkImpl : public celix::Framework {
        public:
            FrameworkImpl(celix_bundle_context_t *c_ctx) : owner{false} {
                bundleContext_getFramework(c_ctx, &this->c_fwm);
                this->setFrameworkContext();
            }

            FrameworkImpl(framework_t *c_fw) : owner{false} {
                //wrapper framework
                this->c_fwm = c_fw;
                //assume started framework
                this->setFrameworkContext();
            }

            FrameworkImpl(const FrameworkImpl&) = delete;
            FrameworkImpl& operator=(const FrameworkImpl&) = delete;
            FrameworkImpl(FrameworkImpl&&) = delete;
            FrameworkImpl& operator=(FrameworkImpl&&) = delete;

            FrameworkImpl(celix::Properties config) : owner{true} {
                //framework which also owns the underlining c framework
                auto c_config = properties_create();
                for (auto &pair : config) {
                    properties_set(c_config, pair.first.c_str(), pair.second.c_str());
                }
                this->c_fwm = frameworkFactory_newFramework(c_config); //should be started

                this->setFrameworkContext();
            };

            virtual ~FrameworkImpl() {
                if (this->owner && this->c_fwm != nullptr) {
                    framework_stop(this->c_fwm);
                    framework_waitForStop(this->c_fwm);
                    framework_destroy(this->c_fwm);
                }
            }

            virtual void start() noexcept override {
                framework_start(this->c_fwm);
            }

            virtual void stop() noexcept override {
                framework_stop(this->c_fwm);
            }

            virtual void waitForStop() noexcept override {
                framework_waitForStop(this->c_fwm);
            }
            //TODO also in c virtual void breakWaitForStops() noexcept = 0;

            std::string getUUID() const noexcept override {
                //TODO return std::string{celix_framework_getUUID(this->c_fwm)};
                return "TODO";
            }

            celix::BundleContext& getFrameworkContext() noexcept override {
                BundleContext &ctx = this->bundleContextsCache.at(0);
                return ctx;
            }

            celix::Bundle& getFrameworkBundle() noexcept override {
                if (this->fwBundle.size() == 0) {
                    celix_bundle_t* c_bnd = nullptr;
                    framework_getFrameworkBundle(this->c_fwm, &c_bnd);
                    this->fwBundle.emplace_back(c_bnd);

                }
                return this->fwBundle[0];
            }

        private:

            void setFrameworkContext() {
                //create and set framework bundle context (replace invalid bundle context)
                bundle_t *fwmBundle = nullptr;
                bundle_context_t *fwmCtx = nullptr;
                framework_getFrameworkBundle(this->c_fwm, &fwmBundle);
                bundle_getContext(fwmBundle, &fwmCtx);
                this->bundleContextsCache.emplace(std::piecewise_construct,
                                                    std::forward_as_tuple(0L),
                                                    std::forward_as_tuple(fwmCtx, *this));
            }


            bool owner;
            framework_t *c_fwm{nullptr};
            std::map<long,celix::impl::BundleContextImpl> bundleContextsCache{};
            std::vector<celix::impl::BundleImpl> fwBundle{}; //optional entry

        };
    }
}

#endif //CXX_CELIX_IMPL_FRAMEWORKIMPL_H
