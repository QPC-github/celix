/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *  KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#pragma once

#include <map>
#include <unordered_map>
#include <memory>

#include "celix_properties.h"
#include "celix_utils.h"
#include "hash_map.h"

namespace celix {

    /**
     * @brief A iterator for celix::Properties.
     */
    class PropertiesIterator {
    public:
        explicit PropertiesIterator(celix_properties_t* props) {
            iter = hashMapIterator_construct((hash_map_t*)props);
            next();
        }
        explicit PropertiesIterator(const celix_properties_t* props) {
            iter = hashMapIterator_construct((hash_map_t*)props);
            next();
        }

        PropertiesIterator& operator++() {
            next();
            return *this;
        }

        PropertiesIterator& operator*() {
            return *this;
        }

        bool operator==(const celix::PropertiesIterator& rhs) const {
            bool sameMap = iter.map == rhs.iter.map;
            bool sameIndex = iter.index == rhs.iter.index;
            bool oneIsEnd = end || rhs.end;
            if (oneIsEnd) {
                return sameMap && end && rhs.end;
            } else {
                return sameMap && sameIndex;
            }
        }

        bool operator!=(const celix::PropertiesIterator& rhs) const {
            return !operator==(rhs);
        }

        void next() {
            if (hashMapIterator_hasNext(&iter)) {
                auto* entry = hashMapIterator_nextEntry(&iter);
                auto *k = hashMapEntry_getKey(entry);
                auto *v = hashMapEntry_getValue(entry);
                first = std::string{(const char*)k};
                second = std::string{(const char*)v};
            } else {
                moveToEnd();
            }
        }

        void moveToEnd() {
            end = true;
            first = {};
            second = {};
        }

        std::string first{};
        std::string second{};
    private:
        hash_map_iterator_t iter{nullptr, nullptr, nullptr, 0, 0};
        bool end{false};
    };

    /**
     * @brief A collection of strings key values mainly used as meta data for registered services.
     * @note Not thread safe.
     */
    class Properties {
    public:
        using const_iterator = PropertiesIterator;

        class ValueRef {
        public:
            ValueRef(std::shared_ptr<celix_properties_t> _props, std::string _key) : props{std::move(_props)}, key{std::move(_key)} {}
            ValueRef& operator=(const std::string& value) {
                celix_properties_set(props.get(), key.c_str(), value.c_str());
                return *this;
            }

            const char* getValue() const {
                return celix_properties_get(props.get(), key.c_str(), nullptr);
            }

            operator std::string() const {
                auto *cstr = getValue();
                return cstr == nullptr ? std::string{} : std::string{cstr};
            }
        private:
            std::shared_ptr<celix_properties_t> props;
            std::string key;
        };


        Properties() : cProps{celix_properties_create(), [](celix_properties_t* p) { celix_properties_destroy(p); }} {}

        Properties(Properties&&) = default;
        Properties& operator=(Properties&&) = default;

        Properties& operator=(const Properties &rhs) {
            if (this != &rhs) {
                cProps = std::shared_ptr<celix_properties_t>{celix_properties_copy(rhs.cProps.get()), [](celix_properties_t* p) { celix_properties_destroy(p); }};
            }
            return *this;
        }

        Properties(const Properties& rhs) :
            cProps{celix_properties_copy(rhs.cProps.get()), [](celix_properties_t* p) { celix_properties_destroy(p); }} {}

        /**
         * @brief Wraps C properties, but does not take ownership -> dtor will not destroy properties
         */
        static std::shared_ptr<const Properties> wrap(const celix_properties_t* wrapProps) {
            auto* cp = const_cast<celix_properties_t*>(wrapProps);
            return std::shared_ptr<const Properties>{new Properties{cp}};
        }

        /**
         * Get the C properties object.
         *
         * @warning Try not the depend on the C API from a C++ bundle. If features are missing these should be added to
         * the C++ API.
         */
        celix_properties_t* getCProperties() const {
            return cProps.get();
        }

        /**
         * @brief Get the value for a property key
         */
        ValueRef operator[](std::string key) {
            return ValueRef{cProps, std::move(key)};
        }

        /**
         * @brief begin iterator
         */
        const_iterator begin() const noexcept {
            return PropertiesIterator{cProps.get()};
        }

        /**
         * @brief end iterator
         */
        const_iterator end() const noexcept {
            auto iter = PropertiesIterator{cProps.get()};
            iter.moveToEnd();
            return iter;
        }

        /**
         * @brief constant begin iterator
         */
        const_iterator cbegin() const noexcept {
            return PropertiesIterator{cProps.get()};
        }

        /**
         * @brief constant end iterator
         */
        const_iterator cend() const noexcept {
            auto iter = PropertiesIterator{cProps.get()};
            iter.moveToEnd();
            return iter;
        }

        /**
         * @brief Get the value for a property key or return the defaultValue if the key does not exists.
         */
        std::string get(const std::string& key, const std::string& defaultValue = {}) const {
            const char* found = celix_properties_get(cProps.get(), key.c_str(), nullptr);
            return found == nullptr ? std::string{defaultValue} : std::string{found};
        }

        /**
         * @brief Get the value as long for a property key or return the defaultValue if the key does not exists.
         */
        long getAsLong(const std::string& key, long defaultValue) const {
            return celix_properties_getAsLong(cProps.get(), key.c_str(), defaultValue);
        }

        /**
         * @brief Get the value as double for a property key or return the defaultValue if the key does not exists.
         */
        double getAsDouble(const std::string &key, double defaultValue) const {
            return celix_properties_getAsDouble(cProps.get(), key.c_str(), defaultValue);
        }

        /**
         * @brief Get the value as bool for a property key or return the defaultValue if the key does not exists.
         */
        bool getAsBool(const std::string &key, bool defaultValue) const {
            return celix_properties_getAsBool(cProps.get(), key.c_str(), defaultValue);
        }

        /**
         * @brief Sets a property
         */
        void set(const std::string& key, const std::string& value) {
            celix_properties_set(cProps.get(), key.c_str(), value.c_str());
        }

        /**
         * @brief Sets a property
         */
        void set(const std::string& key, const char* value) {
            celix_properties_set(cProps.get(), key.c_str(), value);
        }

        /**
         * @brief Sets a bool property
         */
        void set(const std::string& key, bool value) {
            celix_properties_setBool(cProps.get(), key.c_str(), value);
        }

        /**
         * @brief Sets a T property. Will use std::to_string to convert the value to string.
         */
        template<typename T>
        void set(const std::string& key, T value) {
            using namespace std;
            celix_properties_set(cProps.get(), key.c_str(), to_string(value).c_str());
        }

        /**
         * @brief Returns the nr of properties.
         */
        std::size_t size() const {
            return celix_properties_size(cProps.get());
        }
    private:
        explicit Properties(celix_properties_t* props) : cProps{props, [](celix_properties_t*) { /*nop*/ }} {}

        std::shared_ptr<celix_properties_t> cProps;
    };

}

inline std::ostream& operator<<(std::ostream& os, const ::celix::Properties::ValueRef& ref)
{
    os << std::string{ref.getValue()};
    return os;
}