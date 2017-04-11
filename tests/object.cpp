////////////////////////////////////////////////////////////////////////////
//
// Copyright 2017 Realm Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
////////////////////////////////////////////////////////////////////////////

#include "catch.hpp"

#include "util/any.hpp"
#include "util/event_loop.hpp"
#include "util/index_helpers.hpp"
#include "util/test_file.hpp"

#include "collection_notifications.hpp"
#include "object_accessor.hpp"
#include "property.hpp"
#include "schema.hpp"

#include "impl/realm_coordinator.hpp"
#include "impl/object_accessor_impl.hpp"

#include <realm/group_shared.hpp>

using namespace realm;

TEST_CASE("object") {
    using namespace std::string_literals;
    _impl::RealmCoordinator::assert_no_open_realms();

    InMemoryTestFile config;
    config.automatic_change_notifications = false;
    config.cache = false;
    config.schema = Schema{
        {"table", {
            {"value 1", PropertyType::Int},
            {"value 2", PropertyType::Int},
        }},
        {"all types", {
            {"pk", PropertyType::Int, "", "", true},
            {"bool", PropertyType::Bool},
            {"int", PropertyType::Int},
            {"float", PropertyType::Float},
            {"double", PropertyType::Double},
            {"string", PropertyType::String},
            {"data", PropertyType::Data},
            {"date", PropertyType::Date},
            {"object", PropertyType::Object, "link target", "", false, false, true},
            {"array", PropertyType::Array, "array target"},
        }},
        {"link target", {
            {"value", PropertyType::Int},
        }, {
            {"origin", PropertyType::LinkingObjects, "all types", "object"},
        }},
        {"array target", {
            {"value", PropertyType::Int},
        }},
        {"pk after list", {
            {"array 1", PropertyType::Array, "array target"},
            {"int 1", PropertyType::Int},
            {"pk", PropertyType::Int, "", "", true},
            {"int 2", PropertyType::Int},
            {"array 2", PropertyType::Array, "array target"},
        }},
    };
    config.schema_version = 0;
    auto r = Realm::get_shared_realm(config);
    auto& coordinator = *_impl::RealmCoordinator::get_existing_coordinator(config.path);

    SECTION("add_notification_callback()") {
        auto table = r->read_group().get_table("class_table");
        r->begin_transaction();

        table->add_empty_row(10);
        for (int i = 0; i < 10; ++i)
            table->set_int(0, i, i);
        r->commit_transaction();

        auto r2 = coordinator.get_realm();

        CollectionChangeSet change;
        Row row = table->get(0);
        Object object(r, *r->schema().find("table"), row);

        auto write = [&](auto&& f) {
            r->begin_transaction();
            f();
            r->commit_transaction();

            advance_and_notify(*r);
        };

        auto require_change = [&] {
            auto token = object.add_notification_callback([&](CollectionChangeSet c, std::exception_ptr) {
                change = c;
            });
            advance_and_notify(*r);
            return token;
        };

        auto require_no_change = [&] {
            bool first = true;
            auto token = object.add_notification_callback([&](CollectionChangeSet, std::exception_ptr) {
                REQUIRE(first);
                first = false;
            });
            advance_and_notify(*r);
            return token;
        };

        SECTION("deleting the object sends a change notification") {
            auto token = require_change();
            write([&] { row.move_last_over(); });
            REQUIRE_INDICES(change.deletions, 0);
        }

        SECTION("modifying the object sends a change notification") {
            auto token = require_change();

            write([&] { row.set_int(0, 10); });
            REQUIRE_INDICES(change.modifications, 0);
            REQUIRE(change.columns.size() == 1);
            REQUIRE_INDICES(change.columns[0], 0);

            write([&] { row.set_int(1, 10); });
            REQUIRE_INDICES(change.modifications, 0);
            REQUIRE(change.columns.size() == 2);
            REQUIRE(change.columns[0].empty());
            REQUIRE_INDICES(change.columns[1], 0);
        }

        SECTION("modifying a different object") {
            auto token = require_no_change();
            write([&] { table->get(1).set_int(0, 10); });
        }

        SECTION("moving the object") {
            auto token = require_no_change();
            write([&] { table->swap_rows(0, 5); });
        }

        SECTION("subsuming the object") {
            auto token = require_change();
            write([&] {
                table->insert_empty_row(0);
                table->merge_rows(row.get_index(), 0);
                row.set_int(0, 10);
            });
            REQUIRE(change.columns.size() == 1);
            REQUIRE_INDICES(change.columns[0], 0);
        }

        SECTION("multiple write transactions") {
            auto token = require_change();

            auto r2row = r2->read_group().get_table("class_table")->get(0);
            r2->begin_transaction();
            r2row.set_int(0, 1);
            r2->commit_transaction();
            r2->begin_transaction();
            r2row.set_int(1, 2);
            r2->commit_transaction();

            advance_and_notify(*r);
            REQUIRE(change.columns.size() == 2);
            REQUIRE_INDICES(change.columns[0], 0);
            REQUIRE_INDICES(change.columns[1], 0);
        }

        SECTION("skipping a notification") {
            auto token = require_no_change();
            write([&] {
                row.set_int(0, 1);
                token.suppress_next();
            });
        }

        SECTION("skipping only effects the current transaction even if no notification would occur anyway") {
            auto token = require_change();

            // would not produce a notification even if it wasn't skipped because no changes were made
            write([&] {
                token.suppress_next();
            });
            REQUIRE(change.empty());

            // should now produce a notification
            write([&] {
                row.set_int(0, 1);
            });
            REQUIRE_INDICES(change.modifications, 0);
        }
    }

    CppContext context;
    auto create = [&](util::Any&& value, bool update) {
        r->begin_transaction();
        auto obj = Object::create(&context, r, *r->schema().find("all types"), value, update);
        r->commit_transaction();
        return obj;
    };

    SECTION("create object") {
        auto obj = create(AnyDict{
            {"pk", 1LL},
            {"bool", true},
            {"int", 5LL},
            {"float", 2.2f},
            {"double", 3.3},
            {"string", "hello"s},
            {"data", "olleh"s},
            {"date", Timestamp(10, 20)},
            {"object", AnyDict{{"value", 10LL}}},
            {"array", AnyVector{AnyDict{{"value", 20LL}}}},
        }, false);

        auto row = obj.row();
        REQUIRE(row.get_int(0) == 1);
        REQUIRE(row.get_bool(1) == true);
        REQUIRE(row.get_int(2) == 5);
        REQUIRE(row.get_float(3) == 2.2f);
        REQUIRE(row.get_double(4) == 3.3);
        REQUIRE(row.get_string(5) == "hello");
        REQUIRE(row.get_binary(6) == BinaryData("olleh", 5));
        REQUIRE(row.get_timestamp(7) == Timestamp(10, 20));
        REQUIRE(row.get_link(8) == 0);

        auto link_target = r->read_group().get_table("class_link target")->get(0);
        REQUIRE(link_target.get_int(0) == 10);

        auto list = row.get_linklist(9);
        REQUIRE(list->size() == 1);
        REQUIRE(list->get(0).get_int(0) == 20);
    }

    SECTION("create uses defaults for missing values") {
        context.defaults["all types"] = {
            {"bool", true},
            {"int", 5LL},
            {"float", 2.2f},
            {"double", 3.3},
            {"string", "hello"s},
            {"data", "olleh"s},
            {"date", Timestamp(10, 20)},
            {"object", AnyDict{{"value", 10LL}}},
            {"array", AnyVector{AnyDict{{"value", 20LL}}}},
        };

        auto obj = create(AnyDict{
            {"pk", 1LL},
            {"float", 6.6f},
        }, false);

        auto row = obj.row();
        REQUIRE(row.get_int(0) == 1);
        REQUIRE(row.get_bool(1) == true);
        REQUIRE(row.get_int(2) == 5);
        REQUIRE(row.get_float(3) == 6.6f);
        REQUIRE(row.get_double(4) == 3.3);
        REQUIRE(row.get_string(5) == "hello");
        REQUIRE(row.get_binary(6) == BinaryData("olleh", 5));
        REQUIRE(row.get_timestamp(7) == Timestamp(10, 20));
    }

    SECTION("create throws for missing values if there is no default") {
        REQUIRE_THROWS(create(AnyDict{
            {"pk", 1LL},
            {"float", 6.6f},
        }, false));
    }

    SECTION("create always sets the PK first") {
        AnyDict value{
            {"array 1", AnyVector{AnyDict{{"value", 1LL}}}},
            {"array 2", AnyVector{AnyDict{{"value", 2LL}}}},
            {"int 1", 0LL},
            {"int 2", 0LL},
            {"pk", 7LL},
        };
        // Core will throw if the list is populated before the PK is set
        r->begin_transaction();
        REQUIRE_NOTHROW(Object::create(&context, r, *r->schema().find("pk after list"), util::Any(value), false));
    }

    SECTION("create with update") {
        auto obj = create(AnyDict{
            {"pk", 1LL},
            {"bool", true},
            {"int", 5LL},
            {"float", 2.2f},
            {"double", 3.3},
            {"string", "hello"s},
            {"data", "olleh"s},
            {"date", Timestamp(10, 20)},
            {"object", AnyDict{{"value", 10LL}}},
            {"array", AnyVector{AnyDict{{"value", 20LL}}}},
        }, false);
        create(AnyDict{
            {"pk", 1LL},
            {"int", 6LL},
            {"string", "a"s},
        }, true);

        auto row = obj.row();
        REQUIRE(row.get_int(0) == 1);
        REQUIRE(row.get_bool(1) == true);
        REQUIRE(row.get_int(2) == 6);
        REQUIRE(row.get_float(3) == 2.2f);
        REQUIRE(row.get_double(4) == 3.3);
        REQUIRE(row.get_string(5) == "a");
        REQUIRE(row.get_binary(6) == BinaryData("olleh", 5));
        REQUIRE(row.get_timestamp(7) == Timestamp(10, 20));
    }

    SECTION("getters and setters") {
        r->begin_transaction();

        auto& table = *r->read_group().get_table("class_all types");
        table.add_empty_row();
        Object obj(r, *r->schema().find("all types"), table[0]);

        auto& link_table = *r->read_group().get_table("class_link target");
        link_table.add_empty_row();
        Object linkobj(r, *r->schema().find("link target"), link_table[0]);

        obj.set_property_value(&context, "bool", util::Any(true), false);
        REQUIRE(any_cast<bool>(obj.get_property_value<util::Any>(&context, "bool")) == true);

        obj.set_property_value(&context, "int", util::Any(5LL), false);
        REQUIRE(any_cast<long long>(obj.get_property_value<util::Any>(&context, "int")) == 5);

        obj.set_property_value(&context, "float", util::Any(1.23f), false);
        REQUIRE(any_cast<float>(obj.get_property_value<util::Any>(&context, "float")) == 1.23f);

        obj.set_property_value(&context, "double", util::Any(1.23), false);
        REQUIRE(any_cast<double>(obj.get_property_value<util::Any>(&context, "double")) == 1.23);

        obj.set_property_value(&context, "string", util::Any("abc"s), false);
        REQUIRE(any_cast<std::string>(obj.get_property_value<util::Any>(&context, "string")) == "abc");

        obj.set_property_value(&context, "data", util::Any("abc"s), false);
        REQUIRE(any_cast<std::string>(obj.get_property_value<util::Any>(&context, "data")) == "abc");

        obj.set_property_value(&context, "date", util::Any(Timestamp(1, 2)), false);
        REQUIRE(any_cast<Timestamp>(obj.get_property_value<util::Any>(&context, "date")) == Timestamp(1, 2));

        REQUIRE_FALSE(obj.get_property_value<util::Any>(&context, "object").has_value());
        obj.set_property_value(&context, "object", util::Any(linkobj), false);
        REQUIRE(any_cast<Object>(obj.get_property_value<util::Any>(&context, "object")).row().get_index() == linkobj.row().get_index());

        auto linking = any_cast<Results>(linkobj.get_property_value<util::Any>(&context, "origin"));
        REQUIRE(linking.size() == 1);

        REQUIRE_THROWS(obj.set_property_value(&context, "pk", util::Any(5LL), false));
        REQUIRE_THROWS(obj.set_property_value(&context, "not a property", util::Any(5LL), false));

        r->commit_transaction();

        REQUIRE_THROWS(obj.get_property_value<util::Any>(&context, "not a property"));
        REQUIRE_THROWS(obj.set_property_value(&context, "int", util::Any(5LL), false));
    }

#if REALM_ENABLE_SYNC
    if (!util::EventLoop::has_implementation())
        return;

    SyncServer server(false);
    SyncTestFile config1(server, "shared");
    config1.schema = config.schema;
    SyncTestFile config2(server, "shared");
    config2.schema = config.schema;

    SECTION("defaults do not override values explicitly passed to create()") {
        context.defaults["pk after list"] = {
            {"int 1", 10LL},
            {"int 2", 10LL},
        };
        AnyDict v1{
            {"pk", 7LL},
            {"array 1", AnyVector{AnyDict{{"value", 1LL}}}},
            {"array 2", AnyVector{AnyDict{{"value", 2LL}}}},
        };
        auto v2 = v1;
        v1["int 1"] = 1LL;
        v2["int 2"] = 2LL;

        auto r1 = Realm::get_shared_realm(config1);
        auto r2 = Realm::get_shared_realm(config2);

        r1->begin_transaction();
        r2->begin_transaction();
        auto obj = Object::create(&context, r1, *r1->schema().find("pk after list"), util::Any(v1), false);
        Object::create(&context, r2, *r2->schema().find("pk after list"), util::Any(v2), false);
        r2->commit_transaction();
        r1->commit_transaction();

        server.start();
        util::EventLoop::main().run_until([&] {
            return r1->read_group().get_table("class_array target")->size() == 4;
        });

        REQUIRE(obj.row().get_linklist(0)->size() == 2);
        REQUIRE(obj.row().get_int(1) == 1); // non-default from r1
        REQUIRE(obj.row().get_int(2) == 7); // pk
        REQUIRE(obj.row().get_int(3) == 2); // non-default from r2
        REQUIRE(obj.row().get_linklist(4)->size() == 2);
    }
#endif
}
