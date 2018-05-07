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

#ifndef CELIX_IMPL_BUNDLECONTEXT_H
#define CELIX_IMPL_BUNDLECONTEXT_H

#include <mutex>
#include <cstring>

#include "bundle_context.h"
#include "service_tracker.h"

#include "celix/impl/BundleImpl.h"

namespace celix {

    namespace impl {

        static celix::Properties createFromCProps(const celix_properties_t *c_props) {
            celix::Properties result{};
            const char *key = nullptr;
            CELIX_PROPERTIES_FOR_EACH(const_cast<celix_properties_t*>(c_props), key) {
                result[key] = celix_properties_get(c_props, key);
            }
            return result;
        }

        class BundleContextImpl : public celix::BundleContext {
        public:
            BundleContextImpl(bundle_context_t *ctx) : c_ctx{ctx} {}

            virtual ~BundleContextImpl() {
                //NOTE no need to destroy the c bundle context -> done by c framework

                //clearing tracker entries
                {
                    std::lock_guard<std::mutex> lock{this->mutex};
                    for (auto &pair : this->trackEntries) {
                        celix_bundleContext_stopTracker(this->c_ctx, pair.first);
                    }
                    this->trackEntries.clear();
                }

                this->c_ctx = nullptr;
            }

            BundleContextImpl(const BundleContextImpl&) = delete;
            BundleContextImpl& operator=(const BundleContextImpl&) = delete;
            BundleContextImpl(BundleContextImpl&&) = delete;
            BundleContextImpl& operator=(BundleContextImpl&&) = delete;

            void unregisterService(long serviceId) noexcept override {
                celix_bundleContext_unregisterService(this->c_ctx, serviceId);
            }

            std::vector<long> findServices(const std::string &serviceName, const std::string &versionRange, const std::string &filter, const std::string &/*lang = ""*/) noexcept override  {
                std::vector<long> result{};
                auto use = [&result](void *, const celix::Properties &props, const celix::Bundle &) {
                    long id = celix::getProperty(props, OSGI_FRAMEWORK_SERVICE_ID, -1);
                    if (id >= 0) {
                        result.push_back(id);
                    }
                };
                this->useServicesInternal(serviceName, versionRange, filter, use);
                return result;
            }

            void stopTracker(long trackerId) noexcept override {
                std::lock_guard<std::mutex> lock{this->mutex};
                celix_bundleContext_stopTracker(this->c_ctx, trackerId);
                auto it = this->trackEntries.find(trackerId);
                if (it != this->trackEntries.end()) {
                    this->trackEntries.erase(it);
                }
            }

            std::string getProperty(const std::string &key, std::string defaultValue) noexcept  override  {
                const char *val = nullptr;
                bundleContext_getPropertyWithDefault(this->c_ctx, key.c_str(), defaultValue.c_str(), &val);
                return std::string{val};
            }

            bool isInvalid() const noexcept {
                return this->c_ctx == nullptr;
            }

            long registerEmbeddedBundle(
                    std::string /*id*/,
                    std::function<void(celix::BundleContext & ctx)> /*start*/,
                    std::function<void(celix::BundleContext & ctx)> /*stop*/,
                    celix::Properties /*manifest*/,
                    bool /*autoStart*/
            ) noexcept override {
                return -1; //TODO
            };

            void registerEmbeddedBundle(const celix::BundleRegistrationOptions &/*opts*/) noexcept override {
                //TODO
            }

            long installBundle(const std::string &bundleLocation, bool autoStart) noexcept override {
                long bndId = -1;
                if (this->c_ctx != nullptr) {
                    bundle_t *bnd = nullptr;
                    bundleContext_installBundle(this->c_ctx, bundleLocation.c_str(), &bnd);
                    if (bnd != nullptr) {
                        bundle_getBundleId(bnd, &bndId);
                        if (autoStart) {
                            bundle_start(bnd);
                        }
                    }
                }
                return bndId;
            }


            void useBundles(const std::function<void(const celix::Bundle &bnd)> &use) noexcept override {
                auto c_use = [](void *handle, const celix_bundle_t *c_bnd) {
                    auto *func =  static_cast<std::function<void(const celix::Bundle &bnd)>*>(handle);
                    auto m_bnd = const_cast<celix_bundle_t*>(c_bnd);
                    celix::impl::BundleImpl bnd{m_bnd};
                    (*func)(bnd);
                };
                celix_bundleContext_useBundles(this->c_ctx, (void*)(&use), c_use);
            }

            bool useBundle(long bundleId, const std::function<void(const celix::Bundle &bnd)> &use) noexcept override {
                auto c_use = [](void *handle, const celix_bundle_t *c_bnd) {
                    auto *func =  static_cast<std::function<void(const celix::Bundle &bnd)>*>(handle);
                    auto m_bnd = const_cast<celix_bundle_t*>(c_bnd);
                    celix::impl::BundleImpl bnd{m_bnd};
                    (*func)(bnd);
                };
                return celix_bundleContext_useBundle(this->c_ctx, bundleId, (void*)(&use), c_use);
            }

        protected:

            long registerServiceInternal(const std::string &serviceName, void *svc, const std::string &version, const std::string &lang, Properties props = {}) noexcept override {
                properties_t *c_props = properties_create();
                for (auto &pair : props) {
                    properties_set(c_props, pair.first.c_str(), pair.second.c_str());
                }
                return celix_bundleContext_registerServiceForLang(this->c_ctx, serviceName.c_str(), svc, version.c_str(), lang.c_str(), c_props);
            }

            long trackServiceInternal(const std::string &serviceName,
                                      const std::string &versionRange,
                                      const std::string &filter,
                                      std::function<void(void *svc, const celix::Properties &props, const celix::Bundle &bnd)> set) noexcept override  {
                celix_service_tracker_options_t opts;
                std::memset(&opts, 0, sizeof(opts));

                auto c_set = [](void *handle, void *svc, const celix_properties_t *c_props, const celix_bundle_t *c_bnd) {
                    auto *entry = static_cast<TrackEntry*>(handle);
                    celix::Properties props = createFromCProps(c_props);
                    auto m_bnd = const_cast<celix_bundle_t *>(c_bnd);
                    celix::impl::BundleImpl bnd{m_bnd};
                    (entry->set)(svc, props, bnd);
                };
                const char *cname = serviceName.empty() ? nullptr : serviceName.c_str();
                const char *crange = versionRange.empty() ? nullptr : versionRange.c_str();
                const char *cfilter = filter.empty() ? nullptr : filter.c_str();

                opts.serviceName = cname;
                opts.versionRange = crange;
                opts.filter = cfilter;
                opts.lang = CELIX_FRAMEWORK_SERVICE_CXX_LANGUAGE;

                auto te = std::unique_ptr<TrackEntry>{new TrackEntry{}};
                te->set = std::move(set);

                opts.callbackHandle = te.get();
                opts.setWithOwner = c_set;

                long id = celix_bundleContext_trackServicesWithOptions(this->c_ctx, &opts);
                if (id >= 0) {
                    std::lock_guard<std::mutex> lock{this->mutex};
                    this->trackEntries[id] = std::move(te);
                }
                return id;
            }

            long trackServicesInternal(
                    const std::string &serviceName,
                    const std::string &versionRange,
                    const std::string &filter,
                    std::function<void(void *svc, const celix::Properties &props, const celix::Bundle &bnd)> add,
                    std::function<void(void *svc, const celix::Properties &props, const celix::Bundle &bnd)> remove
            ) noexcept override {
                celix_service_tracker_options_t opts;
                std::memset(&opts, 0, sizeof(opts));

                auto c_add = [](void *handle, void *svc, const celix_properties_t *c_props, const celix_bundle_t *c_bnd) {
                    auto *entry = static_cast<TrackEntry*>(handle);
                    celix::Properties props = createFromCProps(c_props);
                    auto m_bnd = const_cast<celix_bundle_t *>(c_bnd);
                    celix::impl::BundleImpl bnd{m_bnd};
                    (entry->add)(svc, props, bnd);
                };
                auto c_remove = [](void *handle, void *svc, const celix_properties_t *c_props, const celix_bundle_t *c_bnd) {
                    auto *entry = static_cast<TrackEntry*>(handle);
                    celix::Properties props = createFromCProps(c_props);
                    auto m_bnd = const_cast<celix_bundle_t *>(c_bnd);
                    celix::impl::BundleImpl bnd{m_bnd};
                    (entry->remove)(svc, props, bnd);
                };

                const char *cname = serviceName.empty() ? nullptr : serviceName.c_str();
                const char *crange = versionRange.empty() ? nullptr : versionRange.c_str();
                const char *cfilter = filter.empty() ? nullptr : filter.c_str();

                opts.serviceName = cname;
                opts.versionRange = crange;
                opts.filter = cfilter;
                opts.lang = CELIX_FRAMEWORK_SERVICE_CXX_LANGUAGE;

                auto te = std::unique_ptr<TrackEntry>{new TrackEntry{}};
                te->add = std::move(add);
                te->remove = std::move(remove);

                opts.callbackHandle = te.get();
                opts.addWithOwner = c_add;
                opts.removeWithOwner = c_remove;

                long id = celix_bundleContext_trackServicesWithOptions(this->c_ctx, &opts);
                if (id >= 0) {
                    std::lock_guard<std::mutex> lock{this->mutex};
                    this->trackEntries[id] = std::move(te);
                }
                return id;
            }

            bool useServiceInternal(
                    const std::string &serviceName,
                    const std::string &versionRange,
                    const std::string &filter,
                    const std::function<void(void *svc, const celix::Properties &props, const celix::Bundle &svcOwner)> &use) noexcept override {
                auto c_use = [](void *handle, void *svc, const celix_properties_t *c_props, const celix_bundle_t *c_svcOwner) {
                    auto *fn = static_cast<const std::function<void(void *svc, const celix::Properties &props, const celix::Bundle &svcOwner)> *>(handle);
                    celix::Properties props = createFromCProps(c_props);
                    celix_bundle_t *m_bnd = const_cast<celix_bundle_t*>(c_svcOwner);
                    celix::impl::BundleImpl bnd{m_bnd};
                    (*fn)(svc, props, bnd);
                };
                const char *cname = serviceName.empty() ? nullptr : serviceName.c_str();
                const char *crange = versionRange.empty() ? nullptr : versionRange.c_str();
                const char *cfilter = filter.empty() ? nullptr : filter.c_str();
                return celix_bundleContext_useService(this->c_ctx, cname, crange, cfilter, (void*)(&use), c_use);
            }

            void useServicesInternal(
                    const std::string &serviceName,
                    const std::string &versionRange,
                    const std::string &filter,
                    const std::function<void(void *svc, const celix::Properties &props, const celix::Bundle &svcOwner)> &use) noexcept override {
                auto c_use = [](void *handle, void *svc, const celix_properties_t *c_props, const celix_bundle_t *c_svcOwner) {
                    auto *fn = static_cast<const std::function<void(void *svc, const celix::Properties &props, const celix::Bundle &svcOwner)> *>(handle);
                    celix::Properties props = createFromCProps(c_props);
                    celix_bundle_t *m_bnd = const_cast<celix_bundle_t*>(c_svcOwner);
                    celix::impl::BundleImpl bnd{m_bnd};
                    (*fn)(svc, props, bnd);
                };
                const char *cname = serviceName.empty() ? nullptr : serviceName.c_str();
                const char *crange = versionRange.empty() ? nullptr : versionRange.c_str();
                const char *cfilter = filter.empty() ? nullptr : filter.c_str();
                celix_bundleContext_useServices(this->c_ctx, cname, crange, cfilter, (void*)(&use), c_use);
            }

        private:
            bundle_context_t *c_ctx;

            struct TrackEntry {
                std::function<void(void *, const celix::Properties &, const celix::Bundle &)> set{};
                std::function<void(void *, const celix::Properties &, const celix::Bundle &)> add{};
                std::function<void(void *, const celix::Properties &, const celix::Bundle &)> remove{};
            };

            std::mutex mutex{};
            std::map<long,std::unique_ptr<TrackEntry>> trackEntries{};
        };
    }
}

#endif //CELIX_IMPL_BUNDLECONTEXT_H
