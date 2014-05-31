Name:       alarm-manager
Summary:    Alarm library
Version:    0.4.112
Release:    1
Group:      System/Libraries
License:    Apache License, Version 2.0
Source0:    %{name}-%{version}.tar.gz
Source1:    alarm-server.service
Requires(post): /sbin/ldconfig
Requires(postun): /sbin/ldconfig

BuildRequires: pkgconfig(dbus-1)
BuildRequires: pkgconfig(glib-2.0)
BuildRequires: pkgconfig(dbus-glib-1)
%if %{_repository} == "wearable"
BuildRequires: pkgconfig(deviced)
%endif
BuildRequires: pkgconfig(dlog)
BuildRequires: pkgconfig(aul)
BuildRequires: pkgconfig(bundle)
BuildRequires: pkgconfig(security-server)
BuildRequires: pkgconfig(db-util)
BuildRequires: pkgconfig(vconf)
BuildRequires: pkgconfig(appsvc)
BuildRequires: pkgconfig(pkgmgr-info)

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

# HACK_removed_dbus_glib_alarm_manager_object_info.diff
#%patch0 -p1

%build
%if 0%{?sec_build_binary_debug_enable}
export CFLAGS="$CFLAGS -DTIZEN_ENGINEER_MODE"
export CXXFLAGS="$CXXFLAGS -DTIZEN_ENGINEER_MODE"
export FFLAGS="$FFLAGS -DTIZEN_ENGINEER_MODE"
%endif

%if %{_repository} == "wearable"
export CFLAGS="$CFLAGS -DWEARABLE_PROFILE"
export DEVICE_PROFILE="wearable"
%else
export CFLAGS="$CFLAGS -DMOBILE_PROFILE"
export DEVICE_PROFILE="mobile"
%endif

export LDFLAGS+=" -Wl,--rpath=%{_libdir} -Wl,--as-needed"

%autogen --disable-static

dbus-binding-tool --mode=glib-server --prefix=alarm_manager ./alarm_mgr.xml > ./include/alarm-skeleton.h
dbus-binding-tool --mode=glib-client --prefix=alarm_manager ./alarm_mgr.xml > ./include/alarm-stub.h
dbus-binding-tool --mode=glib-server --prefix=alarm_client ./alarm-expire.xml > ./include/alarm-expire-skeleton.h
dbus-binding-tool --mode=glib-client --prefix=alarm_client ./alarm-expire.xml > ./include/alarm-expire-stub.h

%configure --disable-static
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

%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig

%post -n alarm-server

%if %{_repository} == "wearable"
vconftool set -t int db/system/timechange 0 -g 5000 -s system::vconf_system
vconftool set -t double db/system/timechange_external 0 -g 5000 -s system::vconf_system
vconftool set -t int memory/system/timechanged 0 -i -g 5000 -s system::vconf_system
%else
vconftool set -t int db/system/timechange 0 -g 5000 -s system::vconf
vconftool set -t double db/system/timechange_external 0 -g 5000 -s system::vconf
vconftool set -t int memory/system/timechanged 0 -i -g 5000 -s system::vconf
%endif

chmod 755 /usr/bin/alarm-server

%post -n libalarm
chmod 644 /usr/lib/libalarm.so.0.0.0


%files -n alarm-server
%if %{_repository} == "wearable"
%manifest alarm-server-wearable.manifest
%else
%manifest alarm-server-mobile.manifest
%endif
%{_bindir}/*
%{_libdir}/systemd/system/multi-user.target.wants/alarm-server.service
%{_libdir}/systemd/system/alarm-server.service
/usr/share/license/alarm-server

%files -n libalarm
%manifest alarm-lib.manifest
%{_libdir}/*.so.*
/usr/share/license/libalarm


%files -n libalarm-devel
%{_includedir}/*.h
%{_libdir}/pkgconfig/*.pc
%{_libdir}/*.so

