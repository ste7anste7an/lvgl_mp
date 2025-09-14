/*
 * RMT NG migration for ESP-IDF 5.x
 * – Replaces legacy driver/rmt.h usage with driver/rmt_tx.h + driver/rmt_encoder.h
 */

#include "py/mphal.h"
#include "py/runtime.h"
#include "modmachine.h"
#include "modesp32.h"

#include "esp_task.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_common.h"

#include "soc/clk_tree_defs.h"   // For APB_CLK_FREQ on some IDF targets

// Forward declaration
extern const mp_obj_type_t esp32_rmt_type;

// NOTE: In NG driver we don't select channels by numeric ID.
// We keep the field to maintain API shape but it’s not used to allocate a specific channel.
typedef struct _esp32_rmt_obj_t {
    mp_obj_base_t base;

    // Legacy-visible fields (kept for printing / compatibility)
    int8_t channel_id;     // not used to allocate in NG; keep for user-visible print
    gpio_num_t pin;
    uint8_t clock_div;

    // NG driver handles
    rmt_channel_handle_t tx_channel;
    rmt_encoder_handle_t copy_encoder;

    // Storage for symbols
    mp_uint_t num_symbols;
    rmt_symbol_word_t *symbols;

    bool loop_en;          // legacy loop flag (not supported here)
    uint32_t resolution_hz;
} esp32_rmt_obj_t;

// Retain the global to keep external references compiling; in NG this has no effect on allocation.
int8_t esp32_rmt_bitstream_channel_id = -1;

/* ------------------------ ctor / dtor ------------------------ */

static mp_obj_t esp32_rmt_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *all_args) {
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_id,        MP_ARG_REQUIRED | MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_pin,       MP_ARG_REQUIRED | MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_clock_div,                   MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 8} },     // ~100ns if APB=80MHz
        { MP_QSTR_idle_level,                  MP_ARG_KW_ONLY | MP_ARG_BOOL, {.u_bool = false} },
        { MP_QSTR_tx_carrier,                  MP_ARG_KW_ONLY | MP_ARG_OBJ, {.u_obj = mp_const_none} },
    };

    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, all_args,
        MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    int channel_id_arg = args[0].u_int;                  // kept for compatibility/printing only
    gpio_num_t pin_id = machine_pin_get_id(args[1].u_obj);
    uint8_t clock_div = args[2].u_int;
    bool idle_level = args[3].u_bool;
    mp_obj_t tx_carrier_obj = args[4].u_obj;

    if (clock_div < 1 || clock_div > 255) {
        mp_raise_ValueError(MP_ERROR_TEXT("clock_div must be between 1 and 255"));
    }

    esp32_rmt_obj_t *self = mp_obj_malloc_with_finaliser(esp32_rmt_obj_t, &esp32_rmt_type);
    self->channel_id = channel_id_arg;
    self->pin = pin_id;
    self->clock_div = clock_div;
    self->tx_channel = NULL;
    self->copy_encoder = NULL;
    self->symbols = NULL;
    self->num_symbols = 0;
    self->loop_en = false;

    // Resolution in Hz: legacy used APB / clock_div
    // APB clock is typically 80MHz on ESP32/ESP32-S3.
    uint32_t apb = APB_CLK_FREQ; // comes from IDF
    self->resolution_hz = apb / clock_div;

    /* --- Create TX channel --- */
    rmt_tx_channel_config_t tx_cfg = {
        .gpio_num = self->pin,
        .clk_src = RMT_CLK_SRC_DEFAULT,         // auto-select
        .resolution_hz = self->resolution_hz,   // ticks per second
        .mem_block_symbols = 64,                // size of a channel memory block, in symbols
        .trans_queue_depth = 4,                 // transactions that can be queued
        .flags.with_dma = 0,
    };

    check_esp_err(rmt_new_tx_channel(&tx_cfg, &self->tx_channel));

    // Idle level and carrier:
    // In NG, idle level is set via tx carrier / EOT config at transmit time or via channel level configs.
    // We’ll set idle level using the default EOT level through tx_config flags (per transmit).
    // Carrier: NG supports carrier in a separate encoder (not implemented here). We’ll reject carrier for now.
    if (tx_carrier_obj != mp_const_none) {
        mp_raise_NotImplementedError(MP_ERROR_TEXT("tx_carrier not implemented on RMT NG in this wrapper"));
    }

    check_esp_err(rmt_enable(self->tx_channel));

    // Copy encoder sends raw rmt_symbol_word_t arrays as-is.
    rmt_copy_encoder_config_t enc_cfg = {};
    check_esp_err(rmt_new_copy_encoder(&enc_cfg, &self->copy_encoder));

    return MP_OBJ_FROM_PTR(self);
}

static void esp32_rmt_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    esp32_rmt_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->tx_channel) {
        mp_printf(print, "RMT(channel=%d, pin=%u, source_freq=%u, clock_div=%u, resolution_hz=%u)",
            self->channel_id, self->pin, APB_CLK_FREQ, self->clock_div, self->resolution_hz);
    } else {
        mp_printf(print, "RMT()");
    }
}

static mp_obj_t esp32_rmt_deinit(mp_obj_t self_in) {
    esp32_rmt_obj_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->copy_encoder) {
        rmt_del_encoder(self->copy_encoder);
        self->copy_encoder = NULL;
    }
    if (self->tx_channel) {
        rmt_disable(self->tx_channel);
        rmt_del_channel(self->tx_channel);
        self->tx_channel = NULL;
    }
    if (self->symbols) {
        m_free(self->symbols);
        self->symbols = NULL;
        self->num_symbols = 0;
    }
    self->pin = -1;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(esp32_rmt_deinit_obj, esp32_rmt_deinit);

/* ------------------------ helpers / props ------------------------ */

// Return the (legacy) source frequency, i.e., APB clock
static mp_obj_t esp32_rmt_source_freq(void) {
    return mp_obj_new_int(APB_CLK_FREQ);
}
static MP_DEFINE_CONST_FUN_OBJ_0(esp32_rmt_source_freq_obj, esp32_rmt_source_freq);
static MP_DEFINE_CONST_STATICMETHOD_OBJ(esp32_rmt_source_obj, MP_ROM_PTR(&esp32_rmt_source_freq_obj));

static mp_obj_t esp32_rmt_clock_div(mp_obj_t self_in) {
    esp32_rmt_obj_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_int(self->clock_div);
}
static MP_DEFINE_CONST_FUN_OBJ_1(esp32_rmt_clock_div_obj, esp32_rmt_clock_div);

/* ------------------------ wait / loop ------------------------ */

static mp_obj_t esp32_rmt_wait_done(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_self,    MP_ARG_REQUIRED | MP_ARG_OBJ, {.u_obj = mp_const_none} },
        { MP_QSTR_timeout, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 0} },   // ms
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);

    esp32_rmt_obj_t *self = MP_OBJ_TO_PTR(args[0].u_obj);
    uint32_t timeout_ms = args[1].u_int;

    esp_err_t err = rmt_tx_wait_all_done(self->tx_channel,
                                         timeout_ms ? pdMS_TO_TICKS(timeout_ms) : portMAX_DELAY);
    return (err == ESP_OK) ? mp_const_true : mp_const_false;
}
static MP_DEFINE_CONST_FUN_OBJ_KW(esp32_rmt_wait_done_obj, 1, esp32_rmt_wait_done);

static mp_obj_t esp32_rmt_loop(mp_obj_t self_in, mp_obj_t loop) {
    esp32_rmt_obj_t *self = MP_OBJ_TO_PTR(self_in);
    bool want_loop = mp_obj_get_int(loop);
    if (want_loop) {
        // NG: infinite loop not provided here; could be emulated via loop_count or feeding queue.
        mp_raise_NotImplementedError(MP_ERROR_TEXT("loop mode not implemented on RMT NG in this wrapper"));
    }
    self->loop_en = false;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(esp32_rmt_loop_obj, esp32_rmt_loop);

/* ------------------------ write_pulses ------------------------ */

static mp_obj_t esp32_rmt_write_pulses(size_t n_args, const mp_obj_t *args) {
    esp32_rmt_obj_t *self = MP_OBJ_TO_PTR(args[0]);
    mp_obj_t duration_obj = args[1];
    mp_obj_t data_obj = n_args > 2 ? args[2] : mp_const_true;

    mp_uint_t duration_const = 0;
    size_t duration_len = 0;
    mp_obj_t *duration_arr = NULL;

    size_t data_len = 0;
    mp_obj_t *data_arr = NULL;

    bool mode1_toggle = false;
    mp_uint_t data_toggle = 0;
    mp_uint_t num_pulses = 0;

    if (!(mp_obj_is_type(data_obj, &mp_type_tuple) || mp_obj_is_type(data_obj, &mp_type_list))) {
        // Mode 1: durations array, and a starting level (bool), toggling each pulse
        mp_obj_get_array(duration_obj, &duration_len, &duration_arr);
        data_toggle = mp_obj_is_true(data_obj);  // starting level
        mode1_toggle = true;
        num_pulses = duration_len;
    } else if (mp_obj_is_int(duration_obj)) {
        // Mode 2: constant duration, array of levels
        duration_const = mp_obj_get_int(duration_obj);
        mp_obj_get_array(data_obj, &data_len, &data_arr);
        num_pulses = data_len;
    } else {
        // Mode 3: arrays of durations and levels, same length
        mp_obj_get_array(duration_obj, &duration_len, &duration_arr);
        mp_obj_get_array(data_obj, &data_len, &data_arr);
        if (duration_len != data_len) {
            mp_raise_ValueError(MP_ERROR_TEXT("duration and data must have same length"));
        }
        num_pulses = duration_len;
    }

    if (num_pulses == 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("No pulses"));
    }
    if (self->loop_en) {
        mp_raise_NotImplementedError(MP_ERROR_TEXT("loop mode not implemented on RMT NG in this wrapper"));
    }

    // Two pulses per symbol
    mp_uint_t num_symbols = (num_pulses / 2) + (num_pulses % 2);
    if (num_symbols > self->num_symbols) {
        self->symbols = (rmt_symbol_word_t *)m_realloc(self->symbols, num_symbols * sizeof(rmt_symbol_word_t));
        self->num_symbols = num_symbols;
    }

    // Build symbols (durations are in "ticks" at resolution_hz, same as legacy)
    mp_uint_t pulse_index = 0;
    for (mp_uint_t i = 0; i < num_symbols; ++i) {
        // first half
        uint32_t d0 = duration_len ? mp_obj_get_int(duration_arr[pulse_index]) : duration_const;
        uint32_t l0;
        if (mode1_toggle) {
            l0 = (data_toggle & 1);
            data_toggle ^= 1; // toggle for next pulse
        } else {
            l0 = mp_obj_is_true(data_len ? data_arr[pulse_index] : mp_const_false);
        }
        pulse_index++;

        // second half (if any)
        uint32_t d1 = 0;
        uint32_t l1 = 0;
        if (pulse_index < num_pulses) {
            d1 = duration_len ? mp_obj_get_int(duration_arr[pulse_index]) : duration_const;
            if (mode1_toggle) {
                l1 = (data_toggle & 1);
                data_toggle ^= 1;
            } else {
                l1 = mp_obj_is_true(data_len ? data_arr[pulse_index] : mp_const_false);
            }
            pulse_index++;
        }

        self->symbols[i] = (rmt_symbol_word_t) {
            .level0 = l0 ? 1 : 0,
            .duration0 = d0,
            .level1 = l1 ? 1 : 0,
            .duration1 = d1,
        };
    }

    // Transmit
    rmt_transmit_config_t tx_cfg = {
        .loop_count = 0, // no loop
        // NG idle level can be controlled via eot_level flag (available in some IDF versions).
        // We keep defaults; adjust if you need a specific idle level at end-of-transmission.
    };

    check_esp_err(rmt_transmit(self->tx_channel, self->copy_encoder,
                               self->symbols, num_symbols * sizeof(rmt_symbol_word_t),
                               &tx_cfg));

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(esp32_rmt_write_pulses_obj, 2, 3, esp32_rmt_write_pulses);

/* ------------------------ bitstream channel (compat) ------------------------ */

static mp_obj_t esp32_rmt_bitstream_channel(size_t n_args, const mp_obj_t *args) {
    // In NG, channels are allocated by the driver; we can only keep a compatibility stub.
    if (n_args > 0) {
        if (args[0] == mp_const_none) {
            esp32_rmt_bitstream_channel_id = -1;
        } else {
            // We accept but cannot actually force a specific channel index with NG API.
            esp32_rmt_bitstream_channel_id = mp_obj_get_int(args[0]);
        }
    }
    if (esp32_rmt_bitstream_channel_id < 0) {
        return mp_const_none;
    } else {
        return MP_OBJ_NEW_SMALL_INT(esp32_rmt_bitstream_channel_id);
    }
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(esp32_rmt_bitstream_channel_fun_obj, 0, 1, esp32_rmt_bitstream_channel);
static MP_DEFINE_CONST_STATICMETHOD_OBJ(esp32_rmt_bitstream_channel_obj, MP_ROM_PTR(&esp32_rmt_bitstream_channel_fun_obj));

/* ------------------------ dict/type ------------------------ */

static const mp_rom_map_elem_t esp32_rmt_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR___del__),      MP_ROM_PTR(&esp32_rmt_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit),       MP_ROM_PTR(&esp32_rmt_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR_clock_div),    MP_ROM_PTR(&esp32_rmt_clock_div_obj) },
    { MP_ROM_QSTR(MP_QSTR_wait_done),    MP_ROM_PTR(&esp32_rmt_wait_done_obj) },
    { MP_ROM_QSTR(MP_QSTR_loop),         MP_ROM_PTR(&esp32_rmt_loop_obj) },
    { MP_ROM_QSTR(MP_QSTR_write_pulses), MP_ROM_PTR(&esp32_rmt_write_pulses_obj) },

    // Static methods
    { MP_ROM_QSTR(MP_QSTR_bitstream_channel), MP_ROM_PTR(&esp32_rmt_bitstream_channel_obj) },

    // Class methods
    { MP_ROM_QSTR(MP_QSTR_source_freq),  MP_ROM_PTR(&esp32_rmt_source_obj) },

    // Constants (legacy)
    { MP_ROM_QSTR(MP_QSTR_PULSE_MAX),    MP_ROM_INT(32767) },
};
static MP_DEFINE_CONST_DICT(esp32_rmt_locals_dict, esp32_rmt_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    esp32_rmt_type,
    MP_QSTR_RMT,
    MP_TYPE_FLAG_NONE,
    make_new, esp32_rmt_make_new,
    print, esp32_rmt_print,
    locals_dict, &esp32_rmt_locals_dict
);
