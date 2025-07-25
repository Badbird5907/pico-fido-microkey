/*
 * This file is part of the Pico FIDO distribution (https://github.com/polhenarejos/pico-fido).
 * Copyright (c) 2022 Pol Henarejos.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "fido.h"
#include "pico_keys.h"
#include "apdu.h"
#include "version.h"
#include "files.h"
#include "asn1.h"
#include "management.h"

int man_process_apdu();
int man_unload();

const uint8_t man_aid[] = {
    8,
    0xa0, 0x00, 0x00, 0x05, 0x27, 0x47, 0x11, 0x17
};
extern void scan_all();
extern void init_otp();
int man_select(app_t *a, uint8_t force) {
    a->process_apdu = man_process_apdu;
    a->unload = man_unload;
    sprintf((char *) res_APDU, "%d.%d.0", PICO_FIDO_VERSION_MAJOR, PICO_FIDO_VERSION_MINOR);
    res_APDU_size = (uint16_t)strlen((char *) res_APDU);
    apdu.ne = res_APDU_size;
    if (force) {
        scan_all();
        init_otp();
    }
    return PICOKEY_OK;
}

INITIALIZER ( man_ctor ) {
    register_app(man_select, man_aid);
}

int man_unload() {
    return PICOKEY_OK;
}

bool cap_supported(uint16_t cap) {
    file_t *ef = search_dynamic_file(EF_DEV_CONF);
    if (file_has_data(ef)) {
        uint16_t tag = 0x0;
        uint8_t *tag_data = NULL, *p = NULL;
        uint16_t tag_len = 0;
        asn1_ctx_t ctxi;
        asn1_ctx_init(file_get_data(ef), file_get_size(ef), &ctxi);
        while (walk_tlv(&ctxi, &p, &tag, &tag_len, &tag_data)) {
            if (tag == TAG_USB_ENABLED) {
                uint16_t ecaps = tag_data[0];
                if (tag_len == 2) {
                    ecaps = get_uint16_t_be(tag_data);
                }
                return ecaps & cap;
            }
        }
    }
    return true;
}

static uint8_t _openpgp_aid[] = {
    6,
    0xD2, 0x76, 0x00, 0x01, 0x24, 0x01,
};
static uint8_t _piv_aid[] = {
    5,
    0xA0, 0x00, 0x00, 0x03, 0x8,
};

int man_get_config() {
    file_t *ef = search_dynamic_file(EF_DEV_CONF);
    res_APDU_size = 0;
    res_APDU[res_APDU_size++] = 0; // Overall length. Filled later
    res_APDU[res_APDU_size++] = TAG_USB_SUPPORTED;
    res_APDU[res_APDU_size++] = 2;
    uint16_t caps = CAP_FIDO2 | CAP_OTP | CAP_U2F | CAP_OATH;
    if (app_exists(_openpgp_aid + 1, _openpgp_aid[0])) {
        caps |= CAP_OPENPGP;
    }
    if (app_exists(_piv_aid + 1, _piv_aid[0])) {
        caps |= CAP_PIV;
    }
    res_APDU[res_APDU_size++] = caps >> 8;
    res_APDU[res_APDU_size++] = caps & 0xFF;
    res_APDU[res_APDU_size++] = TAG_SERIAL;
    res_APDU[res_APDU_size++] = 4;
    memcpy(res_APDU + res_APDU_size, pico_serial.id, 4);
    res_APDU[res_APDU_size] &= ~0xFC; // Force 8-digit serial number
    res_APDU_size += 4;
    res_APDU[res_APDU_size++] = TAG_FORM_FACTOR;
    res_APDU[res_APDU_size++] = 1;
    res_APDU[res_APDU_size++] = 0x01;
    res_APDU[res_APDU_size++] = TAG_VERSION;
    res_APDU[res_APDU_size++] = 3;
    res_APDU[res_APDU_size++] = PICO_FIDO_VERSION_MAJOR;
    res_APDU[res_APDU_size++] = PICO_FIDO_VERSION_MINOR;
    res_APDU[res_APDU_size++] = 0;
    if (!file_has_data(ef)) {
        res_APDU[res_APDU_size++] = TAG_USB_ENABLED;
        res_APDU[res_APDU_size++] = 2;
        uint16_t caps = 0;
        if (cap_supported(CAP_FIDO2)) {
            caps |= CAP_FIDO2;
        }
        if (cap_supported(CAP_OTP)) {
            caps |= CAP_OTP;
        }
        if (cap_supported(CAP_U2F)) {
            caps |= CAP_U2F;
        }
        if (cap_supported(CAP_OATH)) {
            caps |= CAP_OATH;
        }
        if (cap_supported(CAP_OPENPGP)) {
            caps |= CAP_OPENPGP;
        }
        if (cap_supported(CAP_PIV)) {
            caps |= CAP_PIV;
        }
        res_APDU[res_APDU_size++] = caps >> 8;
        res_APDU[res_APDU_size++] = caps & 0xFF;
        res_APDU[res_APDU_size++] = TAG_DEVICE_FLAGS;
        res_APDU[res_APDU_size++] = 1;
        res_APDU[res_APDU_size++] = FLAG_EJECT;
        res_APDU[res_APDU_size++] = TAG_CONFIG_LOCK;
        res_APDU[res_APDU_size++] = 1;
        res_APDU[res_APDU_size++] = 0x00;
    }
    else {
        memcpy(res_APDU + res_APDU_size, file_get_data(ef), file_get_size(ef));
        res_APDU_size += file_get_size(ef);
    }
    res_APDU[0] = (uint8_t)(res_APDU_size - 1);
    return 0;
}

int cmd_read_config() {
    man_get_config();
    return SW_OK();
}

int cmd_write_config() {
    if (apdu.data[0] != apdu.nc - 1) {
        return SW_WRONG_DATA();
    }
    file_t *ef = file_new(EF_DEV_CONF);
    file_put_data(ef, apdu.data + 1, (uint16_t)(apdu.nc - 1));
    low_flash_available();
#ifndef ENABLE_EMULATION
    if (cap_supported(CAP_OTP)) {
        phy_data.enabled_usb_itf |= PHY_USB_ITF_KB;
    }
    else {
        phy_data.enabled_usb_itf &= ~PHY_USB_ITF_KB;
    }
    phy_save();
#endif
    return SW_OK();
}

extern int cbor_reset();
int cmd_factory_reset() {
    cbor_reset();
    return SW_OK();
}

#define INS_READ_CONFIG             0x1D
#define INS_WRITE_CONFIG            0x1C
#define INS_RESET                   0x1E    // Reset device

static const cmd_t cmds[] = {
    { INS_READ_CONFIG, cmd_read_config },
    { INS_WRITE_CONFIG, cmd_write_config },
    { INS_RESET, cmd_factory_reset },
    { 0x00, 0x0 }
};

int man_process_apdu() {
    if (CLA(apdu) != 0x00) {
        return SW_CLA_NOT_SUPPORTED();
    }
    for (const cmd_t *cmd = cmds; cmd->ins != 0x00; cmd++) {
        if (cmd->ins == INS(apdu)) {
            int r = cmd->cmd_handler();
            return r;
        }
    }
    return SW_INS_NOT_SUPPORTED();
}
