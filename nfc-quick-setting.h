/*
 * Copyright (C) 2026 Giuseppe Maggio
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <libphosh.h>

G_BEGIN_DECLS

#define PHOSH_TYPE_NFC_QUICK_SETTING phosh_nfc_quick_setting_get_type ()
G_DECLARE_FINAL_TYPE (PhoshNfcQuickSetting,
                      phosh_nfc_quick_setting,
                      PHOSH, NFC_QUICK_SETTING, PhoshQuickSetting)

G_END_DECLS
