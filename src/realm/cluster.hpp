/*************************************************************************
 *
 * Copyright 2016 Realm Inc.
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

#ifndef REALM_CLUSTER_HPP
#define REALM_CLUSTER_HPP

#include <realm/keys.hpp>
#include <realm/array.hpp>
#include <realm/array_unsigned.hpp>
#include <realm/data_type.hpp>
#include <realm/column_type_traits.hpp>

namespace realm {

class Spec;
class Table;
class Cluster;
class ClusterNodeInner;
class ClusterTree;
class ColumnAttrMask;
struct CascadeState;

class ClusterNode : public Array {
public:
    // This structure is used to bring information back to the upper nodes when
    // inserting new objects or finding existing ones.
    struct State {
        int64_t split_key; // When a node is split, this variable holds the value of the
                           // first key in the new node. (Relative to the key offset)
        MemRef mem;        // MemRef to the Cluster holding the new/found object
        size_t index;      // The index within the Cluster at which the object is stored.
    };

    struct IteratorState {
        IteratorState(Cluster& leaf)
            : m_current_leaf(leaf)
        {
        }
        IteratorState(const IteratorState&);
        void clear();

        Cluster& m_current_leaf;
        int64_t m_key_offset = 0;
        size_t m_current_index = 0;
    };

    ClusterNode(uint64_t offset, Allocator& allocator, const ClusterTree& tree_top)
        : Array(allocator)
        , m_tree_top(tree_top)
        , m_keys(allocator)
        , m_offset(offset)
    {
        m_keys.set_parent(this, 0);
    }
    virtual ~ClusterNode()
    {
    }
    void init_from_parent()
    {
        ref_type ref = get_ref_from_parent();
        char* header = m_alloc.translate(ref);
        init(MemRef(header, ref, m_alloc));
    }
    int64_t get_key_value(size_t ndx) const
    {
        return m_keys.get(ndx);
    }

    void adjust_keys(int64_t offset)
    {
        ensure_general_form();
        m_keys.adjust(0, m_keys.size(), offset);
    }

    virtual bool update_from_parent(size_t old_baseline) noexcept = 0;
    virtual bool is_leaf() const = 0;
    virtual int get_sub_tree_depth() const = 0;
    virtual size_t node_size() const = 0;
    /// Number of elements in this subtree
    virtual size_t get_tree_size() const = 0;
    /// Last key in this subtree
    virtual int64_t get_last_key_value() const = 0;
    virtual void ensure_general_form() = 0;

    /// Initialize node from 'mem'
    virtual void init(MemRef mem) = 0;
    /// Descend the tree from the root and copy-on-write the leaf
    /// This will update all parents accordingly
    virtual MemRef ensure_writeable(ObjKey k) = 0;

    /// Insert a column at position 'ndx'
    virtual void insert_column(size_t ndx) = 0;
    /// Remove a column at position 'ndx'
    virtual void remove_column(size_t ndx) = 0;
    /// Create a new object identified by 'key' and update 'state' accordingly
    /// Return reference to new node created (if any)
    virtual ref_type insert(ObjKey k, State& state) = 0;
    /// Locate object identified by 'key' and update 'state' accordingly
    virtual void get(ObjKey key, State& state) const = 0;
    /// Locate object identified by 'ndx' and update 'state' accordingly
    virtual ObjKey get(size_t ndx, State& state) const = 0;

    /// Erase element identified by 'key'
    virtual size_t erase(ObjKey key, CascadeState& state) = 0;

    /// Move elements from position 'ndx' to 'new_node'. The new node is supposed
    /// to be a sibling positioned right after this one. All key values must
    /// be subtracted 'key_adj'
    virtual void move(size_t ndx, ClusterNode* new_leaf, int64_t key_adj) = 0;

    virtual void dump_objects(int64_t key_offset, std::string lead) const = 0;

    ObjKey get_real_key(size_t ndx) const
    {
        return ObjKey(get_key_value(ndx) + m_offset);
    }
    const ClusterKeyArray* get_key_array() const
    {
        return &m_keys;
    }
    void set_offset(uint64_t offs)
    {
        m_offset = offs;
    }
    uint64_t get_offset() const
    {
        return m_offset;
    }

protected:
    friend class ArrayBacklink;
    friend class ObjList;

    const ClusterTree& m_tree_top;
    ClusterKeyArray m_keys;
    uint64_t m_offset;
};

class Cluster : public ClusterNode {
public:
    Cluster(uint64_t offset, Allocator& allocator, const ClusterTree& tree_top)
        : ClusterNode(offset, allocator, tree_top)
    {
    }
    ~Cluster() override;

    void create(size_t nb_columns);
    void init(MemRef mem) override;
    bool update_from_parent(size_t old_baseline) noexcept override;
    bool is_writeable() const
    {
        return !Array::is_read_only();
    }
    MemRef ensure_writeable(ObjKey k) override;

    bool is_leaf() const override
    {
        return true;
    }
    int get_sub_tree_depth() const override
    {
        return 0;
    }
    size_t node_size() const override
    {
        if (!is_attached()) {
            return 0;
        }
        return m_keys.is_attached() ? m_keys.size() : get_size_in_compact_form();
    }
    size_t get_tree_size() const override
    {
        return node_size();
    }
    int64_t get_last_key_value() const override
    {
        auto sz = node_size();
        return sz ? get_key_value(sz - 1) : -1;
    }
    size_t lower_bound_key(ObjKey key) const
    {
        if (m_keys.is_attached()) {
            return m_keys.lower_bound(uint64_t(key.value));
        }
        else {
            size_t sz = size_t(Array::get(0)) >> 1;
            if (key.value < 0)
                return 0;
            if (size_t(key.value) > sz)
                return sz;
        }
        return size_t(key.value);
    }

    void ensure_general_form() override;
    void insert_column(size_t ndx) override;
    void remove_column(size_t ndx) override;
    ref_type insert(ObjKey k, State& state) override;
    void get(ObjKey k, State& state) const override;
    ObjKey get(size_t, State& state) const override;
    size_t erase(ObjKey k, CascadeState& state) override;
    void upgrade_string_to_enum(size_t col_ndx, ArrayString& keys);

    void init_leaf(size_t col_ndx, ArrayPayload* leaf) const noexcept;
    void add_leaf(size_t col_ndx, ref_type ref);

    void dump_objects(int64_t key_offset, std::string lead) const override;

private:
    static constexpr size_t s_key_ref_or_size_index = 0;
    static constexpr size_t s_first_col_index = 1;

    size_t get_size_in_compact_form() const
    {
        return size_t(Array::get(s_key_ref_or_size_index)) >> 1; // Size is stored as tagged value
    }
    friend class ClusterTree;
    void insert_row(size_t ndx, ObjKey k);
    void move(size_t ndx, ClusterNode* new_node, int64_t key_adj) override;
    template <class T>
    void do_create(size_t col_ndx);
    template <class T>
    void do_insert_column(size_t col_ndx, bool nullable);
    template <class T>
    void do_insert_row(size_t ndx, size_t col_ndx, ColumnAttrMask attr);
    template <class T>
    void do_move(size_t ndx, size_t col_ndx, Cluster* to);
    template <class T>
    void do_erase(size_t ndx, size_t col_ndx);
    void remove_backlinks(ObjKey origin_key, size_t col_ndx, const std::vector<ObjKey>& keys,
                          CascadeState& state) const;
    void do_erase_key(size_t ndx, size_t col_ndx, CascadeState& state);
};

}

#endif /* SRC_REALM_CLUSTER_HPP_ */