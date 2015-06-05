/* crm_internal.h */
/* NOTE:
     This file is excerpted from the Pacemaker-1.1.12 source file for ifcheckd
     and needs to be updated accordingly if those interfaces would be
     changed in the future version.
*/

/*
 * Copyright (C) 2006 - 2008
 *     Andrew Beekhof <andrew@beekhof.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

struct crm_option {
    /* Fields from 'struct option' in getopt.h */
    /* name of long option */
    const char *name;
    /*
     * one of no_argument, required_argument, and optional_argument:
     * whether option takes an argument
     */
    int has_arg;
    /* if not NULL, set *flag to val when option found */
    int *flag;
    /* if flag not NULL, value to set *flag to; else return value */
    int val;

    /* Custom fields */
    const char *desc;
    long flags;
};

void crm_set_options(const char *short_options, const char *usage, struct crm_option *long_options,
                     const char *app_desc);
int crm_get_option(int argc, char **argv, int *index);
int crm_help(char cmd, int exit_code);

int crm_pid_active(long pid);
void crm_make_daemon(const char *name, gboolean daemonize, const char *pidfile);
