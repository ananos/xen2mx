%define debug_package %{nil}

Summary: Open-MX: Myrinet Express over Generic Ethernet Hardware
Name: open-mx
Version: 1.2.1
Release: 0
License: GPL
Group: System Environment/Libraries
Packager: Brice Goglin
Source: open-mx-%{version}.tar.gz
Provides: mx
BuildRoot: /var/tmp/%{name}-%{version}-build
BuildRequires: gcc

%description
Open-MX is a high-performance implementation of the Myrinet Express message-passing stack over generic Ethernet networks. It provides application-level and wire-protocol compatibility with the native MXoE (Myrinet Express over Ethernet) stack.
See http://open-mx.org/ for details.

%prep
%setup -n open-mx-%{version}

%build
./configure --prefix=/opt/open-mx-%{version}
make

%install
make DESTDIR=$RPM_BUILD_ROOT install
mkdir -p $RPM_BUILD_ROOT/etc/udev/rules.d
mkdir -p $RPM_BUILD_ROOT/etc/init.d
DESTDIR=$RPM_BUILD_ROOT $RPM_BUILD_ROOT/opt/open-mx-%{version}/sbin/omx_local_install

%clean
rm -rf $RPM_BUILD_ROOT

%post

%files
%defattr(-, root, root)
/opt
/etc/init.d/open-mx

%config(noreplace)
/etc/open-mx/open-mx.conf
/etc/udev/rules.d/10-open-mx.rules
