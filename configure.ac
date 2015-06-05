#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT(pm_extras, 2.0)
dnl Older distros may need: AM_INIT_AUTOMAKE($PACKAGE_NAME, $PACKAGE_VERSION)
AM_INIT_AUTOMAKE
AC_PROG_CC
AC_PROG_LIBTOOL

# use when make rpm
AC_SUBST(WITH_LIST, [""])

extract_header_define() {
          AC_MSG_CHECKING(for $2 in $1)
          Cfile=/tmp/extract_define.$2.${$}
          printf "#include <stdio.h>\n" > ${Cfile}.c
          printf "#include <%s>\n" $1 >> ${Cfile}.c
          printf "int main(int argc, char **argv) { printf(\"%%s\", %s); return 0; }\n" $2 >> ${Cfile}.c
          $CC $CFLAGS ${Cfile}.c -o ${Cfile}
          value=`${Cfile}`
          AC_MSG_RESULT($value)
          printf $value
          rm -f ${Cfile}.c ${Cfile}
        }

# Checks for programs.

# Checks for libraries.
corosync_version=2.0
pacemaker_version=1.1
PKG_CHECK_MODULES([GLIB], [glib-2.0])
PKG_CHECK_MODULES([COROSYNC], [corosync >= ${corosync_version}])
PKG_CHECK_MODULES([PACEMAKER], [pacemaker >= ${pacemaker_version}])
AC_CHECK_LIB([cmap],
	[cmap_track_add], 
	[], 
	[AC_MSG_ERROR("Not found cmap_track_add in libcmap")]
	)
AC_CHECK_LIB([cfg],
	[corosync_cfg_ring_status_get],
	[],
	[AC_MSG_ERROR("Not found corosync_cfg_ring_status_get in libcfg")]
	)
AC_CHECK_LIB([crmcommon],
	[crm_log_init],
	[],
	[AC_MSG_ERROR("Not found crm_log_init in libcrmcommon")]
	)
AC_CHECK_LIB([qb],
	[qb_log_from_external_source],
	[],
	[AC_MSG_ERROR("Not found qb_log_from_external_source in libqb")],
	)
AC_CHECK_LIB([glib-2.0],
	[g_main_loop_run],
	[],
	[AC_MSG_ERROR("Not found g_main_loop in libglib-2.0")],
	)
# Checks for options
AC_ARG_ENABLE([debug],
	[AS_HELP_STRING([--enable-debug],
		[enable debug mode (default is no)])],
	[],
	[debug=no] 
	)

AC_ARG_ENABLE([upstart],
	[AS_HELP_STRING([--enable-upstart],
		[support for the Upstart (default is auto)])],
	[],
	[enable_upstart=auto] 
	)
AM_CONDITIONAL(SUPPORT_UPSTART, [test x"${enable_upstart}" = x"yes"])

AC_ARG_ENABLE([systemd],
	[AS_HELP_STRING([--enable-systemd],
		[support for the Systemd (default is auto)])],
	[],
	[enable_systemd=auto] 
	)
AM_CONDITIONAL(SUPPORT_SYSTEMD, [test x"${enable_systemd}" = x"yes"])

if test "x${enable_debug}" = x"yes"; then
	CFLAGS="-g -O0"
fi

AC_CHECK_PROG([UPSTART], [initctl], [yes], [no])
if test "x${UPSTART}" = x"yes"; then
	if test "x${enable_upstart}" = x"yes" -o "x${enable_upstart}" = x"auto"; then
		WITH_LIST="${WITH_LIST} --with upstart"
		PACKAGE_FEATURES="$PACKAGE_FEATURES upstart"
	fi
elif test "x${enable_upstart}" = x"yes"; then
	AC_MSG_ERROR("Not found initctl")
fi

AC_CHECK_PROG([SYSTEMD], [systemctl], [yes], [no])
if test "x${SYSTEMD}" = x"yes"; then
	if test "x${enable_systemd}" = x"yes" -o "x${enable_systemd}" = x"auto"; then
		WITH_LIST="${WITH_LIST} --with systemd"
		PACKAGE_FEATURES="$PACKAGE_FEATURES systemd"
	fi
elif test "x${enable_systemd}" = x"yes"; then
	AC_MSG_ERROR("Not found systemctl")
fi

AC_MSG_RESULT([  WITH_LIST        = ${WITH_LIST}])
AC_MSG_RESULT([  PACKAGE_FEATURES = ${PACKAGE_FEATURES}])

# Checks for typedefs, structures, and compiler characteristics.
dnl Fix default variables - "prefix" variable if not specified

if test "$prefix" = "NONE"; then
	prefix="/usr"

	dnl Fix "localstatedir" variable if not specified
	
	if test "$localstatedir" = "\${prefix}/var"; then
		localstatedir="/var"
	fi
	dnl Fix "sysconfdir" variable if not specified
	
	if test "$sysconfdir" = "\${prefix}/etc"; then
		sysconfdir="/etc"
	fi
	dnl Fix "libdir" variable if not specified
	
	if test "$libdir" = "\${exec_prefix}/lib"; then
		if test -e /usr/lib64; then
			libdir="/usr/lib64"
		else
			libdir="/usr/lib"
		fi
	fi
fi

# Checks for header files.
INC_FLAGS="-I${prefix}/include/heartbeat"
INC_FLAGS="$INC_FLAGS -I${prefix}/include/pacemaker"
INC_FLAGS="$INC_FLAGS -I${prefix}/include/corosync"
INC_FLAGS="$INC_FLAGS -I${prefix}/include/glib-2.0"
INC_FLAGS="$INC_FLAGS -I/usr/lib64/glib-2.0/include"
INC_FLAGS="$INC_FLAGS -I/usr/include/libxml2"

CFLAGS="${CFLAGS} ${INC_FLAGS}"
CPPFLAGS="${CPPFLAGS} ${INC_FLAGS}"

# ifcheckd
AC_CHECK_HEADER([corosync/cfg.h],
	[],
	[AC_MSG_ERROR("Not found corosync/cfg.h")]
	)
AC_CHECK_HEADER([corosync/cmap.h],
	[],
	[AC_MSG_ERROR("Not found corosync/cmap.h")]
	)

# RA
AC_CHECK_HEADER([glue_config.h],
	[GLUE_HEADER=glue_config.h],
	[AC_MSG_ERROR("Not found glue_confi.h")]
	)
AC_CHECK_HEADER([agent_config.h],
	[AGENT_CONFIG_HEADER=agent_config.h],
	[AC_MSG_ERROR("Not found agent_config.h")]
	)
AC_CHECK_HEADER([crm_config.h],
	[CRM_CONFIG_HEADER=crm_config.h],
	[AC_MSG_ERROR("Not found crm_config.h")]
	)

AC_CHECK_DECL([OCF_RA_DIR],
	[],
	[AC_MSG_ERROR("Not found OCF_RA_DIR in ${GLUE_HEADER}")],
	[[#include <${GLUE_HEADER}>]]
	)
AC_CHECK_DECL([STONITH_EXT_PLUGINDIR],
	[],
	[AC_MSG_ERROR("Not found STONITH_EXT_PLUGINDIR in ${GLUE_HEADER}")],
	[[#include <${GLUE_HEADER}>]]
	)
AC_CHECK_DECL([HA_APIGROUP],
	[],
	[AC_MSG_ERROR("Not found HA_APIGROUP in ${GLUE_HEADER}")],
	[[#include <${GLUE_HEADER}>]]
	)
AC_CHECK_DECL([HA_CCMUSER],
	[],
	[AC_MSG_ERROR("Not found HA_CCMUSER in ${GLUE_HEADER}")],
	[[#include <${GLUE_HEADER}>]]
	)
AC_CHECK_DECL([CRM_DAEMON_DIR],
	[],
	[AC_MSG_ERROR("Not found CRM_DAEMON_DIR in ${CRM_CONFIG_HEADER}")],
	[[#include <${CRM_CONFIG_HEADER}>]]
	)

OCFDIR=`extract_header_define $GLUE_HEADER OCF_RA_DIR`
AC_DEFINE_UNQUOTED(OCFDIR,"$OCFDIR", ocf resource directory)
AC_SUBST(OCFDIR)

EXTDIR=`extract_header_define $GLUE_HEADER STONITH_EXT_PLUGINDIR`
AC_DEFINE_UNQUOTED(EXTDIR,"$EXTDIR", stonith external plugin directory)
AC_SUBST(EXTDIR)

CRM_DAEMON_DIR=`extract_header_define $CRM_CONFIG_HEADER CRM_DAEMON_DIR`
AC_DEFINE_UNQUOTED(CRM_DAEMON_DIR,"$CRM_DAEMON_DIR", crm daemon directory)
AC_SUBST(CRM_DAEMON_DIR)

CRM_DAEMON_GROUP=`extract_header_define $GLUE_HEADER HA_APIGROUP`
AC_DEFINE_UNQUOTED(CRM_DAEMON_GROUP,"$CRM_DAEMON_GROUP", Group to run Pacemaker daemons as)
AC_SUBST(CRM_DAEMON_GROUP)

CRM_DAEMON_USER=`extract_header_define $GLUE_HEADER HA_CCMUSER`
AC_DEFINE_UNQUOTED(CRM_DAEMON_USER,"$CRM_DAEMON_USER", User to run Pacemaker daemons as)
AC_SUBST(CRM_DAEMON_USER)

# Checks for library functions.

AC_CONFIG_FILES(Makefile	\
		resources/Makefile \
		tools/Makefile \
		pm_extras.spec
		)
AC_OUTPUT

dnl *****************
dnl Configure summary
dnl *****************

eval prefix="`eval echo ${prefix}`"
eval exec_prefix="`eval echo ${prefix}`"
eval bindir="`eval echo ${bindir}`"
eval sbindir="`eval echo ${sbindir}`"
eval libexecdir="`eval echo ${libexecdir}`"
eval datadir="`eval echo ${datadir}`"
eval sysconfdir="`eval echo ${sysconfdir}`"
eval sharedstatedir="`eval echo ${sharedstatedir}`"
eval localstatedir="`eval echo ${localstatedir}`"
eval libdir="`eval echo ${libdir}`"
eval includedir="`eval echo ${includedir}`"
eval oldincludedir="`eval echo ${oldincludedir}`"
eval infodir="`eval echo ${infodir}`"
eval mandir="`eval echo ${mandir}`"
eval docdir="`eval echo ${docdir}`"

AC_MSG_RESULT([])
AC_MSG_RESULT([$PACKAGE configuration:])
AC_MSG_RESULT([  Version                  = ${VERSION}])
AC_MSG_RESULT([])
AC_MSG_RESULT([  Prefix                   = ${prefix}])
AC_MSG_RESULT([  Executables              = ${sbindir}])
AC_MSG_RESULT([  Libraries                = ${libdir}])
AC_MSG_RESULT([  Header files             = ${includedir}])
AC_MSG_RESULT([  State information        = ${localstatedir}])
AC_MSG_RESULT([  System configuration     = ${sysconfdir}])
AC_MSG_RESULT([  Document directory       = ${docdir}])
AC_MSG_RESULT([  AIS Plugins              = ${LCRSODIR}])
AC_MSG_RESULT([])
AC_MSG_RESULT([  HA group name            = ${CRM_DAEMON_GROUP}])
AC_MSG_RESULT([  HA user name             = ${CRM_DAEMON_USER}])
AC_MSG_RESULT([])
AC_MSG_RESULT([  CFLAGS                   = ${CFLAGS}])
AC_MSG_RESULT([  CPPFLAGS                 = ${CPPFLAGS}])
AC_MSG_RESULT([  LIBS                     = ${LIBS}])
AC_MSG_RESULT([])
AC_MSG_RESULT([  Features                 = ${PACKAGE_FEATURES}])
