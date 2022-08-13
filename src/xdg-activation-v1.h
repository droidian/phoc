/*
 * Copyright (C) 2021 Purism SPC
 * SPDX-License-Identifier: GPL-3.0+
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#pragma once

G_BEGIN_DECLS

void
phoc_xdg_activation_v1_handle_request_activate (struct wl_listener *listener,
                                                void               *data);

G_END_DECLS
