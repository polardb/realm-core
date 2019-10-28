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

class ConstDictionary : public ArrayParent {
public:
    class Iterator;

    ConstDictionary()
        : m_keys(Allocator::get_default())
        , m_values(Allocator::get_default())
    {
        ColKey null_col_key;
        m_col_ndx = null_col_key.get_index();
    }
    ConstDictionary(const ConstObj& obj, ColKey col_key)
        : m_obj(obj)
        , m_col_ndx(col_key.get_index())
        , m_keys(obj.get_alloc())
        , m_values(obj.get_alloc())
    {
        size_t ndx = m_obj.get_row_ndx();
        m_keys.set_parent(this, ndx * 2);
        m_values.set_parent(this, ndx * 2 + 1);
        init_from_parent();
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

    size_t size() const;

    // throws std::out_of_range if key is not found
    Mixed get(Mixed key) const;

    Iterator begin() const;
    Iterator end() const;

protected:
    friend class ArrayDictionary;

    Obj m_obj;
    ColKey::Idx m_col_ndx;
    mutable uint_fast64_t m_content_version = 0;
    mutable BPlusTree<Mixed> m_keys;
    mutable BPlusTree<Mixed> m_values;

    void _destroy()
    {
        m_keys.destroy();
        m_values.destroy();
    }

    void update_if_needed() const;
    void init_from_parent() const;

    void update_child_ref(size_t ndx, ref_type new_ref) override;
    ref_type get_child_ref(size_t ndx) const noexcept override;
    std::pair<ref_type, size_t> get_to_dot_parent(size_t) const override;
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
    class MixedRef {
    public:
        operator Mixed()
        {
            return m_dict._get(m_ndx);
        }
        MixedRef& operator=(Mixed val)
        {
            m_dict._set(m_ndx, val);
            return *this;
        }

    private:
        friend class Dictionary;
        MixedRef(Dictionary& dict, size_t ndx)
            : m_dict(dict)
            , m_ndx(ndx)
        {
        }
        Dictionary& m_dict;
        size_t m_ndx;
    };
    using ConstDictionary::ConstDictionary;

    ConstDictionary& operator=(const ConstDictionary& other);

    void create();
    void destroy();

    // first points to inserted/updated element.
    // second is true if the element was inserted
    std::pair<ConstDictionary::Iterator, bool> insert(Mixed key, Mixed value);
    void erase(Mixed key);
    // adds entry if key is not found
    MixedRef operator[](Mixed key);

    void clear();

private:
    friend class MixedRef;
    Mixed _get(size_t ndx) const
    {
        return m_values.get(ndx);
    }
    void _set(size_t ndx, Mixed value)
    {
        m_values.set(ndx, value);
    }
};
}

#endif /* SRC_REALM_DICTIONARY_HPP_ */
