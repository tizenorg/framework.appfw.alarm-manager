Name:       alarm-manager
Summary:    Alarm library
Version:    0.4.179
Release:    1
Group:      System/Libraries
License:    Apache-2.0
Source0:    %{name}-%{version}.tar.gz
Source1:    alarm-server.service
Requires(post): /sbin/ldconfig
Requires(postun): /sbin/ldconfig

BuildRequires: cmake
BuildRequires: pkgconfig(glib-2.0)
BuildRequires: pkgconfig(dlog)
BuildRequires: pkgconfig(aul)
BuildRequires: pkgconfig(bundle)
BuildRequires: pkgconfig(sqlite3)
BuildRequires: pkgconfig(security-server)
BuildRequires: pkgconfig(db-util)
BuildRequires: pkgconfig(vconf)
BuildRequires: pkgconfig(appsvc)
BuildRequires: pkgconfig(pkgmgr-info)
BuildRequires: pkgconfig(gio-2.0)
BuildRequires: pkgconfig(gio-unix-2.0)
BuildRequires: pkgconfig(capi-system-device)
BuildRequires: pkgconfig(vasum)
BuildRequires: pkgconfig(eventsystem)

%description
Alarm Server and devel libraries


%package -n alarm-server
Summary:    Alarm server (devel)
Group:      Development/Libraries


%description -n alarm-server
Alarm Server


%package -n libalarm
Summary:    Alarm server libraries
Group:      Development/Libraries
Requires:   alarm-server = %{version}-%{release}


%description -n libalarm
Alarm server library


%package -n libalarm-devel
Summary:    Alarm server libraries(devel)
Group:      Development/Libraries
Requires:   libalarm = %{version}-%{release}


%description -n libalarm-devel
Alarm server library (devel)


%prep
%setup -q


%build
MAJORVER=`echo %{version} | awk 'BEGIN {FS="."}{print $1}'`

%if 0%{?sec_build_binary_debug_enable}
export CFLAGS="$CFLAGS -DTIZEN_DEBUG_ENABLE"
export CXXFLAGS="$CXXFLAGS -DTIZEN_DEBUG_ENABLE"
export FFLAGS="$FFLAGS -DTIZEN_DEBUG_ENABLE"
%define appfw_feature_alarm_manager_module_log 1
%endif
%ifarch %{ix86}
	ARCH=x86
%else
	ARCH=arm
%endif
%if 0%{?appfw_feature_alarm_manager_module_log}
	%define module_log_path /var/log/alarmmgr.log
	_APPFW_FEATURE_ALARM_MANAGER_MODULE_LOG=ON
%endif
cmake . -DCMAKE_INSTALL_PREFIX=%{_prefix} -DOBS=1 -DFULLVER=%{version} -DMAJORVER=${MAJORVER} -DARCH=${ARCH} -D_APPFW_FEATURE_ALARM_MANAGER_MODULE_LOG:BOOL=${_APPFW_FEATURE_ALARM_MANAGER_MODULE_LOG} -D_APPFW_ALARM_MANAGER_MODULE_LOG_PATH=%{module_log_path}

make %{?jobs:-j%jobs}


%install
rm -rf %{buildroot}
%make_install

mkdir -p %{buildroot}%{_libdir}/systemd/system/multi-user.target.wants
install -m 0644 %SOURCE1 %{buildroot}%{_libdir}/systemd/system/alarm-server.service
ln -s ../alarm-server.service %{buildroot}%{_libdir}/systemd/system/multi-user.target.wants/alarm-server.service

mkdir -p %{buildroot}/usr/share/license
cp LICENSE %{buildroot}/usr/share/license/alarm-server
cp LICENSE %{buildroot}/usr/share/license/libalarm

%if 0%{?appfw_feature_alarm_manager_module_log}
	mkdir -p %{buildroot}/`dirname %{module_log_path}`
	touch %{buildroot}/%{module_log_path}
%endif

%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig

%post -n alarm-server

chown system:system /opt/dbspace/.alarmmgr.db
chown system:system /opt/dbspace/.alarmmgr.db-journal
chown system:system /var/log/alarmmgr.log

chmod 755 /usr/bin/alarm-server
/usr/sbin/setcap CAP_DAC_OVERRIDE+eip /usr/bin/alarm-server

%post -n libalarm
chmod 644 /usr/lib/libalarm.so.0.0.0


%files -n alarm-server
%manifest alarm-server.manifest
%{_bindir}/*
%{_libdir}/systemd/system/multi-user.target.wants/alarm-server.service
%{_libdir}/systemd/system/alarm-server.service
/usr/share/license/alarm-server
%if 0%{?appfw_feature_alarm_manager_module_log}
%attr(0755,system,system) /opt/etc/dump.d/module.d/alarmmgr_log_dump.sh
%attr(0644,system,system) %{module_log_path}
%endif

%files -n libalarm
%manifest alarm-lib.manifest
%{_libdir}/*.so.*
/usr/share/license/libalarm


%files -n libalarm-devel
%{_includedir}/*.h
%{_libdir}/pkgconfig/*.pc
%{_libdir}/*.so
