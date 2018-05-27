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

namespace celix {

    namespace impl {
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

        struct ServiceRegistrationEntry {
            ServiceRegistrationEntry() {
                std::memset(&this->cOpts, 0, sizeof(this->cOpts));
                std::memset(&this->factory, 0, sizeof(this->factory));
            }

            celix_service_factory_t factory = {nullptr, nullptr, nullptr};
            celix_service_registration_options_t cOpts = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
        };

        struct ServiceTrackingEntry {
            celix_service_tracking_options_t cOpts{{nullptr, nullptr, nullptr, nullptr}, nullptr, nullptr, nullptr, nullptr,
                                                   nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
            std::unique_ptr<ServiceTrackingEntryFunctions> functions{nullptr};
        };
    }

    struct BundleContext::Impl {
        Impl(celix_bundle_context_t *_c_ctx, celix::Framework &_fw) : c_ctx(_c_ctx), fw(_fw), bnd(c_ctx), dm(c_ctx) {}
        ~Impl() = default;

        Impl(const Impl&) = delete;
        Impl& operator=(const Impl&) = delete;
        Impl(Impl&&) = delete;
        Impl& operator=(Impl&&) = delete;

        //initialized in ctor
        bundle_context_t *c_ctx;
        celix::Framework& fw;
        celix::impl::BundleImpl bnd;
        celix::dm::DependencyManager dm;

        std::mutex mutex{};
        std::map<long,celix::impl::ServiceTrackingEntry> trackingEntries{};
        std::map<long,celix::impl::ServiceRegistrationEntry> registrationEntries{};

        long registerServiceInternal(celix::impl::ServiceRegistrationEntry &&entry) noexcept;
        long trackServicesInternal(celix::impl::ServiceTrackingEntry &&entry) noexcept;
        bool useServiceInternal(const std::string &serviceName, const std::function<void(void *svc, const celix::Properties &props, const celix::Bundle &svcOwner)> &use) noexcept;
        void useServicesInternal(const std::string &serviceName, const std::function<void(void *svc, const celix::Properties &props, const celix::Bundle &svcOwner)> &use) noexcept;
    };
}

namespace {
    static celix::Properties createFromCProps(const celix_properties_t *c_props) {
        celix::Properties result{};
        const char *key = nullptr;
        CELIX_PROPERTIES_FOR_EACH(const_cast<celix_properties_t *>(c_props), key) {
            result[key] = celix_properties_get(c_props, key);
        }
        return result;
    }
}

inline celix::BundleContext::BundleContext(bundle_context_t *ctx, celix::Framework& fw) {
    this->pimpl = std::unique_ptr<celix::BundleContext::Impl>{new celix::BundleContext::Impl(ctx, fw)};
}

inline celix::BundleContext::~BundleContext() {
    //NOTE no need to destroy the c bundle context -> done by c framework
    {
        //clearing service registration
        std::lock_guard<std::mutex> lock{this->pimpl->mutex};
        for (auto &pair : this->pimpl->registrationEntries) {
            celix_bundleContext_unregisterService(this->pimpl->c_ctx, pair.first);
        }
        this->pimpl->registrationEntries.clear();
    }

    {
        //clearing tracker entries
        std::lock_guard<std::mutex> lock{this->pimpl->mutex};
        for (auto &pair : this->pimpl->trackingEntries) {
            celix_bundleContext_stopTracker(this->pimpl->c_ctx, pair.first);
        }
        this->pimpl->trackingEntries.clear();
    }

    this->pimpl->c_ctx = nullptr;
    this->pimpl = nullptr;
}


inline void celix::BundleContext::unregisterService(long serviceId) noexcept {
    std::lock_guard<std::mutex> lock{this->pimpl->mutex};
    celix_bundleContext_unregisterService(this->pimpl->c_ctx, serviceId);
    auto it = this->pimpl->registrationEntries.find(serviceId);
    if (it != this->pimpl->registrationEntries.end()) {
        this->pimpl->registrationEntries.erase(it);
    }
}

inline std::vector<long> celix::BundleContext::findServices(const std::string &/*serviceName*/, const std::string &/*versionRange*/, const std::string &/*filter*/, const std::string &/*lang = ""*/) noexcept {
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

inline void celix::BundleContext::stopTracker(long trackerId) noexcept {
    std::lock_guard<std::mutex> lock{this->pimpl->mutex};
    celix_bundleContext_stopTracker(this->pimpl->c_ctx, trackerId);
    auto it = this->pimpl->trackingEntries.find(trackerId);
    if (it != this->pimpl->trackingEntries.end()) {
        this->pimpl->trackingEntries.erase(it);
    }
}

inline std::string celix::BundleContext::getProperty(const std::string &key, std::string defaultValue) noexcept {
    const char *val = nullptr;
    bundleContext_getPropertyWithDefault(this->pimpl->c_ctx, key.c_str(), defaultValue.c_str(), &val);
    return std::string{val};
}


//    long celix::BundleContext::registerEmbeddedBundle(
//            std::string /*id*/,
//            std::function<void(celix::BundleContext & ctx)> /*start*/,
//            std::function<void(celix::BundleContext & ctx)> /*stop*/,
//            celix::Properties /*manifest*/,
//            bool /*autoStart*/
//    ) noexcept  {
//        return -1; //TODO
//    };
//
//    void registerEmbeddedBundle(const celix::BundleRegistrationOptions &/*opts*/) noexcept override {
//        //TODO
//    }

inline long celix::BundleContext::installBundle(const std::string &bundleLocation, bool autoStart) noexcept {
    long bndId = -1;
    if (this->pimpl->c_ctx != nullptr) {
        bundle_t *bnd = nullptr;
        bundleContext_installBundle(this->pimpl->c_ctx, bundleLocation.c_str(), &bnd);
        if (bnd != nullptr) {
            bundle_getBundleId(bnd, &bndId);
            if (autoStart) {
                bundle_start(bnd);
            }
        }
    }
    return bndId;
}


inline void celix::BundleContext::useBundles(const std::function<void(const celix::Bundle &bnd)> &use) noexcept {
    auto c_use = [](void *handle, const celix_bundle_t *c_bnd) {
        auto *func =  static_cast<std::function<void(const celix::Bundle &bnd)>*>(handle);
        auto m_bnd = const_cast<celix_bundle_t*>(c_bnd);
        celix::impl::BundleImpl bnd{m_bnd};
        (*func)(bnd);
    };
    celix_bundleContext_useBundles(this->pimpl->c_ctx, (void*)(&use), c_use);
}

inline bool celix::BundleContext::useBundle(long bundleId, const std::function<void(const celix::Bundle &bnd)> &use) noexcept {
    auto c_use = [](void *handle, const celix_bundle_t *c_bnd) {
        auto *func =  static_cast<std::function<void(const celix::Bundle &bnd)>*>(handle);
        auto m_bnd = const_cast<celix_bundle_t*>(c_bnd);
        celix::impl::BundleImpl bnd{m_bnd};
        (*func)(bnd);
    };
    return celix_bundleContext_useBundle(this->pimpl->c_ctx, bundleId, (void*)(&use), c_use);
}

inline celix::Framework& celix::BundleContext::getFramework() noexcept {
    return this->pimpl->fw;
}

inline celix::Bundle& celix::BundleContext::getBundle() noexcept {
    return this->pimpl->bnd;
};

inline celix::dm::DependencyManager& celix::BundleContext::getDependencyManager() noexcept {
    return this->pimpl->dm;
}

inline long celix::BundleContext::Impl::registerServiceInternal(celix::impl::ServiceRegistrationEntry&& entry) noexcept {
    long svcId = celix_bundleContext_registerServiceWithOptions(this->c_ctx, &entry.cOpts);
    if (svcId >= 0) {
        std::lock_guard<std::mutex> lock{this->mutex};
        this->registrationEntries[svcId] = std::move(entry);
    }
    return svcId;
}

inline long celix::BundleContext::Impl::trackServicesInternal(celix::impl::ServiceTrackingEntry &&entry) noexcept {
    long trkId = celix_bundleContext_trackServicesWithOptions(this->c_ctx, &entry.cOpts);
    if (trkId >= 0) {
        std::lock_guard<std::mutex> lock{this->mutex};
        this->trackingEntries[trkId] = std::move(entry);
    }
    return trkId;
}

inline bool celix::BundleContext::Impl::useServiceInternal(
        const std::string &serviceName,
        const std::function<void(void *svc, const celix::Properties &props, const celix::Bundle &svcOwner)> &use) noexcept {
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

inline void celix::BundleContext::Impl::useServicesInternal(
        const std::string &serviceName,
        const std::function<void(void *svc, const celix::Properties &props, const celix::Bundle &svcOwner)> &use) noexcept {
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

    celix::impl::ServiceRegistrationEntry re{};

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

    return this->pimpl->registerServiceInternal(std::move(re));
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
    celix::impl::ServiceTrackingEntry entry{};
    entry.functions = std::unique_ptr<celix::impl::ServiceTrackingEntryFunctions>{new celix::impl::ServiceTrackingEntryFunctions()};

    auto set = opts.set;
    if (set) {
        auto voidfunc = [set](void *voidSvc) {
            I *typedSvc = static_cast<I*>(voidSvc);
            set(typedSvc);
        };
        entry.functions->set = voidfunc;
        entry.cOpts.set = [](void *handle, void *svc) {
            auto *fentry = static_cast<celix::impl::ServiceTrackingEntryFunctions*>(handle);
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
            auto *fentry = static_cast<celix::impl::ServiceTrackingEntryFunctions*>(handle);
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
            auto *fentry = static_cast<celix::impl::ServiceTrackingEntryFunctions*>(handle);
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
            auto *fentry = static_cast<celix::impl::ServiceTrackingEntryFunctions*>(handle);
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
            auto *fentry = static_cast<celix::impl::ServiceTrackingEntryFunctions*>(handle);
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
            auto *fentry = static_cast<celix::impl::ServiceTrackingEntryFunctions*>(handle);
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
            auto *fentry = static_cast<celix::impl::ServiceTrackingEntryFunctions*>(handle);
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
            auto *fentry = static_cast<celix::impl::ServiceTrackingEntryFunctions*>(handle);
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
            auto *fentry = static_cast<celix::impl::ServiceTrackingEntryFunctions*>(handle);
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

    return this->pimpl->trackServicesInternal(std::move(entry));
}

template<typename I>
bool celix::BundleContext::useServiceWithId(long serviceId, const std::string &/*serviceName*/ /*sanity*/, const std::function<void(I &svc, const celix::Properties &props, const celix::Bundle &svcOwner)> &/*use*/) noexcept {
    std::string filter = std::string{"(service.id="} + std::to_string(serviceId) + std::string{")"};
    //TODO use useServiceWithOptions return this->useService<I>(serviceName, "", filter, use);
    return false;
}

template<typename I>
bool celix::BundleContext::useService(const std::string &serviceName, const std::function<void(I &svc, const celix::Properties &props, const celix::Bundle &svcOwner)> &use) noexcept {
    return this->pimpl->useServiceInternal(serviceName, [use](void *voidSvc, const celix::Properties &props, const celix::Bundle &svcOwner) {
        I *typedSvc = static_cast<I*>(voidSvc);
        use(*typedSvc, props, svcOwner);
    });
}

template<typename I>
void celix::BundleContext::useServices(const std::string &serviceName, const std::function<void(I &svc, const celix::Properties &props, const celix::Bundle &svcOwner)> &use) noexcept {
    this->pimpl->useServicesInternal(serviceName, [use](void *voidSvc, const celix::Properties &props, const celix::Bundle &svcOwner) {
        I *typedSvc = static_cast<I*>(voidSvc);
        use(*typedSvc, props, svcOwner);
    });
}


#endif //CELIX_IMPL_BUNDLECONTEXTIMPL_H
