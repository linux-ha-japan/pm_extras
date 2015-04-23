/*
 * ifhceckd - Daemon for checking corosync ring status
 *
 * Copyright (C) 2013 NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <corosync/cfg.h>
#include <corosync/cmap.h>

#include <crm/attrd.h>
#include <crm/common/mainloop.h>
#include <crm_internal.h>

/**
 * default system name
 * (normally, execute file name)
 */
#define DEFAULT_SYS_NAME "ifcheckd"

/**
 * default pid file path
 */
#define PID_FILE "/var/run/ifcheckd.pid"

/**
 * retry number when we get value from cmap
 */
#define CMAP_MAX_RETRIES 10

/**
 * attribute value when status is faulty
 */
#define STATE_FAULTY "FAULTY"

/**
 * attribute value when status isn't faulty
 */
#define STATE_UP "UP"

/**
 * attribute value when status is another
 * (normally, this doesn't appear)
 */
#define STATE_UNKOWN "UNKOWN"

/**
 * the max length of string
 * (attribute name, attribute value, ip, etc)
 */
#define MAX_LENGTH 255

/**
 * a max buffer size of pid file
 */
#define LOCKSTRLEN  11

/**
 * default wait time(seconds)
 */
#define DEFAULT_INTERVAL 1

/**
 * trace faulty key
 */
#define FAULTY_TRACE_KEY "runtime.totem.pg.mrp.rrp."

/**
 * format to make faulty key from ring id
 */
#define FAULTY_KEY_MAKE_FORMAT FAULTY_TRACE_KEY "%u.faulty"

/**
 * format to get ring id and a last element from faulty key
 */
#define FAULTY_KEY_SCAN_FORMAT FAULTY_TRACE_KEY "%u.%s"

/**
 * trace connectios key
 */
#define CONNECTIONS_TRACE_KEY "runtime.connections."

/**
 * format to get a last element from connections key
 */
#define CONNECTIONS_KEY_SCAN_FORMAT CONNECTIONS_TRACE_KEY "%[^.].%s"

/**
 * Pacemaker process name
 * cmap is set runtime.connections."ID".name
 */
#define PACEMAKER_PNAME "pacemakerd"

/**
 * attribute name format
 */
#define ATTR_NAME_FORMAT "ringnumber_%u"

/**
 * attribute value format
 */
#define ATTR_VALUE_FORMAT "%s is %s"

/**
 * kind of configure
 */
enum {
    IF_CH_FG = 0,
    IF_CH_MAX
};

/**
 * wait time structure
 */
struct wait_time {
    guint timer_id; /**< timer is id */
    guint seconds; /**< timer is interval */
};

/**
 * mainloop
 */
GMainLoop *mainloop = NULL;

/**
 * pid file name
 */
static char *pid_file;

/**
 * set each option, if true or false.
 */
static gboolean conf[IF_CH_MAX];

/**
 * cmap handler
 */
static cmap_handle_t cmap_handle;

/**
 * cmap fd source
 */
static mainloop_io_t* cmap_source;

/**
 * timeout timer when init
 */
static struct wait_time w_timer;

/**
 * options index
 */
static struct crm_option long_options[] = {
        {"help", 0, 0, '?', "\tThis text"},
        {"version", 0, 0, '$', "\tVersion information"},
        {"verbose", 0, 0, 'V', "\tIncrease debug output"},
        {"pid-file", 1, 0, 'p', "\t(Advanced) Daemon pid file location"},
        {"foreground", 0, 0, 'f', "\tStart application in foreground"},
        {NULL, 0, 0, 0}
};

#if PACEMAKER_GE_1113
static int attr_options = attrd_opt_none;
#else
static gboolean attr_options = FALSE;
#endif

void ifcheckd_init(void);
void ifcheckd_finalize(void);

/**
 * this function is used when the program is executed as foreground
 * (this is equal to the same name of function in utile.c)
 */
static int
crm_pidfile_inuse(const char *filename, long mypid)
{
    long pid = 0;
    struct stat sbuf;
    char buf[LOCKSTRLEN + 1];
    int rc = -ENOENT, fd = 0;

    if ((fd = open(filename, O_RDONLY)) >= 0) {
        if (fstat(fd, &sbuf) >= 0 && sbuf.st_size < LOCKSTRLEN) {
            sleep(2);           /* if someone was about to create one,
                                 * give'm a sec to do so
                                 */
        }
        if (read(fd, buf, sizeof(buf)) > 0) {
            if (sscanf(buf, "%lu", &pid) > 0) {
                crm_trace("Got pid %lu from %s\n", pid, filename);
                if (pid <= 1) {
                    /* Invalid pid */
                    rc = -ENOENT;
                    unlink(filename);

                } else if (mypid && pid == mypid) {
                    /* In use by us */
                    rc = pcmk_ok;

                } else if (crm_pid_active(pid) == FALSE) {
                    /* Contains a stale value */
                    unlink(filename);
                    rc = -ENOENT;

                } else if (mypid && pid != mypid) {
                    /* locked by existing process - give up */
                    rc = -EEXIST;
                }
            }
        }
        close(fd);
    }
    return rc;
}

/**
 * this function is used when the program is executed as foreground
 * (this is equal to the same name of function in utile.c)
 */
static int
crm_read_pidfile(const char *filename)
{
    int fd;
    long pid = -1;
    char buf[LOCKSTRLEN + 1];

    if ((fd = open(filename, O_RDONLY)) < 0) {
        goto bail;
    }

    if (read(fd, buf, sizeof(buf)) < 1) {
        goto bail;
    }

    if (sscanf(buf, "%lu", &pid) > 0) {
        if (pid <= 0) {
            pid = -ESRCH;
        }
    }

  bail:
    if (fd >= 0) {
        close(fd);
    }
    return pid;
}

/**
 * this function is used the program is executed as foreground
 * (this is equal to the same name of function in utile.c)
 */
static int
crm_lock_pidfile(const char *filename)
{
    long mypid = 0;
    int fd = 0, rc = 0;
    char buf[LOCKSTRLEN + 1];

    mypid = (unsigned long)getpid();

    rc = crm_pidfile_inuse(filename, 0);
    if (rc == -ENOENT) {
        /* exists but the process is not active */

    } else if (rc != pcmk_ok) {
        /* locked by existing process - give up */
        return rc;
    }

    if ((fd = open(filename, O_CREAT | O_WRONLY | O_EXCL, 0644)) < 0) {
        /* Hmmh, why did we fail? Anyway, nothing we can do about it */
        return -errno;
    }

    snprintf(buf, sizeof(buf), "%*lu\n", LOCKSTRLEN - 1, mypid);
    rc = write(fd, buf, LOCKSTRLEN);
    close(fd);

    if (rc != LOCKSTRLEN) {
        crm_perror(LOG_ERR, "Incomplete write to %s", filename);
        return -errno;
    }

    return crm_pidfile_inuse(filename, mypid);
}

/**
 * SIGNAL handler function.
 * @param nsig the number of SIGNAL. except as called by signal trap,
 * if this function is called param is -1.
 */
static void
_ifcheckd_shutdown(int nsig)
{
    crm_debug("mainloop shutdown. SIGNAL is %d", nsig);
    if (mainloop != NULL && g_main_loop_is_running(mainloop)) {
        g_main_loop_quit(mainloop);
        return;
    }
    ifcheckd_finalize();
    unlink(pid_file);
    free(pid_file);
    crm_notice("Exiting %s", crm_system_name);
    crm_exit(EX_OK);
}

/**
 * cmap dispatch function
 * @param user_data the gpointer of user data
 * @return CS_OK is 0, otherwise, -1
 */
static int
_cs_cmap_dispatch(gpointer user_data)
{
    cs_error_t rc = cmap_dispatch(cmap_handle, CS_DISPATCH_ONE);
    if (rc != CS_OK) {
        crm_debug("Failed to dispatch cmap: Error %d", rc);
        return -1;
    }
    return 0;
}

/**
 * cmap destroy function
 * @param user_data the gpointer of user data
 */
static void
_cs_cmap_destroy(gpointer user_data)
{
    crm_notice("Stop monitoring interface. cmap connection is destroyed");
    ifcheckd_finalize();
    /* run init when corosync stopped */
    ifcheckd_init();
}

/**
 * Delete a specified attribute by the ring number
 * @param iface_no the ring number
 * @param size the length of attribute name and attribute value
 * @return if a attribute can be deleted, TRUE. otherwise FALSE
 */
static gboolean
_delete_attr_iface(uint32_t iface_no,
        size_t size)
{
    char if_attr[size];
    const char *if_value = NULL;
    const char *attr_section = NULL;
    const char *attr_set = NULL;
    int len = -1;

    len = snprintf(if_attr, size, ATTR_NAME_FORMAT, iface_no);
    if (!(-1 < len && len < size)) {
        crm_debug("Failed to copy ring number: len=%d", len);
        return FALSE;
    }
    if (attrd_update_delegate(NULL, 'D', NULL, if_attr, if_value,
                    attr_section, attr_set, NULL, NULL, attr_options) != pcmk_ok) {
        crm_debug("Could not delete %s", if_attr);
        return FALSE;
    }
    return TRUE;
}

/**
 * Update a specified attribute by ring number
 * @param iface_no the ring number
 * @param interface_name the string of ip
 * @param state the string of ring link status
 * @param size the length of attribute name and attribute value
 * @return if a attribute can be updated, TRUE. otherwise, FALSE
 */
static gboolean
_update_attr_iface(uint32_t iface_no,
        const char *interface_name,
        const char *state,
        size_t size)
{
    char if_attr[size];
    char if_value[size];
    const char *attr_section = NULL;
    const char *attr_set = NULL;
    int len = -1;

    len = snprintf(if_attr, size, ATTR_NAME_FORMAT, iface_no);
    if (!(-1 < len && len < size)) {
        crm_debug("Failed to copy ring number: len=%d", len);
        return FALSE;
    }
    len = snprintf(if_value, size, ATTR_VALUE_FORMAT, interface_name, state);
    if (!(-1 < len && len < size)) {
        crm_debug("Failed to copy interface name: len=%d", len);
        return FALSE;
    }
    if (attrd_update_delegate(NULL, 'U', NULL, if_attr, if_value,
                    attr_section, attr_set, NULL, NULL, attr_options) != pcmk_ok) {
        crm_debug("Could not update %s=%s", if_attr, if_value);
        return FALSE;
    }
    return TRUE;
}

/**
 * cfg_ring_status is released
 * @param interface_count the number of interface
 * @param interface_names the string of the interface name
 * @param interface_status the string of the ring status
 */
static void
_corosync_cfg_ring_status_free(unsigned int interface_count,
        char **interface_names,
        char **interface_status)
{
    int i;
    for (i = 0; i < interface_count; i++) {
        free(interface_names[i]);
        free(interface_status[i]);
    }
    free(interface_names);
    free(interface_status);
}

/**
 * Delete all attributes relating to ring number
 * @return if all attributes can be delete, TRUE. otherwise, FALSE
 */
static gboolean
_attr_iface_finalize(void)
{
    size_t size = MAX_LENGTH;
    cs_error_t result;
    corosync_cfg_handle_t handle;
    unsigned int interface_count;
    char **interface_names;
    char **interface_status;
    unsigned int i;

    gboolean rc = FALSE;

    crm_debug("Start to finalize attribute information.");
    result = corosync_cfg_initialize(&handle, NULL);
    if (result != CS_OK) {
        crm_debug("Could not initialize corosync configuration API error %d",
                result);
        return FALSE;
    }

    result = corosync_cfg_ring_status_get(handle, &interface_names,
            &interface_status, &interface_count);
    if (result != CS_OK) {
        crm_debug("Could not get the ring status, the error is %d", result);
        rc = FALSE;
    } else {
        for (i = 0; i < interface_count; i++) {
            crm_debug("ring id=%d, ifname= %s, status= %s",
                    i, interface_names[i], interface_status[i]);

            if (_delete_attr_iface(i, size) == FALSE) {
                crm_debug("Failed to delete attribute");
                rc = FALSE;
                goto out_free;
            }
        }
        rc = TRUE;
    }

    out_free:
        _corosync_cfg_ring_status_free(interface_count,
                interface_names,
                interface_status);
        (void) corosync_cfg_finalize(handle);
        return rc;
}

/**
 * Update all attributes relating to ring number
 * @return if all attributes can be updated, TRUE. otherwise, FALSE
 */
static gboolean
_attr_iface_init(void)
{
    size_t size = MAX_LENGTH;
    cs_error_t result;
    corosync_cfg_handle_t handle;
    cmap_handle_t handle2;
    uint8_t faulty;
    char tmp_key[CMAP_KEYNAME_MAXLEN];
    int no_retries;
    unsigned int interface_count;
    char **interface_names;
    char **interface_status;
    char state[size];
    unsigned int i;
    int len;
    gboolean rc = FALSE;

    crm_debug("Start to initialize attribute information.");
    result = corosync_cfg_initialize(&handle, NULL);
    if (result != CS_OK) {
        crm_debug("Could not initialize corosync configuration API error %d",
                result);
        return FALSE;
    }

    result = cmap_initialize(&handle2);
    if (result != CS_OK) {
        crm_debug("Failed to initialize the cmap API. Error %d", result);
        (void) corosync_cfg_finalize(handle);
        return FALSE;
    }

    result = corosync_cfg_ring_status_get(handle, &interface_names,
            &interface_status, &interface_count);
    if (result != CS_OK) {
        crm_debug("Could not get the ring status, the error is %d", result);
        rc = FALSE;
    } else {
        for (i = 0; i < interface_count; i++) {
            crm_debug("ring id=%d, ifname= %s, status= %s",
                    i, interface_names[i], interface_status[i]);
            len = snprintf(tmp_key, CMAP_KEYNAME_MAXLEN, FAULTY_KEY_MAKE_FORMAT, i);
            if (!(-1 < len && len < size)) {
                crm_debug("Failed to copy string: len=%d", len);
                rc = FALSE;
                goto out_free;
            }

            no_retries = 0;
            while ((result = cmap_get_uint8(handle2, tmp_key, &faulty))
                    == CS_ERR_TRY_AGAIN && no_retries++ < CMAP_MAX_RETRIES) {
                sleep(1);
            }

            if (result != CS_OK) {
                crm_debug("Failed to connect cmap.  Error %d", result);
                rc = FALSE;
                goto out_free;
            }

            if (faulty == 0){
                len = snprintf(state, size, STATE_UP);
            } else if (faulty == 1){
                len = snprintf(state, size, STATE_FAULTY);
            } else {
                len = snprintf(state, size, STATE_UNKOWN);
            }
            if (!(-1 < len && len < size)) {
                crm_debug("Failed to copy string: len=%d", len);
                rc = FALSE;
                goto out_free;
            }
            if (_update_attr_iface(i, interface_names[i], state, size) == FALSE) {
                crm_debug("Failed to send value to attrd");
                rc = FALSE;
                goto out_free;
            }
        }
        rc = TRUE;
    }

    out_free:
        _corosync_cfg_ring_status_free(interface_count,
                interface_names,
                interface_status);
        (void) corosync_cfg_finalize(handle);
        (void) cmap_finalize(handle2);
        return rc;
}

/**
 * Get ip from ring number
 * @param ring_id ring number
 * @param interface_name the string of ip
 * @param size size the length of interface_name
 * @return if ip can be gotten, TRUE. otherwise FALSE.
 */
static gboolean
_get_interface_name(uint32_t ring_id,
        char *interface_name,
        size_t size)
{
    cs_error_t result;
    corosync_cfg_handle_t handle;
    unsigned int interface_count;
    char **interface_names;
    char **interface_status;
    int len = -1;
    gboolean rc = FALSE;

    crm_debug("Start to get interface name.");
    result = corosync_cfg_initialize(&handle, NULL);
    if (result != CS_OK) {
        crm_debug("Could not initialize corosync configuration API error %d",
                result);
        return FALSE;
    }

    result = corosync_cfg_ring_status_get(handle, &interface_names,
            &interface_status, &interface_count);
    if (result != CS_OK) {
        crm_debug("Could not get the ring status, the error is %d", result);
        rc = FALSE;
    } else {
        if (ring_id < interface_count) {
            crm_debug("ring id=%u, ifname= %s, status= %s",
                    ring_id, interface_names[ring_id], interface_status[ring_id]);
            len = snprintf(interface_name, size, "%s", interface_names[ring_id]);
            if (-1 < len && len < size) {
                rc = TRUE;
                goto out_free;
            } else {
                rc = FALSE;
                crm_debug("Failed to copy interface name: len=%d", len);
                goto out_free;
            }
        }
    }
    rc = FALSE;
    crm_debug("Not found the appropriate ring id");

    out_free:
        _corosync_cfg_ring_status_free(interface_count,
                interface_names,
                interface_status);
        (void) corosync_cfg_finalize(handle);
        return rc;
}

/**
 * Send the link status to a attribute relating to ring number
 * @param iface_no ring number
 * @param state the string of link status
 * @param size the length of interface_name(the string of ip)
 * @return if link status can be sent, TRUE. otherwise, FALSE.
 */
static gboolean
_send_attr_iface(uint32_t iface_no,
        const char *state,
        size_t size)
{
    char interface_name[size];
    gboolean rc = FALSE;

    rc = _get_interface_name(iface_no, interface_name, size);
    if (rc == FALSE) {
        crm_debug("Failed to convert a ring id into a interface name");
        return rc;
    }
    rc = _update_attr_iface(iface_no, interface_name, state, size);
    if (rc == FALSE) {
        crm_debug("Failed to send to attrd");
        return rc;
    }
    return TRUE;
}

/**
 * Update link status relating to ring number
 * @param iface_no ring number
 * @param state the string of link status
 */
static void
_cs_rrp_faulty_event(uint32_t iface_no,
        const char *state)
{
    if (_send_attr_iface(iface_no, state, MAX_LENGTH) == FALSE) {
        crm_err("Failed to change link status [ring id=%u, expected state=%s]",
                iface_no, state);
        return;
    }
    crm_info("Interface link status changed [ring id=%u, state=%s]",
            iface_no, state);
}

/**
 * cmap connections key trace function
 * @param cmap_handle_c cmap_handle_t
 * @param cmap_track_handle cmap_track_handle
 * @param event int32_t
 * @param key_name const char
 * @param new_value cmap_notify_value
 * @param old_value cmap_notify_value
 * @param user_data void
 * @see in detail about arguments, see corosync cmap API
 */
static void
_cs_cmap_connections_key_changed(cmap_handle_t cmap_handle_c,
        cmap_track_handle_t cmap_track_handle,
        int32_t event,
        const char *key_name,
        struct cmap_notify_value new_value,
        struct cmap_notify_value old_value,
        void *user_data)
{
    char *value;
    char conn_str[CMAP_KEYNAME_MAXLEN];
    char tmp_key[CMAP_KEYNAME_MAXLEN];
    int result;

    result = sscanf(key_name, CONNECTIONS_KEY_SCAN_FORMAT, conn_str, tmp_key);
    if (result != 2) {
        crm_debug("Failed to fetch ID or key: result=%d", result);
        return;
    }

    if (strcmp(tmp_key, "name") != 0) {
        crm_debug("key isn't name[key=%s]", tmp_key);
        return;
    }

    if (old_value.type != CMAP_VALUETYPE_STRING) {
        crm_debug("old_value isn't the string");
        return;
    }

    value = malloc(old_value.len + 1);
    memcpy(value, old_value.data, old_value.len);

    if (strcmp(value, PACEMAKER_PNAME) == 0) {
        if (event == CMAP_TRACK_DELETE) {
            /* a notification is ignored when already run timer*/
            if (w_timer.timer_id == 0) {
                crm_notice("Stop monitoring interface. Notified of Pacemaker stop event");
                /* run init when pacemaker left */
                ifcheckd_init();
            }
        } else {
            crm_err("the event isn't exist: event=%u", event);
        }
    }

    free(value);
}

/**
 * cmap faulty key trace function
 * @param cmap_handle_c cmap_handle_t
 * @param cmap_track_handle cmap_track_handle
 * @param event int32_t
 * @param key_name const char
 * @param new_value cmap_notify_value
 * @param old_value cmap_notify_value
 * @param user_data void
 * @see in detail about arguments, see corosync cmap API
 */
static void
_cs_cmap_rrp_faulty_key_changed(cmap_handle_t cmap_handle_c,
        cmap_track_handle_t cmap_track_handle,
        int32_t event,
        const char *key_name,
        struct cmap_notify_value new_value,
        struct cmap_notify_value old_value,
        void *user_data)
{
    uint32_t iface_no;
    char tmp_key[CMAP_KEYNAME_MAXLEN];
    int result;
    int no_retries;
    uint8_t faulty;
    cs_error_t err;

    result = sscanf(key_name, FAULTY_KEY_SCAN_FORMAT, &iface_no,
            tmp_key);
    if (result != 2) {
        crm_err("Failed to fetch key name or ring name: result=%d", result);
        return;
    }

    if (strcmp(tmp_key, "faulty") != 0) {
        crm_err("Failed to fetch key name: tmp_key=%s", tmp_key);
        return;
    }

    no_retries = 0;
    while ((err = cmap_get_uint8(cmap_handle, key_name, &faulty))
            == CS_ERR_TRY_AGAIN && no_retries++ < CMAP_MAX_RETRIES) {
        sleep(1);
    }

    if (err != CS_OK) {
        crm_err("Failed to connect cmap.  Error %d", err);
        return;
    }

    if (faulty == 0) {
        _cs_rrp_faulty_event(iface_no, STATE_UP);
    } else if (faulty == 1) {
        _cs_rrp_faulty_event(iface_no, STATE_FAULTY);
    } else {
        _cs_rrp_faulty_event(iface_no, STATE_UNKOWN);
    }
}

/**
 * Add cmap to mainloop
 * @return if cmap can be added mainloop, TRUE. otherwise FALSE.
 */
static gboolean
_cs_cmap_init(void)
{
    cs_error_t rc;
    int cmap_fd = 0;
    cmap_track_handle_t track_handle;

    static struct mainloop_fd_callbacks cmap_fd_callbacks = {
            .dispatch = _cs_cmap_dispatch,
            .destroy = _cs_cmap_destroy,
    };

    rc = cmap_initialize(&cmap_handle);
    if (rc != CS_OK) {
        crm_debug("Failed to initialize the cmap API. Error %d", rc);
        return FALSE;
    }

    rc = cmap_fd_get(cmap_handle, &cmap_fd);
    if (rc != CS_OK) {
        crm_debug("Failed to get cmap fd. Error %d", rc);
        goto bail;
    }

    cmap_source = mainloop_add_fd("corosync-cmap",
            G_PRIORITY_DEFAULT,
            cmap_fd,
            &cmap_handle,
            &cmap_fd_callbacks);
    if (cmap_source == NULL) {
        crm_debug("Failed to add cmap fd to mainloop");
        goto bail;
    }

    rc = cmap_track_add(cmap_handle,
            FAULTY_TRACE_KEY,
            CMAP_TRACK_ADD | CMAP_TRACK_MODIFY | CMAP_TRACK_PREFIX,
            _cs_cmap_rrp_faulty_key_changed,
            NULL,
            &track_handle);
    if (rc != CS_OK) {
        crm_debug("Failed to track the faulty key. Error %d", rc);
        goto bail2;
    }

    rc = cmap_track_add(cmap_handle,
            CONNECTIONS_TRACE_KEY,
            CMAP_TRACK_DELETE | CMAP_TRACK_PREFIX,
            _cs_cmap_connections_key_changed,
            NULL,
            &track_handle);
    if (rc != CS_OK) {
        crm_debug("Failed to track the connections key. Error %d", rc);
        goto bail2;
    }

    return TRUE;

    bail2:
    mainloop_del_fd(cmap_source);

    bail:
    cmap_finalize(cmap_handle);
    cmap_handle = 0;
    return FALSE;
}

/**
 * Timeout function for initializing attributes
 * @return if initialization succeeded, TRUE. otherwise, FALSE.
 */
static gboolean
_regular_attr_init(gpointer interval)
{
    crm_debug("Start to initialize ifcheckd");

    if (_attr_iface_init() == FALSE) {
        return TRUE;
    }

    /* timer stop when we already have cmap_handle */
    if (cmap_handle != 0) {
        crm_debug("Finished to initialize ifcheckd. cmap_handle existed");
        crm_notice("Start to monitor interface after Pacemaker restarted");
        w_timer.timer_id = 0;
        return FALSE;
    }

    /* timer stop after we got cmap_handler */
    if (_cs_cmap_init() == TRUE) {
        crm_debug("Finished to initialize ifcheckd. cmap_handle created");
        crm_notice("Start to monitor interface");
        w_timer.timer_id = 0;
        return FALSE;
    }

    return TRUE;
}

/**
 * Finalize deamon.
 */
void
ifcheckd_finalize(void)
{
    (void)_attr_iface_finalize();
    (void)cmap_finalize(cmap_handle);
    cmap_handle = 0;
}

/**
 * Add initialize function to mainloop.
 */
void
ifcheckd_init(void)
{
    crm_debug("Start to regularly initialize attribute [interval %u(s)]", w_timer.seconds);
    if (w_timer.timer_id != 0) {
        crm_debug("The timer already existed");
        return;
    }
    w_timer.timer_id = g_timeout_add_seconds(w_timer.seconds,
            _regular_attr_init,
            &w_timer);
}

/**
 * Main function
 * @return if normal exit, 0
 */
int
main(int argc, char **argv)
{
    const char *crm_system_name = DEFAULT_SYS_NAME;
    int option_index = 0;
    int flag;
    conf[IF_CH_FG] = FALSE;
    w_timer.seconds = DEFAULT_INTERVAL;
    w_timer.timer_id = 0;
    pid_file = strdup(PID_FILE);
    cmap_source = NULL;
    cmap_handle = 0;

    crm_log_init(crm_system_name,
            LOG_INFO,
            TRUE,
            FALSE,
            argc,
            argv,
            FALSE);
    crm_set_options(NULL,
            "[options]",
            long_options,
            "Daemon for updating attribute by tracing corosync link status");

    while (1) {
        flag = crm_get_option(argc, argv, &option_index);
        if (flag == -1)
            break;

        switch (flag) {
        case 'V':
            crm_bump_log_level(argc, argv);
            break;
        case 'f':
            conf[IF_CH_FG] = TRUE;
            break;
        case 'p':
            free(pid_file);
            pid_file = strdup(optarg);
            break;
        case '?':
        case '$':
            crm_help(flag, EX_OK);
            break;
        default:
            crm_help(flag, EX_USAGE);
            break;
        }
    }

    if (conf[IF_CH_FG] == FALSE) {
        crm_make_daemon(crm_system_name, TRUE, pid_file);
    } else {
        int pid;
        int rc;
        rc = crm_pidfile_inuse(pid_file, 1);
        if (rc < pcmk_ok && rc != -ENOENT) {
            pid = crm_read_pidfile(pid_file);
            crm_err("%s: already running [pid %ld in %s]", crm_system_name, pid, pid_file);
            free(pid_file);
            crm_exit(rc);
        } else {
            rc = crm_lock_pidfile(pid_file);
            if(rc < pcmk_ok) {
                crm_err("Could not lock '%s' for %s: %s (%d)", pid_file, crm_system_name, pcmk_strerror(rc), rc);
                free(pid_file);
                crm_exit(rc);
            }
        }
    }

    crm_notice("Starting %s", crm_system_name);

    mainloop = g_main_loop_new(NULL, FALSE);
    mainloop_add_signal(SIGTERM, _ifcheckd_shutdown);
    mainloop_add_signal(SIGINT, _ifcheckd_shutdown);

    ifcheckd_init();
    g_main_loop_run(mainloop);

    ifcheckd_finalize();
    unlink(pid_file);
    free(pid_file);
    crm_notice("Exiting %s", crm_system_name);
    return crm_exit(EX_OK);
}
