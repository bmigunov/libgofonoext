Name: libgofonoext
Version: 1.0.3
Release: 0
Summary: Client library for nemomobile ofono extensions
Group: Development/Libraries
License: BSD
URL: https://git.merproject.org/mer-core/libgofonoext
Source: %{name}-%{version}.tar.bz2
BuildRequires: glib2-devel >= 2.0
BuildRequires: libglibutil-devel >= 1.0.3
BuildRequires: libgofono-devel
Requires(post): /sbin/ldconfig
Requires(postun): /sbin/ldconfig

%description
Provides glib-based API for nemomobile ofono extensions

%package devel
Summary: Development library for %{name}
Requires: %{name} = %{version}
Requires: pkgconfig

%description devel
This package contains the development library for %{name}.

%prep
%setup -q

%build
make KEEP_SYMBOLS=1 release pkgconfig

%install
rm -rf %{buildroot}
make install-dev DESTDIR=%{buildroot}

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
