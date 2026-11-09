#pragma once

#include <hidi/asio/ssl_context.hpp>
#include <hidi/h2client.hpp>
#include <hidi/h2server.hpp>
#include <svarg/backend/cxx/support/operations.hpp>
#include <svarg/errors.hpp>

namespace btye::h2 {

using hidi::http_body;
using hidi::http_body_bytes;
using hidi::http_header_t;
using hidi::http_headers_t;
using hidi::http_method_e;
using hidi::http_request;
using hidi::http_response;
using hidi::stream_body_maker_t;
using hidi::streaming_body_t;

using hidi::client_request_context;
using hidi::request_context;

// время
using hidi::deadline_after;
using hidi::deadline_t;
using hidi::duration_t;

// адреса
using hidi::endpoint;
using hidi::internet_address;
using hidi::server_endpoint;

using hidi::h2client;
using hidi::h2client_options;
using hidi::h2server;
using hidi::h2server_options;

// TLS
using hidi::client_ssl_context_ptr;
using hidi::make_ssl_context_for_client;
using hidi::make_ssl_context_for_server;
using hidi::server_ssl_context_ptr;

// для нетипизированного middleware (raw_client)
using hidi::on_data_part_fn_ptr;
using hidi::on_header_fn_ptr;
using svarg::cxx_support::client_ref;
// статус < 0 у ответа клиента - ошибка транспорта (CANCELLED, TIMEOUT, NETWORK_ERR...)
using hidi::reqerr_e;
using svarg::cxx_support::throw_transport_error;

// ответ целиком, ошибка транспорта - исключение
using svarg::cxx_support::send_buffered;

// операции сгенерированного API
using svarg::content_type_params;
using svarg::cxx_support::from_request;
using svarg::cxx_support::make_request;
using svarg::cxx_support::make_response;
using svarg::cxx_support::make_response_headers;
using svarg::cxx_support::ready_response;

// ошибки разбора и проверки значений по схеме
using svarg::error_code;
using svarg::parse_error;
using svarg::serialize_error;
using svarg::validate_error;

// для типизированной middleware надо специализацироть svarg::cxx_support::operation_hooks<Op>

}  // namespace btye::h2
