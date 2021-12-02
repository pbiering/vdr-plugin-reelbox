%global pname   reelbox
%global __provides_exclude_from ^%{vdr_plugindir}/.*\\.so.*$

%global fork_account pbiering
#global fork_branch  vdr-2.4.1

#global gitcommit e4d46cc6dda08ad2394b72f3d9b9083e5f7735e6
%global gitshortcommit %(c=%{gitcommit}; echo ${c:0:7})
%global gitdate 20211114

%define rel	1

Name:           vdr-%{pname}
Version:        3.3.0
%if 0%{?gitcommit:1}
Release:        %{rel}.git.%{gitshortcommit}.%{gitdate}%{?dist}
%else
%if 0%{?fork_account:1}
  %if 0%{?fork_branch:1}
Release:        %{rel}.fork.%{fork_account}.branch.%{fork_branch}%{?dist}
  %else
Release:        %{rel}.fork.%{fork_account}%{?dist}
  %endif
%else
Release:        %{rel}%{?dist}
%endif
%endif
Summary:        ReelBox eHD Frontend for VDR

License:        GPLv2

%if 0%{?fork_account:1}
URL:            https://github.com/%{fork_account}/vdr-plugin-reelbox
  %if 0%{?fork_branch:1}
Source0:        https://github.com/%{fork_account}/vdr-plugin-reelbox/archive/%{fork_branch}/%{name}.tar.gz
  %else
    %if 0%{?gitcommit:1}
Source0:        https://github.com/%{fork_account}/vdr-plugin-reelbox/archive/%{gitcommit}/%{name}-%{gitshortcommit}.tar.gz
    %else
Source0:        https://github.com/%{fork_account}/vdr-plugin-reelbox/archive/v%{version}/vdr-plugin-%{pname}-%{version}.tar.gz
    %endif
  %endif
%else
URL:            https://github.com/vdr-projects/vdr-plugin-reelbox
  %if 0%{?gitcommit:1}
Source0:        https://github.com/vdr-projects/vdr-plugin-reelbox/archive/v%{version}/vdr-plugin-%{pname}-%{version}.tar.gz
  %else
Source0:        https://github.com/vdr-projects/vdr-plugin-reelbox/archive/%{gitcommit}/%{name}-%{gitshortcommit}.tar.gz
  %endif
%endif

Source1:	https://github.com/pbiering/ReelBoxNG/archive/main/ReelBoxNG-head.tar.gz


%define		file_plugin_config		reelbox.conf

BuildRequires:  gcc-c++
BuildRequires:  vdr-devel >= 2.3.9
BuildRequires:  zlib-devel
BuildRequires:  libxml2-devel
BuildRequires:  freetype-devel
BuildRequires:  fontconfig-devel
BuildRequires:  ffmpeg-devel
BuildRequires:  liba52-devel
BuildRequires:  libmad-devel
BuildRequires:  alsa-lib-devel
Requires:       vdr(abi)%{?_isa} = %{vdr_apiversion}

%description
ReelBox eHD Frontend for VDR
%if 0%{?fork_account:1}
Fork: %{fork_account} / Branch: %{fork_branch}
%else
%if 0%{?gitcommit:1}
git-commit: %{gitshortcommit} from %{gitdate}
%endif
%endif


%prep
%if 0%{?fork_account:1}
  %if 0%{?fork_branch:1}
%setup -q -n vdr-plugin-%{pname}-%{fork_branch}
  %else
    %if 0%{?gitcommit:1}
%setup -q -n vdr-plugin-%{pname}-%{gitcommit}
    %else
%setup -q -n vdr-plugin-%{pname}-%{version}
    %endif
  %endif
%else
  %if 0%{?gitcommit:1}
%setup -q -n vdr-plugin-%{pname}-%{gitcommit}
  %else
%setup -q -n vdr-plugin-%{pname}-%{version}
  %endif
%endif

mkdir utils
tar xvzf /home/vdrdev/rpmbuild/SOURCES/ReelBoxNG-head.tar.gz ReelBoxNG-main/src-reelvdr/utils/bspshm --strip-components=2 -C utils/
tar xvzf /home/vdrdev/rpmbuild/SOURCES/ReelBoxNG-head.tar.gz ReelBoxNG-main/src-reelvdr/utils/hdshm3 --strip-components=2 -C utils/


%build
# enforce non-parallel build
%define _smp_mflags -j1

%make_build AUTOCONFIG=0 BSPSHM=./utils/bspshm HDSHM=./utils/hdshm3/src


%install
%make_install


# plugin config file
install -Dpm 644 contrib/%{file_plugin_config} $RPM_BUILD_ROOT%{_sysconfdir}/sysconfig/vdr-plugins.d/reelbox.conf


%find_lang %{name} --all-name --with-man


%files -f %{name}.lang
%license COPYING
%doc HISTORY README
%config(noreplace) %{_sysconfdir}/sysconfig/vdr-plugins.d/*.conf
%{vdr_plugindir}/libvdr-*.so.%{vdr_apiversion}


%post


%changelog
* Thu Dec 02 2021 Peter Bieringer <pb@bieringer.de> - 3.3.0
- Update to 3.3.0
- Bugfixes

* Sun Nov 14 2021 Peter Bieringer <pb@bieringer.de> - 3.2.0
- First build
