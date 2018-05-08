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

#ifndef CXX_CELIX_BUNDLECONTEXT_H
#define CXX_CELIX_BUNDLECONTEXT_H

#include <string>
#include <vector>
#include <functional>

#include "celix/Constants.h"
#include "celix/Properties.h"
#include "celix/Bundle.h"

namespace celix {

    //forward declarations
    class BundleContext;
    class Framework;
    namespace dm {
        class DependencyManager;
    }

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

    class BundleContext {
    public:
        virtual ~BundleContext(){};

        template<typename I>
        long registerService(const std::string &serviceName, I *svc, const std::string &version = "", Properties props = {}) noexcept {
            return this->registerServiceInternal(serviceName, svc, version, celix::Constants::SERVICE_CXX_LANG, std::move(props));
        }

        template<typename I>
        long registerCService(const std::string &serviceName, I *svc, const std::string &version = "", Properties props = {}) noexcept {
            return this->registerServiceInternal(serviceName, svc, version, celix::Constants::SERVICE_C_LANG, std::move(props));
        }

        template<typename I>
        long registerServiceForLang(const std::string &serviceName, I *svc, const std::string &version = "", const std::string &lang = celix::Constants::SERVICE_C_LANG, Properties props = {}) noexcept {
            return this->registerServiceInternal(serviceName, svc, version, lang, std::move(props));
        }

        //TODO register std::function ?

        virtual void unregisterService(long serviceId) noexcept = 0;

        //register service factory

        /**
        * track service for the provided service type, service name, optional version range and optional filter.
        * The highest ranking services will used for the callback.
        * If a new and higher ranking services the callback with be called again with the new service.
        * If a service is removed a the callback with be called with next highest ranking service or NULL as service.
        *
        * @param ctx The bundle context.
        * @param serviceName The required service name to track
        * @param serviceVersionRange Optional the service version range to track. Can be empty ("")
        * @param filter Optional the LDAP filter to use. Can be empty ("")
        * @param set is a required callback, which will be called when a new highest ranking service is set.
        * @return the tracker id or < 0 if unsuccessful.
        */
        template<typename I>
        long trackService(
                const std::string &serviceName,
                const std::string &versionRange,
                const std::string &filter,
                std::function<void(I *svc, const celix::Properties& props, const celix::Bundle &bnd)> set
        ) noexcept {
            return this->trackServiceInternal(serviceName, versionRange, filter, [set](void *voidSvc, const celix::Properties& props, const celix::Bundle &bnd) {
                I* typedSvc = static_cast<I*>(voidSvc);
                set(typedSvc, props, bnd);
            });
        }

        /**
         * track services for the provided serviceName and/or filter.
         *
         * @param ctx The bundle context.
         * @param serviceName The required service name to track
         * @param serviceVersionRange Optional the service version range to track
         * @param filter Optional the LDAP filter to use
         * @param callbackHandle The data pointer, which will be used in the callbacks
         * @param add is a required callback, which will be called when a service is added and initially for the existing service.
         * @param remove is a required callback, which will be called when a service is removed
         * @return the tracker id or < 0 if unsuccessful.
         */
        template<typename I>
        long trackServices(
                const std::string &serviceName,
                const std::string &versionRange,
                const std::string &filter,
                std::function<void(I *svc, const celix::Properties& props, const celix::Bundle &bnd)> add,
                std::function<void(I *svc, const celix::Properties& props, const celix::Bundle &bnd)> remove
        ) noexcept {
            return this->trackServicesInternal(serviceName, versionRange, filter,
                                               [add](void *voidSvc, const celix::Properties& props, const celix::Bundle &bnd) {
                                                   I *typedSvc = static_cast<I *>(voidSvc);
                                                   add(typedSvc, props, bnd);
                                               },
                                               [remove](void *voidSvc, const celix::Properties& props, const celix::Bundle &bnd) {
                                                   I *typedSvc = static_cast<I *>(voidSvc);
                                                   remove(typedSvc, props, bnd);
                                               }
            );
        }

        //TODO make add / remove service refs??
        //TODO missing lang for track services

        /**
         * Note use fucntion by const reference. Only used during the call.
         * @param serviceId
         * @param I
         * @return
         */
        template<typename I>
        bool useService(long serviceId, const std::string &serviceName /*sanity*/, const std::function<void(I &svc, const celix::Properties &props, const celix::Bundle &svcOwner)> &use) noexcept {
            std::string filter = std::string{"(service.id="} + std::to_string(serviceId) + std::string{")"};
            return this->useService<I>(serviceName, "", filter, use);
        }

        template<typename I>
        bool useService(const std::string &serviceName, const std::string &versionRange, const std::string &filter, const std::function<void(I &svc, const celix::Properties &props, const celix::Bundle &svcOwner)> &use) noexcept {
            return this->useServiceInternal(serviceName, versionRange, filter, [use](void *voidSvc, const celix::Properties &props, const celix::Bundle &svcOwner) {
                I *typedSvc = static_cast<I*>(voidSvc);
                use(*typedSvc, props, svcOwner);
            });
        }

        template<typename I>
        void useServices(const std::string &serviceName, const std::string &versionRange, const std::string &filter, const std::function<void(I &svc, const celix::Properties &props, const celix::Bundle &svcOwner)> &use) noexcept {
            this->useServicesInternal(serviceName, filter, versionRange, [use](void *voidSvc, const celix::Properties &props, const celix::Bundle &svcOwner) {
                I *typedSvc = static_cast<I*>(voidSvc);
                use(*typedSvc, props, svcOwner);
            });
        }

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
        virtual long registerServiceInternal(const std::string &serviceName, void *svc, const std::string &version, const std::string &lang, celix::Properties props) noexcept = 0;

        virtual long trackServiceInternal(const std::string &serviceName,
                                         const std::string &versionRange,
                                         const std::string &filter,
                                         std::function<void(void *svc, const celix::Properties &props, const celix::Bundle &bnd)> set) noexcept = 0;

        virtual long trackServicesInternal(
                const std::string &serviceName,
                const std::string &versionRange,
                const std::string &filter,
                std::function<void(void *svc, const celix::Properties &props, const celix::Bundle &bnd)> add,
                std::function<void(void *svc, const celix::Properties &props, const celix::Bundle &bnd)> remove
        ) noexcept = 0;

        virtual bool useServiceInternal(const std::string &serviceName, const std::string &versionRange, const std::string &filter, const std::function<void(void *svc, const celix::Properties &props, const celix::Bundle &svcOwner)> &use) noexcept = 0;
        virtual void useServicesInternal(const std::string &serviceName, const std::string &versionRange, const std::string &filter, const std::function<void(void *svc, const celix::Properties &props, const celix::Bundle &svcOwner)> &use) noexcept = 0;
    };

}

#endif //CXX_CELIX_BUNDLECONTEXT_H

#include "celix/impl/BundleContextImpl.h"
