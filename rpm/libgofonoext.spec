Name: libgofonoext
Version: 1.0.11
Release: 0
Summary: Client library for Sailfish OS ofono extensions
Group: Development/Libraries
License: BSD
URL: https://git.sailfishos.org/mer-core/libgofonoext
Source: %{name}-%{version}.tar.bz2

%define libglibutil_version 1.0.5

BuildRequires: pkgconfig(glib-2.0)
BuildRequires: pkgconfig(libgofono)
BuildRequires:  pkgconfig(libglibutil) >= %{libglibutil_version}
Requires:   libglibutil >= %{libglibutil_version}
Requires(post): /sbin/ldconfig
Requires(postun): /sbin/ldconfig

%description
Provides glib-based API for Sailfish OS ofono extensions

%package devel
Summary: Development library for %{name}
Requires: %{name} = %{version}
Requires: pkgconfig

%description devel
This package contains the development library for %{name}.

%prep
%setup -q

%build
make LIBDIR=%{_libdir} KEEP_SYMBOLS=1 release pkgconfig

%install
rm -rf %{buildroot}
make LIBDIR=%{_libdir} DESTDIR=%{buildroot} install-dev

%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig

%files
%defattr(-,root,root,-)
%{_libdir}/%{name}.so.*

%files devel
%defattr(-,root,root,-)
%{_libdir}/pkgconfig/*.pc
%{_libdir}/%{name}.so
%{_includedir}/gofonoext/*.h
