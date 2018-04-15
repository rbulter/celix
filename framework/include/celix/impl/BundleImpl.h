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

#ifndef CELIX_IMPL_BUNDLEIMPL_H
#define CELIX_IMPL_BUNDLEIMPL_H

#include "celix_bundle.h"

namespace celix {

    //forward declaration

    namespace impl {
        class BundleImpl : public celix::Bundle {
        public:
            BundleImpl(celix_bundle_t *b) : c_bnd{b} {
                framework_t *c_fw{nullptr};
                bundle_getFramework(this->c_bnd, &c_fw);
                //this->fw = std::unique_ptr<celix::impl::Framework>{new celix::impl::Framework(c_fw, false)};
            }

            virtual ~BundleImpl() {
                //no need to destroy the c bundle context -> done by c framework
                this->c_bnd = nullptr;
            }

            bool isSystemBundle() const noexcept  override {
                bool r;
                bundle_isSystemBundle(this->c_bnd, &r);
                return r;
            }

            void * getHandle() const noexcept override {
                return bundle_getHandle(this->c_bnd);
            }

            BundleState getState() const noexcept  override {
                bundle_state_e c_state;
                bundle_getState(this->c_bnd, &c_state);
                return this->fromCState(c_state);
            }

            long getBundleId() const noexcept  override {
                long id{-1};
                bundle_getBundleId(this->c_bnd, &id);
                return id;
            }

            std::string getBundleLocation() const noexcept  override {
                std::string location{};
                const char *loc = nullptr;
                bundle_getBundleLocation(this->c_bnd, &loc);
                if (loc != nullptr) {
                    location = std::string{loc};
                }
                return location;
            }

            std::string getBundleCache() const noexcept  override {
                std::string cache{};
                const char *c = celix_bundle_getEntry(this->c_bnd, ".");
                if (c != nullptr) {
                    cache = std::string{c};
                }
                return cache;
            }

            std::string getBundleName() const noexcept override {
                std::string name{};
                module_pt mod = nullptr;
                bundle_getCurrentModule(this->c_bnd, &mod);
                if (mod != nullptr) {
                    name = module_getId(mod);
                }
                return name;
            }

            std::string getBundleSymbolicName() const noexcept override {
                std::string name{};
                module_pt mod = nullptr;
                bundle_getCurrentModule(this->c_bnd, &mod);
                if (mod != nullptr) {
                    const char *n = nullptr;
                    module_getSymbolicName(mod, &n);
                    if (n != nullptr) {
                        name = n;
                    }
                }
                return name;
            }

            std::string getBundleVersion() const noexcept override {
                return std::string{}; //TODO
//                std::string version{};
//                module_pt mod = nullptr;
//                bundle_getCurrentModule(this->c_bnd, &mod);
//                if (mod != nullptr) {
//                    auto version = module_getVersion(mod);
//                    //TODO
//                }
//                return version;
            }

            celix::Properties getManifestAsProperties() const noexcept  override {
                return celix::Properties{}; //TODO
            }

            celix::Framework* getFramework() const noexcept override {
//                return this->fw;
                return nullptr; //TODO
            }

            void start() noexcept override {
                bundle_start(this->c_bnd);
            }

            void stop() noexcept override {
                bundle_stop(this->c_bnd);
            }

            void uninstall() noexcept override {
                bundle_uninstall(this->c_bnd);
            }

        private:
            BundleState fromCState(bundle_state_e c_state) const {
                switch(c_state) {
                    case OSGI_FRAMEWORK_BUNDLE_UNKNOWN:
                        return BundleState::UNKNOWN;
                    case OSGI_FRAMEWORK_BUNDLE_UNINSTALLED:
                        return BundleState::INSTALLED;
                    case OSGI_FRAMEWORK_BUNDLE_INSTALLED:
                        return BundleState::INSTALLED;
                    case OSGI_FRAMEWORK_BUNDLE_RESOLVED:
                        return BundleState::RESOLVED;
                    case OSGI_FRAMEWORK_BUNDLE_STOPPING:
                        return BundleState::STOPPING;
                    case OSGI_FRAMEWORK_BUNDLE_ACTIVE:
                        return BundleState::ACTIVE;
                    case OSGI_FRAMEWORK_BUNDLE_STARTING:
                        return BundleState::STARTING;
                    default:
                        ;//passs
                }
                return BundleState::UNKNOWN;
            };

            celix_bundle_t *c_bnd;
//            framework_t *c_fw{nullptr};
//            std::unique_ptr<celix::impl::Framework> fw{nullptr};
        };
    }
}

#endif //CELIX_IMPL_BUNDLEIMPL_H
