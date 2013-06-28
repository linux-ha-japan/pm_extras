MAINTAINERCLEANFILES = Makefile.in

sbin_PROGRAMS		= ifcheckd

# BUILD

ifcheckd_SOURCES	= ifcheckd.c

if SUPPORT_UPSTART
upstartdir		= /etc/init
upstart_DATA		= ifcheckd.conf
endif

if SUPPORT_SYSTEMD
systemddir		= /usr/lib/systemd/system
systemd_DATA		= ifcheckd.service
endif
