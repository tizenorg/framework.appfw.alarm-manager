Name:       alarm-manager
Summary:    Alarm library
Version:    0.4.163
Release:    1
Group:      System/Libraries
License:    Apache License, Version 2.0
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
BuildRequires: python-xml

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
%endif
%ifarch %{ix86}
	ARCH=x86
%else
	ARCH=arm
%endif

cmake . -DCMAKE_INSTALL_PREFIX=%{_prefix} -DOBS=1 -DFULLVER=%{version} -DMAJORVER=${MAJORVER} -DARCH=${ARCH}

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

vconftool set -t int db/system/timechange 0 -g 5000 -s system::vconf_system
vconftool set -t double db/system/timechange_external 0 -g 5000 -s system::vconf_system
vconftool set -t int memory/system/timechanged 0 -i -g 5000 -s system::vconf_system

chmod 755 /usr/bin/alarm-server

%post -n libalarm
chmod 644 /usr/lib/libalarm.so.0.0.0


%files -n alarm-server
%manifest alarm-server.manifest
%{_bindir}/*
%{_libdir}/systemd/system/multi-user.target.wants/alarm-server.service
%{_libdir}/systemd/system/alarm-server.service
/usr/share/license/alarm-server
%attr(0755,root,root) /opt/etc/dump.d/module.d/alarmmgr_log_dump.sh

%files -n libalarm
%manifest alarm-lib.manifest
%{_libdir}/*.so.*
/usr/share/license/libalarm


%files -n libalarm-devel
%{_includedir}/*.h
%{_libdir}/pkgconfig/*.pc
%{_libdir}/*.so

