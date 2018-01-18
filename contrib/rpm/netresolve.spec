Name:           netresolve
Version:        0.0.1
Release:        0
Summary:        Non-blocking network name resolution library and tools
License:        BSD
Group:          Development/Libraries/C
Url:            https://github.com/crossdistro/netresolve
Source:         netresolve-%{version}.tar.xz
BuildRoot:      %{_tmppath}/%{name}-%{version}-build

%description
Netresolve is a package for nonblocking network name resolution via
backends intended as a replacement for name service switch based name
resolution in glibc.

%prep
%setup -q

%build
%configure
make %{?_smp_mflags}

%install
%make_install

%post
%postun

%files
%defattr(-,root,root)
#%doc ChangeLog README COPYING
/usr/bin/getaddrinfo
/usr/bin/gethostbyaddr
/usr/bin/gethostbyname
/usr/bin/getnameinfo
/usr/bin/netresolve
/usr/bin/res_query
/usr/bin/wrapresolve
/usr/include/netresolve-epoll.h
/usr/include/netresolve-event.h
/usr/include/netresolve-glib.h
/usr/include/netresolve-nonblock.h
/usr/include/netresolve-select.h
/usr/include/netresolve.h
/usr/lib64/libnetresolve-backend-any.la
/usr/lib64/libnetresolve-backend-any.so
/usr/lib64/libnetresolve-backend-any.so.0
/usr/lib64/libnetresolve-backend-any.so.0.0.0
/usr/lib64/libnetresolve-backend-exec.la
/usr/lib64/libnetresolve-backend-exec.so
/usr/lib64/libnetresolve-backend-exec.so.0
/usr/lib64/libnetresolve-backend-exec.so.0.0.0
/usr/lib64/libnetresolve-backend-hostname.la
/usr/lib64/libnetresolve-backend-hostname.so
/usr/lib64/libnetresolve-backend-hostname.so.0
/usr/lib64/libnetresolve-backend-hostname.so.0.0.0
/usr/lib64/libnetresolve-backend-hosts.la
/usr/lib64/libnetresolve-backend-hosts.so
/usr/lib64/libnetresolve-backend-hosts.so.0
/usr/lib64/libnetresolve-backend-hosts.so.0.0.0
/usr/lib64/libnetresolve-backend-libc.la
/usr/lib64/libnetresolve-backend-libc.so
/usr/lib64/libnetresolve-backend-libc.so.0
/usr/lib64/libnetresolve-backend-libc.so.0.0.0
/usr/lib64/libnetresolve-backend-loopback.la
/usr/lib64/libnetresolve-backend-loopback.so
/usr/lib64/libnetresolve-backend-loopback.so.0
/usr/lib64/libnetresolve-backend-loopback.so.0.0.0
/usr/lib64/libnetresolve-backend-nss.la
/usr/lib64/libnetresolve-backend-nss.so
/usr/lib64/libnetresolve-backend-nss.so.0
/usr/lib64/libnetresolve-backend-nss.so.0.0.0
/usr/lib64/libnetresolve-backend-numerichost.la
/usr/lib64/libnetresolve-backend-numerichost.so
/usr/lib64/libnetresolve-backend-numerichost.so.0
/usr/lib64/libnetresolve-backend-numerichost.so.0.0.0
/usr/lib64/libnetresolve-backend-unix.la
/usr/lib64/libnetresolve-backend-unix.so
/usr/lib64/libnetresolve-backend-unix.so.0
/usr/lib64/libnetresolve-backend-unix.so.0.0.0
/usr/lib64/libnetresolve-libc.la
/usr/lib64/libnetresolve-libc.so
/usr/lib64/libnetresolve-libc.so.0
/usr/lib64/libnetresolve-libc.so.0.0.0
/usr/lib64/libnetresolve.la
/usr/lib64/libnetresolve.so
/usr/lib64/libnetresolve.so.0
/usr/lib64/libnetresolve.so.0.0.0
/usr/lib64/libnss_netresolve.la
/usr/lib64/libnss_netresolve.so
/usr/lib64/libnss_netresolve.so.2
/usr/lib64/libnss_netresolve.so.2.0.0
%changelog
