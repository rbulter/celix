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

#ifndef CELIX_IMPL_BUNDLECONTEXTIMPL_H
#define CELIX_IMPL_BUNDLECONTEXTIMPL_H

#include <mutex>
#include <cstring>
#include <memory>

#include "bundle_context.h"
#include "service_tracker.h"

#include "celix/impl/BundleImpl.h"
#include "celix/dm/DependencyManager.h"
#include "celix_service_factory.h"

namespace {
    static celix::Properties createFromCProps(const celix_properties_t *c_props) {
        celix::Properties result{};
        const char *key = nullptr;
        CELIX_PROPERTIES_FOR_EACH(const_cast<celix_properties_t*>(c_props), key) {
            result[key] = celix_properties_get(c_props, key);
        }
        return result;
    }

    struct ServiceTrackingEntryFunctions {
        std::function<void(void *)> set{};
        std::function<void(void *, const celix::Properties &)> setWithProperties{};
        std::function<void(void *, const celix::Properties &, const celix::Bundle &)> setWithOwner{};

        std::function<void(void *)> add{};
        std::function<void(void *, const celix::Properties &)> addWithProperties{};
        std::function<void(void *, const celix::Properties &, const celix::Bundle &)> addWithOwner{};

        std::function<void(void *)> remove{};
        std::function<void(void *, const celix::Properties &)> removeWithProperties{};
        std::function<void(void *, const celix::Properties &, const celix::Bundle &)> removeWithOwner{};
    };
}


namespace celix {

    struct ServiceRegistrationEntry {
        ServiceRegistrationEntry() {
            std::memset(&this->cOpts, 0, sizeof(this->cOpts));
            std::memset(&this->factory, 0, sizeof(this->factory));
        }
        celix_service_factory_t factory;
        celix_service_registration_options_t cOpts;
    };

    struct ServiceTrackingEntry {
        ServiceTrackingEntry() {
            std::memset(&this->cOpts, 0, sizeof(this->cOpts));
        }
        celix_service_tracking_options_t cOpts;
        std::unique_ptr<ServiceTrackingEntryFunctions> functions{nullptr};
    };

    namespace impl {

        class BundleContextImpl : public celix::BundleContext {
        public:
            BundleContextImpl(bundle_context_t *ctx, celix::Framework& _fw) : c_ctx(ctx), fw(_fw), bnd(c_ctx), dm(c_ctx) {}

            virtual ~BundleContextImpl() {
                //NOTE no need to destroy the c bundle context -> done by c framework

                {
                    //clearing service registration
                    std::lock_guard<std::mutex> lock{this->mutex};
                    for (auto &pair : this->registrationEntries) {
                        celix_bundleContext_unregisterService(this->c_ctx, pair.first);
                    }
                    this->registrationEntries.clear();
                }

                {
                    //clearing tracker entries
                    std::lock_guard<std::mutex> lock{this->mutex};
                    for (auto &pair : this->trackingEntries) {
                        celix_bundleContext_stopTracker(this->c_ctx, pair.first);
                    }
                    this->trackingEntries.clear();
                }

                this->c_ctx = nullptr;
            }

            BundleContextImpl(const BundleContextImpl&) = delete;
            BundleContextImpl& operator=(const BundleContextImpl&) = delete;
            BundleContextImpl(BundleContextImpl&&) = delete;
            BundleContextImpl& operator=(BundleContextImpl&&) = delete;

            void unregisterService(long serviceId) noexcept override {
                std::lock_guard<std::mutex> lock{this->mutex};
                celix_bundleContext_unregisterService(this->c_ctx, serviceId);
                auto it = this->registrationEntries.find(serviceId);
                if (it != this->registrationEntries.end()) {
                    this->registrationEntries.erase(it);
                }
            }

            std::vector<long> findServices(const std::string &/*serviceName*/, const std::string &/*versionRange*/, const std::string &/*filter*/, const std::string &/*lang = ""*/) noexcept override  {
                std::vector<long> result{};
//                auto use = [&result](void *, const celix::Properties &props, const celix::Bundle &) {
//                    long id = celix::getProperty(props, OSGI_FRAMEWORK_SERVICE_ID, -1);
//                    if (id >= 0) {
//                        result.push_back(id);
//                    }
//                };
                //TODO useServicesWithOptions this->useServicesInternal(serviceName, versionRange, filter, use);
                return result;
            }

            void stopTracker(long trackerId) noexcept override {
                std::lock_guard<std::mutex> lock{this->mutex};
                celix_bundleContext_stopTracker(this->c_ctx, trackerId);
                auto it = this->trackingEntries.find(trackerId);
                if (it != this->trackingEntries.end()) {
                    this->trackingEntries.erase(it);
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

            celix::Framework& getFramework() noexcept override {
                return this->fw;
            }

            celix::Bundle& getBundle() noexcept override {
                return this->bnd;
            };

            celix::dm::DependencyManager& getDependencyManager() noexcept override {
                return this->dm;
            }
        protected:

            long registerServiceInternal(celix::ServiceRegistrationEntry&& entry) noexcept  override {
                long svcId = celix_bundleContext_registerServiceWithOptions(this->c_ctx, &entry.cOpts);
                if (svcId >= 0) {
                    std::lock_guard<std::mutex> lock{this->mutex};
                    this->registrationEntries[svcId] = std::move(entry);
                }
                return svcId;
            }

            long trackServicesInternal(celix::ServiceTrackingEntry &&entry) noexcept override {
                long trkId = celix_bundleContext_trackServicesWithOptions(this->c_ctx, &entry.cOpts);
                if (trkId >= 0) {
                    std::lock_guard<std::mutex> lock{this->mutex};
                    this->trackingEntries[trkId] = std::move(entry);
                }
                return trkId;
            }

            bool useServiceInternal(
                    const std::string &serviceName,
                    const std::function<void(void *svc, const celix::Properties &props, const celix::Bundle &svcOwner)> &use) noexcept override {
                auto c_use = [](void *handle, void *svc, const celix_properties_t *c_props, const celix_bundle_t *c_svcOwner) {
                    auto *fn = static_cast<const std::function<void(void *svc, const celix::Properties &props, const celix::Bundle &svcOwner)> *>(handle);
                    celix::Properties props = createFromCProps(c_props);
                    celix_bundle_t *m_bnd = const_cast<celix_bundle_t*>(c_svcOwner);
                    celix::impl::BundleImpl bnd{m_bnd};
                    (*fn)(svc, props, bnd);
                };

                celix_service_use_options_t opts;
                std::memset(&opts, 0, sizeof(opts));

                opts.filter.serviceName = serviceName.empty() ? nullptr : serviceName.c_str();;
                opts.filter.serviceLanguage = celix::Constants::SERVICE_CXX_LANG;
                opts.callbackHandle = (void*)&use;
                opts.useWithOwner = c_use;

                return celix_bundleContext_useServiceWithOptions(this->c_ctx, &opts);
            }

            void useServicesInternal(
                    const std::string &serviceName,
                    const std::function<void(void *svc, const celix::Properties &props, const celix::Bundle &svcOwner)> &use) noexcept override {
                auto c_use = [](void *handle, void *svc, const celix_properties_t *c_props, const celix_bundle_t *c_svcOwner) {
                    auto *fn = static_cast<const std::function<void(void *svc, const celix::Properties &props, const celix::Bundle &svcOwner)> *>(handle);
                    celix::Properties props = createFromCProps(c_props);
                    celix_bundle_t *m_bnd = const_cast<celix_bundle_t*>(c_svcOwner);
                    celix::impl::BundleImpl bnd{m_bnd};
                    (*fn)(svc, props, bnd);
                };

                celix_service_use_options_t opts;
                std::memset(&opts, 0, sizeof(opts));

                opts.filter.serviceName = serviceName.empty() ? nullptr : serviceName.c_str();;
                opts.filter.serviceLanguage = celix::Constants::SERVICE_CXX_LANG;
                opts.callbackHandle = (void*)&use;
                opts.useWithOwner = c_use;

                celix_bundleContext_useServicesWithOptions(this->c_ctx, &opts);
            }

        private:
            //initialized in ctor
            bundle_context_t *c_ctx;
            celix::Framework& fw;
            celix::impl::BundleImpl bnd;
            celix::dm::DependencyManager dm;

            std::mutex mutex{};
            std::map<long,ServiceTrackingEntry> trackingEntries{};
            std::map<long,celix::ServiceRegistrationEntry> registrationEntries{};
        };
    }
}


template<typename I>
long celix::BundleContext::registerService(I *svc, const std::string &serviceName, Properties props) noexcept {
    celix::ServiceRegistrationOptions<I> opts{*svc, serviceName};
    opts.properties = std::move(props);
    return this->registerServiceWithOptions(opts);
}

template<typename I>
long celix::BundleContext::registerCService(I *svc, const std::string &serviceName, Properties props) noexcept {
    static_assert(std::is_pod<I>::value, "Service I must be a 'Plain Old Data' object");
    celix::ServiceRegistrationOptions<I> opts{*svc, serviceName};
    opts.properties = std::move(props);
    opts.serviceLanguage = celix::Constants::SERVICE_C_LANG;
    return this->registerServiceWithOptions(opts);
}

template<typename I>
long celix::BundleContext::registerServiceFactory(celix::IServiceFactory<I> *factory, const std::string &serviceName, celix::Properties props) {
    celix::ServiceRegistrationOptions<I> opts{factory, serviceName};
    opts.properties = std::move(props);
    return this->registerServiceWithOptions(opts);
}

template<typename I>
long celix::BundleContext::registerCServiceFactory(IServiceFactory<I> *factory, const std::string &serviceName, celix::Properties props) {
    static_assert(std::is_pod<I>::value, "Service I must be a 'Plain Old Data' object");
    celix::ServiceRegistrationOptions<I> opts{factory, serviceName};
    opts.properties = std::move(props);
    opts.serviceLanguage = celix::Constants::SERVICE_C_LANG;
    return this->registerServiceWithOptions(opts);
}

template<typename I>
long celix::BundleContext::registerServiceWithOptions(const celix::ServiceRegistrationOptions<I>& opts) noexcept {
    celix_properties_t *c_props = celix_properties_create();
    for (auto &pair : opts.properties) {
        celix_properties_set(c_props, pair.first.c_str(), pair.second.c_str());
    }

    celix::ServiceRegistrationEntry re{};

    re.cOpts = CELIX_EMPTY_SERVICE_REGISTRATION_OPTIONS;
    if (opts.svc != nullptr) {
        re.cOpts.svc = static_cast<void *>(opts.svc);
    } else if (opts.factory != nullptr) {
        auto c_getService = [](void *handle, const celix_bundle_t *requestingBundle, const celix_properties_t *svcProperties) -> void* {
            celix::IServiceFactory<I> *f = static_cast<celix::IServiceFactory<I>*>(handle);
            auto mbnd = const_cast<celix_bundle_t*>(requestingBundle);
            celix::impl::BundleImpl bundle{mbnd};
            celix::Properties props = createFromCProps(svcProperties);
            I *svc = f->getService(bundle, props);
            return static_cast<void*>(svc);
        };
        auto c_ungetService = [](void *handle, const celix_bundle_t *requestingBundle, const celix_properties_t *svcProperties) {
            celix::IServiceFactory<I> *f = static_cast<celix::IServiceFactory<I>*>(handle);
            auto mbnd = const_cast<celix_bundle_t*>(requestingBundle);
            celix::impl::BundleImpl bundle{mbnd};
            celix::Properties props = createFromCProps(svcProperties);
            f->ungetService(bundle, props);
        };
        re.factory.handle = static_cast<void*>(opts.factory);
        re.factory.getService = c_getService;
        re.factory.ungetService = c_ungetService;
        re.cOpts.factory = &re.factory;
    }

    re.cOpts.serviceName = opts.serviceName.c_str();
    re.cOpts.serviceVersion = opts.serviceVersion.c_str();
    re.cOpts.serviceLanguage = opts.serviceLanguage.c_str();
    re.cOpts.properties = c_props;

    return this->registerServiceInternal(std::move(re));
}

template<typename I>
long celix::BundleContext::trackService(const std::string &serviceName, std::function<void(I *svc)> set) noexcept {
    celix::ServiceTrackingOptions<I> opts{serviceName};
    opts.set = std::move(set);
    return this->trackServicesWithOptions<I>(opts);
}

template<typename I>
long celix::BundleContext::trackServices(const std::string &serviceName,
        std::function<void(I *svc)> add, std::function<void(I *svc)> remove) noexcept {
    celix::ServiceTrackingOptions<I> opts{serviceName};
    opts.add = std::move(add);
    opts.remove = std::move(remove);
    return this->trackServicesWithOptions<I>(opts);
}

template<typename I>
long celix::BundleContext::trackServicesWithOptions(const celix::ServiceTrackingOptions<I>& opts) {
    celix::ServiceTrackingEntry entry{};
    entry.functions = std::unique_ptr<ServiceTrackingEntryFunctions>{new ServiceTrackingEntryFunctions()};

    auto set = opts.set;
    if (set) {
        auto voidfunc = [set](void *voidSvc) {
            I *typedSvc = static_cast<I*>(voidSvc);
            set(typedSvc);
        };
        entry.functions->set = voidfunc;
        entry.cOpts.set = [](void *handle, void *svc) {
            auto *fentry = static_cast<ServiceTrackingEntryFunctions*>(handle);
            (fentry->set)(svc);
        };
    }

    auto setWithProperties = opts.setWithProperties;
    if (setWithProperties) {
        auto voidfunc = [setWithProperties](void *voidSvc, const celix::Properties &props) {
            I *typedSvc = static_cast<I*>(voidSvc);
            setWithProperties(typedSvc, props);
        };
        entry.functions->setWithProperties = voidfunc;
        entry.cOpts.setWithProperties = [](void *handle, void *svc, const celix_properties_t *c_props) {
            auto *fentry = static_cast<ServiceTrackingEntryFunctions*>(handle);
            celix::Properties props = createFromCProps(c_props);
            (fentry->setWithProperties)(svc, props);
        };
    }

    auto setWithOwner = opts.setWithOwner;
    if (setWithOwner) {
        auto voidfunc = [setWithOwner](void *voidSvc, const celix::Properties &props, const celix::Bundle &bnd) {
            I *typedSvc = static_cast<I*>(voidSvc);
            setWithOwner(typedSvc, props, bnd);
        };
        entry.functions->setWithOwner = voidfunc;
        entry.cOpts.setWithOwner = [](void *handle, void *svc, const celix_properties_t *c_props, const celix_bundle_t *c_bnd) {
            auto *fentry = static_cast<ServiceTrackingEntryFunctions*>(handle);
            celix::Properties props = createFromCProps(c_props);
            auto m_bnd = const_cast<celix_bundle_t *>(c_bnd);
            celix::impl::BundleImpl bnd{m_bnd};
            (fentry->setWithOwner)(svc, props, bnd);
        };
    }

    auto add = opts.add;
    if (add) {
        auto voidfunc = [add](void *voidSvc) {
            I *typedSvc = static_cast<I*>(voidSvc);
            add(typedSvc);
        };
        entry.functions->add = voidfunc;
        entry.cOpts.add = [](void *handle, void *svc) {
            auto *fentry = static_cast<ServiceTrackingEntryFunctions*>(handle);
            (fentry->add)(svc);
        };
    }

    auto addWithProperties = opts.addWithProperties;
    if (addWithProperties) {
        auto voidfunc = [addWithProperties](void *voidSvc, const celix::Properties &props) {
            I *typedSvc = static_cast<I*>(voidSvc);
            addWithProperties(typedSvc, props);
        };
        entry.functions->addWithProperties = voidfunc;
        entry.cOpts.addWithProperties = [](void *handle, void *svc, const celix_properties_t *c_props) {
            auto *fentry = static_cast<ServiceTrackingEntryFunctions*>(handle);
            celix::Properties props = createFromCProps(c_props);
            (fentry->addWithProperties)(svc, props);
        };
    }

    auto addWithOwner = opts.setWithOwner;
    if (addWithOwner) {
        auto voidfunc = [addWithOwner](void *voidSvc, const celix::Properties &props, const celix::Bundle &bnd) {
            I *typedSvc = static_cast<I*>(voidSvc);
            addWithOwner(typedSvc, props, bnd);
        };
        entry.functions->addWithOwner = voidfunc;
        entry.cOpts.addWithOwner = [](void *handle, void *svc, const celix_properties_t *c_props, const celix_bundle_t *c_bnd) {
            auto *fentry = static_cast<ServiceTrackingEntryFunctions*>(handle);
            celix::Properties props = createFromCProps(c_props);
            auto m_bnd = const_cast<celix_bundle_t *>(c_bnd);
            celix::impl::BundleImpl bnd{m_bnd};
            (fentry->addWithOwner)(svc, props, bnd);
        };
    }

    auto remove = opts.remove;
    if (remove) {
        auto voidfunc = [remove](void *voidSvc) {
            I *typedSvc = static_cast<I*>(voidSvc);
            remove(typedSvc);
        };
        entry.functions->remove = voidfunc;
        entry.cOpts.remove = [](void *handle, void *svc) {
            auto *fentry = static_cast<ServiceTrackingEntryFunctions*>(handle);
            (fentry->add)(svc);
        };
    }

    auto removeWithProperties = opts.removeWithProperties;
    if (removeWithProperties) {
        auto voidfunc = [removeWithProperties](void *voidSvc, const celix::Properties &props) {
            I *typedSvc = static_cast<I*>(voidSvc);
            removeWithProperties(typedSvc, props);
        };
        entry.functions->removeWithProperties = voidfunc;
        entry.cOpts.removeWithProperties = [](void *handle, void *svc, const celix_properties_t *c_props) {
            auto *fentry = static_cast<ServiceTrackingEntryFunctions*>(handle);
            celix::Properties props = createFromCProps(c_props);
            (fentry->removeWithProperties)(svc, props);
        };
    }

    auto removeWithOwner = opts.removeWithOwner;
    if (removeWithOwner) {
        auto voidfunc = [removeWithOwner](void *voidSvc, const celix::Properties &props, const celix::Bundle &bnd) {
            I *typedSvc = static_cast<I*>(voidSvc);
            removeWithOwner(typedSvc, props, bnd);
        };
        entry.functions->removeWithOwner = voidfunc;
        entry.cOpts.removeWithOwner = [](void *handle, void *svc, const celix_properties_t *c_props, const celix_bundle_t *c_bnd) {
            auto *fentry = static_cast<ServiceTrackingEntryFunctions*>(handle);
            celix::Properties props = createFromCProps(c_props);
            auto m_bnd = const_cast<celix_bundle_t *>(c_bnd);
            celix::impl::BundleImpl bnd{m_bnd};
            (fentry->removeWithOwner)(svc, props, bnd);
        };
    }

    entry.cOpts.filter.serviceName = opts.filter.serviceName.c_str();
    entry.cOpts.filter.serviceLanguage = opts.filter.serviceLanguage.c_str();
    entry.cOpts.filter.versionRange = opts.filter.versionRange.c_str();
    entry.cOpts.filter.filter = opts.filter.filter.c_str();

    entry.cOpts.callbackHandle = entry.functions.get();

    return this->trackServicesInternal(std::move(entry));
}


#endif //CELIX_IMPL_BUNDLECONTEXTIMPL_H
