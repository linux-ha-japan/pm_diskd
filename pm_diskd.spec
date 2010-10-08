########################################
# Derived definitions
########################################
%define name pm_diskd 
%define version 1.0
%define release 1.el5
%define prefix /usr
%define libdir  %{_libdir}
#
%define __check_files %{nil}
#
#
#
Summary: Pacemaker Diskcheck Module 
Name: %{name}
Version: %{version}
Release: %{release}
Group: Applications
Source: %{name}.tar.bz2
License: GPL/LGPL
Vendor: NIPPON TELEGRAPH AND TELEPHONE CORPORATION
BuildRoot: %{_tmppath}/%{name}-%{version}
BuildRequires: make 
Requires: pacemaker >= 1.0.9

########################################
%description
########################################
Pacemaker Diskcheck Module

########################################
%prep
########################################
rm -rf $RPM_BUILD_ROOT
%setup -q -n %{name}
pushd $RPM_BUILD_DIR/%{name}
./autogen.sh
./configure
popd

########################################
%build
########################################
pushd $RPM_BUILD_DIR/%{name}/lib
make DESTDIR=$RPM_BUILD_ROOT
popd
pushd $RPM_BUILD_DIR/%{name}/pengine
make DESTDIR=$RPM_BUILD_ROOT
popd
pushd $RPM_BUILD_DIR/%{name}/tools
make DESTDIR=$RPM_BUILD_ROOT
popd

########################################
%install
########################################
pushd $RPM_BUILD_DIR/%{name}/tools
make DESTDIR=$RPM_BUILD_ROOT install
popd
pushd $RPM_BUILD_DIR/%{name}/extra/resources
make DESTDIR=$RPM_BUILD_ROOT install
popd

########################################
%clean
########################################
if
	[ -n "${RPM_BUILD_ROOT}"  -a "${RPM_BUILD_ROOT}" != "/" ]
then
	rm -rf $RPM_BUILD_ROOT
fi
rm -rf $RPM_BUILD_DIR/%{name}

########################################
%post
########################################
true
########################################
%preun
########################################
true
########################################
%postun
########################################
true

########################################
%files
########################################
%defattr(755,root,root)

%{_libdir}/heartbeat/diskd
/usr/lib/ocf/resource.d/pacemaker/diskd

