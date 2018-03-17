/*************************************************************************
 *
 * Copyright 2019 Realm Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 **************************************************************************/

#include <realm/dictionary.hpp>

using namespace realm;

/****************************** ConstDictionary ******************************/

ConstDictionary::ConstDictionary(ConstDictionary&& other)
    : m_keys(std::move(other.m_keys))
    , m_values(std::move(other.m_values))
{
}

ConstDictionary::ConstDictionary(const ConstDictionary& other)
    : m_keys(other.m_keys)
    , m_values(other.m_values)
{
}

bool ConstDictionary::operator==(const ConstDictionary& other) const
{
    if (size() != other.size()) {
        return false;
    }
    auto i1 = begin();
    auto i2 = other.begin();
    auto e = end();
    while (i1 != e) {
        if (i1->first != i2->first) {
            return false;
        }
        if (i1->second != i2->second) {
            return false;
        }
        ++i1;
        ++i2;
    }

    return true;
}

Mixed ConstDictionary::get(Mixed key) const
{
    size_t m = m_keys.find_first(key);
    if (m == realm::npos) {
        throw std::out_of_range("Key not found");
    }
    return m_values.get(m);
}

ConstDictionary::Iterator ConstDictionary::begin() const
{
    return Iterator(this, 0);
}

ConstDictionary::Iterator ConstDictionary::end() const
{
    return Iterator(this, size());
}

/******************************** Dictionary *********************************/

void Dictionary::create()
{
    if (!m_keys.is_attached()) {
        m_keys.create();
        m_values.create();
    }
}

void Dictionary::destroy()
{
    if (m_keys.is_attached()) {
        _destroy();
    }
}

std::pair<ConstDictionary::Iterator, bool> Dictionary::insert(Mixed key, Mixed value)
{
    create();
    size_t m = m_keys.find_first(key);

    bool do_insert = (m == realm::npos);
    if (do_insert) {
        m = m_keys.size();
        m_keys.add(key);
        m_values.add(value);
    }
    else {
        m_values.set(m, value);
    }

    return {Iterator(this, m), do_insert};
}

Mixed Dictionary::operator[](Mixed key)
{
    create();
    size_t m = m_keys.find_first(key);
    if (m == realm::npos) {
        insert(key, {});
        return {};
    }
    return m_values.get(m);
}

void Dictionary::clear()
{
    if (size() > 0) {
        m_keys.clear();
        m_values.clear();
    }
}

/************************* ConstDictionary::Iterator *************************/

ConstDictionary::Iterator::pointer ConstDictionary::Iterator::operator->()
{
    REALM_ASSERT(m_pos < m_keys.size());
    m_val = std::make_pair(m_keys.get(m_pos), m_values.get(m_pos));
    return &m_val;
}
