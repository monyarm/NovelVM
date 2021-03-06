#------------------------------------------------------------------------------
#   novelvm-tools.spec
#       This SPEC file controls the building of NovelVM Tools RPM packages.
#------------------------------------------------------------------------------

#------------------------------------------------------------------------------
#   Prologue information
#------------------------------------------------------------------------------
Name		: novelvm-tools
Version		: 2.3.0git
Release		: 1
Summary		: NovelVM-related tools
Group		: Interpreters
License		: GPL

Url             : https://www.novelvm.org

Source		: %{name}-%{version}.tar.xz
BuildRoot	: %{_tmppath}/%{name}-%{version}-root

BuildRequires: zlib-devel
BuildRequires: wxGTK3-devel
BuildRequires: libmad-devel
BuildRequires: libvorbis-devel
BuildRequires: libogg-devel
BuildRequires: libpng-devel
BuildRequires: boost-devel
BuildRequires: flac-devel
BuildRequires: freetype-devel

#------------------------------------------------------------------------------
#   Description
#------------------------------------------------------------------------------
%description
Tools for compressing NovelVM datafiles and other related tools.

#------------------------------------------------------------------------------
#   install scripts
#------------------------------------------------------------------------------
%prep
%setup -q -n novelvm-tools-%{version}

%build
./configure --prefix=%{_prefix}
make %{_smp_mflags}

%install
make DESTDIR=%{buildroot} install
rm %{buildroot}%{_datadir}/novelvm-tools/detaillogo.jpg
rm %{buildroot}%{_datadir}/novelvm-tools/logo.jpg
rm %{buildroot}%{_datadir}/novelvm-tools/novelvmtools.icns
rm %{buildroot}%{_datadir}/novelvm-tools/novelvmtools.ico
rm %{buildroot}%{_datadir}/novelvm-tools/novelvmtools_128.png
rm %{buildroot}%{_datadir}/novelvm-tools/tile.gif

%clean
rm -Rf ${RPM_BUILD_ROOT}

#------------------------------------------------------------------------------
#   Files listing.
#------------------------------------------------------------------------------
%files
%doc README COPYING
%attr(0755,root,root)%{_bindir}/*

#------------------------------------------------------------------------------
#   Change Log
#------------------------------------------------------------------------------
%changelog
* Thu Nov 23 2017 (2.0.0)
  - remove own libmad since this is now in Fedora itself
* Sat Apr 03 2010 (1.2.0)
  - include libmad
* Sat Mar 26 2005 (0.7.1)
  - first tools package
