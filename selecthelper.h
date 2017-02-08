/*
 * This file is part of libwandevent
 *
 * Copyright (c) 2009 The University of Waikato, Hamilton, New Zealand.
 *
 * Authors: Perry Lorier
 * 	    Shane Alcock
 *
 * All rights reserved.
 *
 * This code has been developed by the University of Waikato WAND research
 * group. For further information, please see http://www.wand.net.nz/
 *
 * libwandevent is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License (GPL) as
 * published by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * libwandevent is distributed in the hope that it will be useful but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with libwandevent; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Any feedback (bug reports, suggestions, complaints) should be sent to
 * contact@wand.net.nz
 *
 */

#ifndef SELECTHELPER_H_
#define SELECTHELPER_H_

#include "libwandevent.h"

void process_select_event(wand_event_handler_t *ev_hdl,
		int fd, fd_set *xrfd, fd_set *xwfd, fd_set *xxfd);
struct timeval calculate_select_delay(wand_event_handler_t *ev_hdl,
		struct wand_timer_t *next);

#endif
