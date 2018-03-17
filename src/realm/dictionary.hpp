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

#ifndef REALM_DICTIONARY_HPP
#define REALM_DICTIONARY_HPP

#include <realm/array.hpp>
#include <realm/array_mixed.hpp>
#include <realm/bplustree.hpp>

namespace realm {

class ConstDictionary {
public:
    class Iterator;

    ConstDictionary()
        : m_keys(Allocator::get_default())
        , m_values(Allocator::get_default())
    {
    }

    ConstDictionary(const ConstDictionary&);
    ConstDictionary(ConstDictionary&&);


    bool operator==(const ConstDictionary& other) const;
    bool operator!=(const ConstDictionary& other) const
    {
        return !(*this == other);
    }

    bool is_attached() const
    {
        return !m_keys.is_attached();
    }

    size_t size() const
    {
        return m_keys.is_attached() ? m_keys.size() : 0;
    }

    // throws std::out_of_range if key is not found
    Mixed get(Mixed key) const;

    Iterator begin() const;
    Iterator end() const;

protected:
    friend class ArrayDictionary;

    BPlusTree<Mixed> m_keys;
    BPlusTree<Mixed> m_values;

    void _destroy()
    {
        m_keys.destroy();
        m_values.destroy();
    }
};

class ConstDictionary::Iterator {
public:
    typedef std::forward_iterator_tag iterator_category;
    typedef const std::pair<Mixed, Mixed> value_type;
    typedef ptrdiff_t difference_type;
    typedef const value_type* pointer;
    typedef const value_type& reference;

    pointer operator->();

    reference operator*()
    {
        return *operator->();
    }

    Iterator& operator++()
    {
        m_pos++;
        return *this;
    }

    Iterator operator++(int)
    {
        Iterator tmp(*this);
        operator++();
        return tmp;
    }

    bool operator!=(const Iterator& rhs)
    {
        return m_pos != rhs.m_pos;
    }

    bool operator==(const Iterator& rhs)
    {
        return m_pos == rhs.m_pos;
    }

private:
    friend class ConstDictionary;
    friend class Dictionary;

    const BPlusTree<Mixed>& m_keys;
    const BPlusTree<Mixed>& m_values;
    size_t m_pos;
    std::pair<Mixed, Mixed> m_val;

    Iterator(const ConstDictionary* dict, size_t pos)
        : m_keys(dict->m_keys)
        , m_values(dict->m_values)
        , m_pos(pos)
    {
    }
};

class Dictionary : public ConstDictionary {
public:
    using ConstDictionary::ConstDictionary;

    ConstDictionary& operator=(const ConstDictionary& other);

    void create();
    void destroy();

    // first points to inserted/updated element.
    // second is true if the element was inserted
    std::pair<ConstDictionary::Iterator, bool> insert(Mixed key, Mixed value);
    // adds entry if key is not found
    Mixed operator[](Mixed key);


    void clear();
};
}

#endif /* SRC_REALM_DICTIONARY_HPP_ */
