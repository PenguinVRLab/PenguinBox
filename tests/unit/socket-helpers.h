/*
 * Helper functions for tests using sockets
 *
 * Copyright 2015-2018 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef TESTS_SOCKET_HELPERS_H
#define TESTS_SOCKET_HELPERS_H

int socket_check_protocol_support(bool *has_ipv4, bool *has_ipv6);

void socket_check_afunix_support(bool *has_afunix);

#endif
