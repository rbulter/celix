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
#include <vector>
#include <functional>

#include "celix/Constants.h"
#include "celix/Properties.h"
#include "celix/Bundle.h"
#include "celix/IServiceFactory.h"

#ifndef CXX_CELIX_BUNDLECONTEXT_H
#define CXX_CELIX_BUNDLECONTEXT_H

namespace celix {

    //forward declarations
    class BundleContext;
    class Framework;
    namespace dm {
        class DependencyManager;
    }

    template<typename I>
    struct ServiceRegistrationOptions {
        using type = I;

        ServiceRegistrationOptions(I& _svc, const std::string& _serviceName) : svc{&_svc}, serviceName{_serviceName} {};
        ServiceRegistrationOptions(celix::IServiceFactory<I>& _factory, const std::string& _serviceName) : factory{&_factory}, serviceName{_serviceName} {};

        I *svc{nullptr};
        celix::IServiceFactory<I> *factory{nullptr};

        const std::string serviceName;

        celix::Properties properties{};
        std::string serviceVersion{};
        std::string serviceLanguage{celix::Constants::SERVICE_CXX_LANG};
    };

    template<typename I>
    struct ServiceFilterOptions {
        using type = I;

        ServiceFilterOptions(const std::string &_serviceName) : serviceName{_serviceName} {};

        std::string serviceName;

        std::string versionRange{};
        std::string filter{};
        std::string serviceLanguage{celix::Constants::SERVICE_CXX_LANG};
    };


    template<typename I>
    struct ServiceUseOptions {
        using type = I;

        ServiceUseOptions(const std::string &serviceName) : filter{ServiceFilterOptions<I>{serviceName}} {};

        ServiceFilterOptions<I> filter;

        /*
         * Callbacks
         */
        std::function<void(I &svc)> use{};
        std::function<void(I &svc, const celix::Properties &props)> useWithProperties{};
        std::function<void(I &svc, const celix::Properties &props, celix::Bundle &svcOwner)> useWithOwner{};
    };

    template<typename I>
    struct ServiceTrackingOptions {
        using type = I;

        ServiceTrackingOptions(const std::string serviceName) : filter{ServiceFilterOptions<I>{serviceName}} {};

        ServiceFilterOptions<I> filter;

        std::function<void(I* svc)> set{};
        std::function<void(I* svc)> add{};
        std::function<void(I* svc)> remove{};

        std::function<void(I* svc, const celix::Properties &props)> setWithProperties{};
        std::function<void(I* svc, const celix::Properties &props)> addWithProperties{};
        std::function<void(I* svc, const celix::Properties &props)> removeWithProperties{};

        std::function<void(I* svc, const celix::Properties &props, const celix::Bundle &svcOwner)> setWithOwner{};
        std::function<void(I* svc, const celix::Properties &props, const celix::Bundle &svcOwner)> addWithOwner{};
        std::function<void(I* svc, const celix::Properties &props, const celix::Bundle &svcOwner)> removeWithOwner{};
    };


    struct BundleRegistrationOptions {
        std::string id{};
        std::string name{};
        std::string version{};

        bool autoStart{true};

        std::function<void(celix::BundleContext &ctx)> start{};
        std::function<void(celix::BundleContext &ctx)> stop{};

        celix::Properties manifest{};

        //If manifest symbol and manifest len symbol is set, this is used instead of the properties as manifest
        std::string manifestSymbol{};
        std::string manifestLenSymbol{};

        std::string resourceSymbol{};
        std::string resourceLenSymbol{};
    };

    //opaque types to forward impl details to impl header
    struct ServiceRegistrationEntry;
    struct ServiceTrackingEntry;

    class BundleContext {
    public:
        virtual ~BundleContext(){};

        template<typename I>
        long registerService(I *svc, const std::string &serviceName, celix::Properties props = {}) noexcept;

        template<typename I>
        long registerCService(I *svc, const std::string &serviceName, celix::Properties props = {}) noexcept;

        template<typename I>
        long registerServiceFactory(celix::IServiceFactory<I> *svc, const std::string &serviceName, celix::Properties props = {});

        template<typename I>
        long registerCServiceFactory(celix::IServiceFactory<I> *svc, const std::string &serviceName, celix::Properties props = {});

        template<typename I>
        long registerServiceWithOptions(const celix::ServiceRegistrationOptions<I>& opts) noexcept;

        //TODO register std::function ?

        virtual void unregisterService(long serviceId) noexcept = 0;


        /**
        * track service for the provided service type, service name, optional version range and optional filter.
        * The highest ranking services will used for the callback.
        * If a new and higher ranking services the callback with be called again with the new service.
        * If a service is removed a the callback with be called with next highest ranking service or NULL as service.
        *
        * @param serviceName The required service name to track
        * @param set is a required callback, which will be called when a new highest ranking service is set.
        * @return the tracker id or < 0 if unsuccessful.
        */
        template<typename I>
        long trackService(const std::string &serviceName, std::function<void(I *svc)> set) noexcept;

        /**
         * track services for the provided serviceName and/or filter.
         *
         * @param serviceName The required service name to track
         * @param add is a required callback, which will be called when a service is added and initially for the existing service.
         * @param remove is a required callback, which will be called when a service is removed
         * @return the tracker id or < 0 if unsuccessful.
         */
        template<typename I>
        long trackServices(const std::string &serviceName, std::function<void(I *svc)> add, std::function<void(I *svc)> remove) noexcept;

        //TODO add trackCService(s) variants

        /**
         * track services using the provided tracking options
         *
         * @param opts The tracking options
         * @return the tracker id or < 0 if unsuccessful.
         */
        template<typename I>
        long trackServicesWithOptions(const celix::ServiceTrackingOptions<I>& opts) noexcept;


        /**
         * Note use function by const reference. Only used during the call.
         * @param serviceId
         * @param I
         * @return
         */
        template<typename I>
        bool useServiceWithId(long serviceId, const std::string &/*serviceName*/ /*sanity*/, const std::function<void(I &svc, const celix::Properties &props, const celix::Bundle &svcOwner)> &/*use*/) noexcept {
            std::string filter = std::string{"(service.id="} + std::to_string(serviceId) + std::string{")"};
            //TODO use useServiceWithOptions return this->useService<I>(serviceName, "", filter, use);
            return false;
        }

        template<typename I>
        bool useService(const std::string &serviceName, const std::function<void(I &svc, const celix::Properties &props, const celix::Bundle &svcOwner)> &use) noexcept {
            return this->useServiceInternal(serviceName, [use](void *voidSvc, const celix::Properties &props, const celix::Bundle &svcOwner) {
                I *typedSvc = static_cast<I*>(voidSvc);
                use(*typedSvc, props, svcOwner);
            });
        }

        template<typename I>
        void useServices(const std::string &serviceName, const std::function<void(I &svc, const celix::Properties &props, const celix::Bundle &svcOwner)> &use) noexcept {
            this->useServicesInternal(serviceName, [use](void *voidSvc, const celix::Properties &props, const celix::Bundle &svcOwner) {
                I *typedSvc = static_cast<I*>(voidSvc);
                use(*typedSvc, props, svcOwner);
            });
        }

        //TODO add useService(s)WithOptions
        //TODO add useCService(s) variants

        /**
         * Note ordered by service rank.
         */
        virtual std::vector<long> findServices(const std::string &serviceName, const std::string &versionRange = "", const std::string &filter = "", const std::string &lang = "") noexcept = 0;

        //TODO also support getting int, long, unsigned int, etc??
        virtual std::string getProperty(const std::string &key, std::string defaultValue = "") noexcept  = 0;

        //TODO options

        //track bundle
        //TODO

        //track service tracker
        //TODO

        /**
         * Stop the tracker with the provided track id.
         * Could be a service tracker, bundle tracker or service tracker tracker.
         * Only works for the trackers owned by the bundle of the bundle context.
         *
         * Will log a error if the provided tracker id is unknown. Will silently ignore trackerId < 0.
         */
        virtual void stopTracker(long trackerId) noexcept  = 0;

        virtual celix::Framework& getFramework() noexcept = 0;

        virtual celix::Bundle& getBundle() noexcept = 0;

        virtual celix::dm::DependencyManager& getDependencyManager() noexcept  = 0;

        //TODO
        //class celix::DependencyManager; //forward declaration TODO create
        //virtual celix::DependencyManager& getDependencyManager() const noexcept = 0;

        virtual long registerEmbeddedBundle(
                std::string id,
                std::function<void(celix::BundleContext& ctx)> start,
                std::function<void(celix::BundleContext& ctx)> stop,
                celix::Properties manifest = {},
                bool autoStart = true
        ) noexcept = 0;

        virtual void registerEmbeddedBundle(const celix::BundleRegistrationOptions &opts) noexcept = 0;

        virtual long installBundle(const std::string &bundleLocation, bool autoStart = true) noexcept = 0;

        virtual void useBundles(const std::function<void(const celix::Bundle &bnd)> &use) noexcept = 0;

        virtual bool useBundle(long bundleId, const std::function<void(const celix::Bundle &bnd)> &use) noexcept = 0;
    protected:
        virtual long registerServiceInternal(celix::ServiceRegistrationEntry &&entry) noexcept  = 0;

        virtual long trackServicesInternal(celix::ServiceTrackingEntry &&entry) noexcept  = 0;

        virtual bool useServiceInternal(const std::string &serviceName, const std::function<void(void *svc, const celix::Properties &props, const celix::Bundle &svcOwner)> &use) noexcept = 0;
        virtual void useServicesInternal(const std::string &serviceName, const std::function<void(void *svc, const celix::Properties &props, const celix::Bundle &svcOwner)> &use) noexcept = 0;
    };

}

#endif //CXX_CELIX_BUNDLECONTEXT_H

#include "celix/impl/BundleContextImpl.h"
