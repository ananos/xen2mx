%define targetdir /opt
%define debug_package %{nil}

Summary: Open-MX: Myrinet Express over Generic Ethernet Hardware
Name: open-mx
Version: 1.2.0
Release: 0
License: GPL
Group: System Environment/Libraries
Packager: Ljl
Source: open-mx-%{version}.tar.gz
Provides: mx
BuildRoot: /var/tmp/%{name}-%{version}-build
BUildrequires: gcc

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

%clean
rm -rf $RPM_BUILD_ROOT

%post

%files
%defattr(-, root, root)
%{targetdir}
