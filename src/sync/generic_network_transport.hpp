////////////////////////////////////////////////////////////////////////////
//
// Copyright 2020 Realm Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or utilied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
////////////////////////////////////////////////////////////////////////////


#ifndef REALM_GENERIC_NETWORK_TRANSPORT_HPP
#define REALM_GENERIC_NETWORK_TRANSPORT_HPP

#include <functional>
#include <string>
#include <memory>
#include <map>
#include <vector>

#include <realm/util/to_string.hpp>

namespace realm {
namespace app {

#pragma mark Errors

struct AppError : public std::runtime_error {
    enum class Type {
        Unknown,
        JSON,
        Service,
        Custom
    };

    std::string category() const
    {
        switch (type) {
            case Type::JSON: return "realm::json";
            case Type::Service: return "realm::service";
            case Type::Custom: return "realm::custom";
            default: return "realm::unknown";
        }
    }

    int code() const
    {
        return m_code;
    }

    AppError(std::string msg, int code, Type classification = Type::Unknown)
    : std::runtime_error(msg),
    type(classification),
    m_code(code)
    {
    }

    const Type type;
    
private:
    const int m_code;
};

enum class JSONErrorCode {
    bad_token = 1,
    malformed_json = 2,
    missing_json_key = 3,

    none = 0
};

struct JSONError : public AppError {
    JSONError(JSONErrorCode c, std::string msg)
    : AppError(msg, static_cast<int>(c), AppError::Type::JSON)
    , code(c)
    {
    }

    const JSONErrorCode code;
};

#define HAS_JSON_KEY_OR_THROW(JSON, KEY, RET_TYPE) \
JSON.find(KEY) != JSON.end() ? JSON[KEY].get<RET_TYPE>() : \
throw app::JSONError(app::JSONErrorCode::missing_json_key, KEY)

enum class ServiceErrorCode {
    missing_auth_req = 1,
    /// Invalid session, expired, no associated user, or app domain mismatch
    invalid_session = 2,
    user_app_domain_mismatch = 3,
    domain_not_allowed = 4,
    read_size_limit_exceeded = 5,
    invalid_parameter = 6,
    missing_parameter = 7,
    twilio_error = 8,
    gcm_error = 9,
    http_error = 10,
    aws_error = 11,
    mongodb_error = 12,
    arguments_not_allowed = 13,
    function_execution_error = 14,
    no_matching_rule_found = 15,
    internal_server_error = 16,
    auth_provider_not_found = 17,
    auth_provider_already_exists = 18,
    service_not_found = 19,
    service_type_not_found = 20,
    service_already_exists = 21,
    service_command_not_found = 22,
    value_not_found = 23,
    value_already_exists = 24,
    value_duplicate_name = 25,
    function_not_found = 26,
    function_already_exists = 27,
    function_duplicate_name = 28,
    function_syntax_error = 29,
    function_invalid = 30,
    incoming_webhook_not_found = 31,
    incoming_webhook_already_exists = 32,
    incoming_webhook_duplicate_name = 33,
    rule_not_found = 34,
    api_key_not_found = 35,
    rule_already_exists = 36,
    rule_duplicate_name = 37,
    auth_provider_duplicate_name = 38,
    restricted_host = 39,
    api_key_already_exists = 40,
    incoming_webhook_auth_failed = 41,
    execution_time_limit_exceeded = 42,
    not_callable = 43,
    user_already_confirmed = 44,
    user_not_found = 45,
    user_disabled = 46,

    unknown = -1,
    none = 0
};

/// Struct allowing for generic error data.
struct ServiceError : public AppError {
    ServiceError(std::string raw_code, std::string message)
    : AppError(message, static_cast<int>(error_code_for_string(raw_code)), AppError::Type::Service)
    {
    }

private:
    static ServiceErrorCode error_code_for_string(const std::string& code);
    const std::string m_raw_code;
};

/**
 * An HTTP method type.
 */
enum class HttpMethod {
    get, post, patch, put, del
};

/**
 * An HTTP request that can be made to an arbitrary server.
 */
struct Request {
    /**
     * The HTTP method of this request.
     */
    HttpMethod method;

    /**
     * The URL to which this request will be made.
     */
    std::string url;

    /**
     * The number of milliseconds that the underlying transport should spend on an HTTP round trip before failing with an
     * error.
     */
    uint64_t timeout_ms;

    /**
     * The HTTP headers of this request.
     */
    std::map<std::string, std::string> headers;

    /**
     * The body of the request.
     */
    std::string body;
};

/**
 * The contents of an HTTP response.
 */
struct Response {
    /**
     * The status code of the HTTP response.
     */
    int http_status_code;

    /**
     * A custom status code provided by the language binding.
     */
    int custom_status_code;

    /**
     * The headers of the HTTP response.
     */
    std::map<std::string, std::string> headers;

    /**
     * The body of the HTTP response.
     */
    std::string body;
};

#pragma mark GenericNetworkTransport

/// Generic network transport for foreign interfaces.
struct GenericNetworkTransport {
    using NetworkTransportFactory = std::function<std::unique_ptr<GenericNetworkTransport>()>;
    virtual void send_request_to_server(const Request request,
                                        std::function<void(const Response)> completionBlock) = 0;
    virtual ~GenericNetworkTransport() = default;
};

} // namespace app
} // namespace realm

#endif /* REALM_GENERIC_NETWORK_TRANSPORT_HPP */
