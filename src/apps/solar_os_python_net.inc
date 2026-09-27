/* Shared solaros.net binding implementation. Runtime helpers are supplied by the embedding host. */
static mp_obj_t solaros_net_router_start(void)
{
    python_check_esp(solar_os_network_router_start());
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_0(solaros_net_router_start_obj, solaros_net_router_start);

static mp_obj_t solaros_net_router_stop(void)
{
    python_check_esp(solar_os_network_router_stop());
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_0(solaros_net_router_stop_obj, solaros_net_router_stop);

static mp_obj_t solaros_net_ping(size_t n_args, const mp_obj_t *args)
{
    const char *host = mp_obj_str_get_str(args[0]);
    solar_os_net_ping_options_t options = {
        .count = python_optional_u32(n_args, args, 1, 4),
        .timeout_ms = python_optional_u32(n_args, args, 2, 1000),
        .interval_ms = python_optional_u32(n_args, args, 3, 1000),
        .data_size = python_optional_u32(n_args, args, 4, 32),
    };
    solar_os_net_ping_result_t result;
    python_check_esp(solar_os_net_ping(host,
                                       &options,
                                       NULL,
                                       NULL,
                                       python_should_cancel,
                                       NULL,
                                       &result));

    mp_obj_t dict = mp_obj_new_dict(9);
    python_dict_store_cstr(dict, "resolved_ip", result.resolved_ip);
    python_dict_store_bool(dict, "interrupted", result.interrupted);
    python_dict_store_uint(dict, "transmitted", result.transmitted);
    python_dict_store_uint(dict, "received", result.received);
    python_dict_store_uint(dict, "loss_percent", result.loss_percent);
    python_dict_store_uint(dict, "total_time_ms", result.total_time_ms);
    python_dict_store_uint(dict, "min_time_ms", result.min_time_ms);
    python_dict_store_uint(dict, "avg_time_ms", result.avg_time_ms);
    python_dict_store_uint(dict, "max_time_ms", result.max_time_ms);
    return dict;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(solaros_net_ping_obj, 1, 5, solaros_net_ping);

static mp_obj_t solaros_net_tcp_connect(size_t n_args, const mp_obj_t *args)
{
    uint32_t handle = 0;
    python_check_esp(solar_os_net_session_tcp_connect(
        python_net_get(),
        mp_obj_str_get_str(args[0]),
        python_net_port(args[1]),
        python_net_timeout(n_args,
                           args,
                           2,
                           SOLAR_OS_NET_DEFAULT_CONNECT_TIMEOUT_MS),
        &handle));
    return mp_obj_new_int_from_uint(handle);
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(solaros_net_tcp_connect_obj,
                                    2,
                                    3,
                                    solaros_net_tcp_connect);

static mp_obj_t solaros_net_tcp_send(size_t n_args, const mp_obj_t *args)
{
    mp_buffer_info_t data;
    mp_get_buffer_raise(args[1], &data, MP_BUFFER_READ);
    python_check_esp(solar_os_net_session_tcp_send(
        python_net_get(),
        python_u32_from_obj(args[0]),
        data.buf,
        data.len,
        python_net_timeout(n_args, args, 2, SOLAR_OS_NET_DEFAULT_IO_TIMEOUT_MS)));
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(solaros_net_tcp_send_obj,
                                    2,
                                    3,
                                    solaros_net_tcp_send);

static mp_obj_t solaros_net_tcp_receive(size_t n_args, const mp_obj_t *args)
{
    const size_t max_bytes = python_net_receive_size(n_args, args, 1);
    const uint32_t handle = python_u32_from_obj(args[0]);
    const uint32_t timeout_ms = python_net_timeout(
        n_args, args, 2, SOLAR_OS_NET_DEFAULT_IO_TIMEOUT_MS);
    vstr_t data;
    vstr_init_len(&data, max_bytes);
    solar_os_net_receive_result_t result;
    const esp_err_t err = solar_os_net_session_tcp_receive(
        python_net_get(),
        handle,
        data.buf,
        max_bytes,
        timeout_ms,
        &result);
    if (err != ESP_OK || result.timed_out) {
        vstr_clear(&data);
        if (err != ESP_OK) {
            python_raise_esp(err);
        }
        return mp_const_none;
    }
    data.len = result.data_len;
    return mp_obj_new_bytes_from_vstr(&data);
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(solaros_net_tcp_receive_obj,
                                    1,
                                    3,
                                    solaros_net_tcp_receive);

static mp_obj_t solaros_net_udp_open(size_t n_args, const mp_obj_t *args)
{
    uint16_t local_port = 0;
    if (n_args > 0 && args[0] != mp_const_none) {
        const uint32_t value = python_u32_from_obj(args[0]);
        if (value > UINT16_MAX) {
            mp_raise_ValueError(MP_ERROR_TEXT("local port out of range"));
        }
        local_port = (uint16_t)value;
    }
    uint32_t handle = 0;
    python_check_esp(solar_os_net_session_udp_open(python_net_get(),
                                                  local_port,
                                                  &handle));
    return mp_obj_new_int_from_uint(handle);
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(solaros_net_udp_open_obj,
                                    0,
                                    1,
                                    solaros_net_udp_open);

static mp_obj_t solaros_net_udp_send(size_t n_args, const mp_obj_t *args)
{
    mp_buffer_info_t data;
    mp_get_buffer_raise(args[3], &data, MP_BUFFER_READ);
    python_check_esp(solar_os_net_session_udp_send(
        python_net_get(),
        python_u32_from_obj(args[0]),
        mp_obj_str_get_str(args[1]),
        python_net_port(args[2]),
        data.buf,
        data.len,
        python_net_timeout(n_args, args, 4, SOLAR_OS_NET_DEFAULT_IO_TIMEOUT_MS)));
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(solaros_net_udp_send_obj,
                                    4,
                                    5,
                                    solaros_net_udp_send);

static mp_obj_t solaros_net_udp_receive(size_t n_args, const mp_obj_t *args)
{
    const size_t max_bytes = python_net_receive_size(n_args, args, 1);
    const uint32_t handle = python_u32_from_obj(args[0]);
    const uint32_t timeout_ms = python_net_timeout(
        n_args, args, 2, SOLAR_OS_NET_DEFAULT_IO_TIMEOUT_MS);
    vstr_t data;
    vstr_init_len(&data, max_bytes);
    solar_os_net_receive_result_t result;
    const esp_err_t err = solar_os_net_session_udp_receive(
        python_net_get(),
        handle,
        data.buf,
        max_bytes,
        timeout_ms,
        &result);
    if (err != ESP_OK || result.timed_out) {
        vstr_clear(&data);
        if (err != ESP_OK) {
            python_raise_esp(err);
        }
        return mp_const_none;
    }
    data.len = result.data_len;
    mp_obj_t response = mp_obj_new_dict(5);
    mp_obj_dict_store(response, python_key("data"), mp_obj_new_bytes_from_vstr(&data));
    python_dict_store_cstr(response, "address", result.address);
    python_dict_store_uint(response, "port", result.port);
    python_dict_store_bool(response, "truncated", result.truncated);
    python_dict_store_uint(response, "datagram_bytes", result.message_len);
    return response;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(solaros_net_udp_receive_obj,
                                    1,
                                    3,
                                    solaros_net_udp_receive);

static mp_obj_t solaros_net_websocket_connect(size_t n_args, const mp_obj_t *args)
{
    const char *subprotocol = n_args > 1 && args[1] != mp_const_none ?
        mp_obj_str_get_str(args[1]) : NULL;
    uint32_t handle = 0;
    python_check_esp(solar_os_net_session_websocket_connect(
        python_net_get(),
        mp_obj_str_get_str(args[0]),
        subprotocol,
        python_net_timeout(n_args,
                           args,
                           2,
                           SOLAR_OS_NET_DEFAULT_CONNECT_TIMEOUT_MS),
        &handle));
    return mp_obj_new_int_from_uint(handle);
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(solaros_net_websocket_connect_obj,
                                    1,
                                    3,
                                    solaros_net_websocket_connect);

static mp_obj_t solaros_net_websocket_send(size_t n_args, const mp_obj_t *args)
{
    mp_buffer_info_t data;
    mp_get_buffer_raise(args[1], &data, MP_BUFFER_READ);
    python_check_esp(solar_os_net_session_websocket_send(
        python_net_get(),
        python_u32_from_obj(args[0]),
        data.buf,
        data.len,
        n_args > 2 && mp_obj_is_true(args[2]),
        python_net_timeout(n_args, args, 3, SOLAR_OS_NET_DEFAULT_IO_TIMEOUT_MS)));
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(solaros_net_websocket_send_obj,
                                    2,
                                    4,
                                    solaros_net_websocket_send);

static mp_obj_t solaros_net_websocket_receive(size_t n_args, const mp_obj_t *args)
{
    const size_t max_bytes = python_net_receive_size(n_args, args, 1);
    const uint32_t handle = python_u32_from_obj(args[0]);
    const uint32_t timeout_ms = python_net_timeout(
        n_args, args, 2, SOLAR_OS_NET_DEFAULT_IO_TIMEOUT_MS);
    vstr_t data;
    vstr_init_len(&data, max_bytes);
    solar_os_net_receive_result_t result;
    const esp_err_t err = solar_os_net_session_websocket_receive(
        python_net_get(),
        handle,
        data.buf,
        max_bytes,
        timeout_ms,
        &result);
    if (err != ESP_OK || result.timed_out) {
        vstr_clear(&data);
        if (err != ESP_OK) {
            python_raise_esp(err);
        }
        return mp_const_none;
    }
    data.len = result.data_len;
    mp_obj_t response = mp_obj_new_dict(6);
    mp_obj_dict_store(response, python_key("data"), mp_obj_new_bytes_from_vstr(&data));
    python_dict_store_cstr(response, "type", solar_os_net_ws_opcode_name(result.opcode));
    python_dict_store_bool(response, "final", result.final);
    python_dict_store_bool(response, "closed", result.closed);
    python_dict_store_bool(response, "truncated", result.truncated);
    python_dict_store_uint(response, "frame_bytes", result.message_len);
    return response;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(solaros_net_websocket_receive_obj,
                                    1,
                                    3,
                                    solaros_net_websocket_receive);

static mp_obj_t solaros_net_close(mp_obj_t handle)
{
    python_check_esp(solar_os_net_session_close(python_net_get(),
                                               python_u32_from_obj(handle)));
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_1(solaros_net_close_obj, solaros_net_close);

static mp_obj_t solaros_net_close_all(void)
{
    if (python_net_session != NULL) {
        solar_os_net_session_close_all(python_net_session);
    }
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_0(solaros_net_close_all_obj, solaros_net_close_all);

static mp_obj_t solaros_net_limits(void)
{
    solar_os_net_session_status_t status;
    solar_os_net_session_get_status(python_net_get(), &status);
    mp_obj_t response = mp_obj_new_dict(10);
    python_dict_store_cstr(response, "owner", status.owner);
    python_dict_store_uint(response, "open_channels", status.open_channels);
    python_dict_store_uint(response, "session_channels", status.session_limit);
    python_dict_store_uint(response, "global_open_channels", status.global_open_channels);
    python_dict_store_uint(response, "global_channels", status.global_limit);
    python_dict_store_uint(response, "max_transfer_bytes", SOLAR_OS_NET_MAX_TRANSFER_BYTES);
    python_dict_store_uint(response, "max_udp_bytes", SOLAR_OS_NET_MAX_UDP_BYTES);
    python_dict_store_uint(response, "max_timeout_ms", SOLAR_OS_NET_MAX_TIMEOUT_MS);
    python_dict_store_uint(response, "poll_slice_ms", SOLAR_OS_NET_POLL_SLICE_MS);
    python_dict_store_bool(response, "synchronous", true);
    return response;
}
MP_DEFINE_CONST_FUN_OBJ_0(solaros_net_limits_obj, solaros_net_limits);

