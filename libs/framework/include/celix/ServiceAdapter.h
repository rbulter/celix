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

#include "celix/Constants.h"

#ifndef CXX_CELIX_SERIVCEADAPTOR_H
#define CXX_CELIX_SERIVCEADAPTOR_H

namespace celix {

    class IServiceAdapterFactoryBase {
    public:
        virtual ~IServiceAdapterFactoryBase() = default;
        virtual std::string serviceName() = 0; //TODO make const std::string?
        virtual std::string serviceLanguage() = 0;

    };

    class IServiceAdapterBase {
    public:
        virtual ~IServiceAdapterBase() = default;
    };

    template<typename I>
    class IServiceAdapter : public IServiceAdapterBase {
    public:
        virtual ~IServiceAdapter() = default;
        virtual I* adapt() = 0;
    };

    template<typename I /*from*/, typename T /*to -> register version*/>
    class IServiceRegistrationAdapterFactory : public IServiceAdapterFactoryBase {
    public:
        using fromType = I;
        using registeringType = T;
        virtual ~IServiceRegistrationAdapterFactory() = default;
        virtual std::string serviceVersion() = 0;
        virtual IServiceAdapter<T>* createAdapter(I *fromSvc) = 0;
    };

    template<typename I>
    class IServiceUsageAdapterFactory : public IServiceAdapterFactoryBase {
    public:
        using type = I;
        virtual ~IServiceUsageAdapterFactory() = default;
        virtual std::string serviceVersionRange() = 0;
        virtual IServiceAdapter<I>* createAdapter(void *registeredSvc) = 0;
    };

    /**
     * The default Service Registration Adaptor. This wrapper assumes that a SERVICE_NAME, SERVICE_VERSION is available.
     * It also assumes the service is a C++ service for registering and for using.
     *
     * It is valid to create the wrapper with a nulllptr svc object, but then the servicePointer() method result
     * will also be a nullptr.
     *
     * @tparam I The C++ Service Type
     */
    template<typename I>
    class DefaultServiceRegistrationAdapterFactory : public IServiceRegistrationAdapterFactory<I, I> {
    public:
        class DefaultServiceAdapter : public IServiceAdapter<I> {
        public:
            DefaultServiceAdapter(I *_svc) : svc{_svc} {}
            virtual ~DefaultServiceAdapter() = default;
            DefaultServiceAdapter(const DefaultServiceAdapter&) = delete;
            DefaultServiceAdapter& operator=(const DefaultServiceAdapter&) = delete;
            I* adapt() override { return svc; }
        private:
            I* svc;
        };

        virtual ~DefaultServiceRegistrationAdapterFactory() = default;
        std::string serviceName() override { return I::SERVICE_NAME; }
        std::string serviceVersion() override { return I::SERVICE_VERSION; }
        std::string serviceLanguage() override { return celix::Constants::SERVICE_CXX_LANG; }
        IServiceAdapter<I>* createAdapter(I* svc) override { return new DefaultServiceAdapter{svc}; }
    };


    template<typename I>
    IServiceRegistrationAdapterFactory<I,I>& serviceRegistrationAdapterFactoryFor(I */*dummy for infer*/) {
        static DefaultServiceRegistrationAdapterFactory<I> factory{};
        return factory;
    }

    /**
     * The default Service Usage Adaptor. This wrapper assumes that a SERVICE_NAME, SERVICE_VERSION is available.
     * It also assumes the service is a registered C++ service to be used for C++.
     *
     * @tparam I The C++ Service Type
     */
    template<typename I>
    class DefaultServiceUsageAdapterFactory : public IServiceUsageAdapterFactory<I> {
    public:
        class DefaultServiceAdapter : public IServiceAdapter<I> {
        public:
            DefaultServiceAdapter(void *_svc) : svc{static_cast<I*>(_svc)} {}
            virtual ~DefaultServiceAdapter() = default;
            DefaultServiceAdapter(const DefaultServiceAdapter&) = delete;
            DefaultServiceAdapter& operator=(const DefaultServiceAdapter&) = delete;
            I* adapt() override { return svc; }
        private:
            I* svc;
        };

        virtual ~DefaultServiceUsageAdapterFactory() = default;
        std::string serviceName() override { return I::SERVICE_NAME; }
        std::string serviceVersionRange() override { return ""; /* TODO std::string{"["} + I::SERVICE_VERSION + std::string{"]"};*/ }
        std::string serviceLanguage() override { return celix::Constants::SERVICE_CXX_LANG; }
        IServiceAdapter<I>* createAdapter(void *registeredSvc) override { return new DefaultServiceAdapter{registeredSvc}; }
    };

    template<typename I>
    IServiceUsageAdapterFactory<I>& serviceUsageAdapterFactoryFor(I* /*dummy_used_for_infer*/) {
        static DefaultServiceUsageAdapterFactory<I> factory{};
        return factory;
    }

    /* TODO enable/improve for a better error message when function using adaptors are used, but no adaptor can be created
    void* createServiceUsageAdapter(void *) {
        static_assert(false, "No matching createServiceUsageAdaptor found. Please provide one!");
        return nullptr;
    }

    void* createServiceRegistrationAdapter(void *) {
        static_assert(false, "No matching createServiceRegistrationAdaptor   found. Please provide one!");
        return nullptr;
    }
     */

}

#endif //CXX_CELIX_SERIVCEADAPTOR_H
