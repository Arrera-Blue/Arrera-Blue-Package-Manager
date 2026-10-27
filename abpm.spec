Name:           abpm
Version:        2026.1.0
Release:        1%{?dist}
Summary:        Arrera Blue Package Manager - Unified package manager for Arrera Blue OS
Summary(fr):    Gestionnaire de paquets unifié pour Arrera Blue OS

License:        GPL-3.0-or-later
URL:            https://github.com/Arrera-Blue/Arrera-Blue-Package-Manager
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc-c++ >= 13
BuildRequires:  cmake >= 3.20
BuildRequires:  pkgconfig
BuildRequires:  pkgconfig(libdnf5)
BuildRequires:  pkgconfig(flatpak)
BuildRequires:  pkgconfig(glib-2.0)
BuildRequires:  pkgconfig(gio-2.0)
BuildRequires:  pkgconfig(libarchive)

Requires:       libdnf5
Requires:       flatpak
Requires:       glib2
Requires:       libarchive
Requires:       fuse-libs

# Recommends pour l'intégration complète sous Arrera Blue OS
Recommends:     polkit

%description
Arrera Blue Package Manager (abpm) is a modern, unified package management CLI
written in C++20 for Arrera Blue OS (Fedora-based). It combines system packages
(DNF5/RPM via libdnf5), sandboxed desktop applications (Flatpak via libflatpak),
standalone AppImages, and compressed archive distributions (tar, zip via libarchive).

%description -l fr
Arrera Blue Package Manager (abpm) est un gestionnaire de paquets unifié moderne
développé en C++20 pour Arrera Blue OS (base Fedora). Il unifie les paquets
système (DNF5/RPM via libdnf5), les applications Flatpak (Flathub via libflatpak),
les applications autonomes AppImage et les archives compressées (tar, zip via libarchive).

%prep
%autosetup

%build
%cmake -DCMAKE_BUILD_TYPE=Release
%cmake_build

%install
%cmake_install

# Création des répertoires de stockage système pour les AppImages et Archives
mkdir -p %{buildroot}%{_localstatedir}/lib/abpm
mkdir -p %{buildroot}/opt/applications/.icons

# Installation des wrappers de redirection optionnels pour dnf et flatpak
mkdir -p %{buildroot}%{_sysconfdir}/profile.d
cat << 'EOF' > %{buildroot}%{_sysconfdir}/profile.d/abpm-aliases.sh
# Redirection informative sous Arrera Blue OS
# Pour exécuter les commandes d'origine en cas de besoin : /usr/bin/dnf et /usr/bin/flatpak
alias dnf='echo -e "\033[1;33m[Arrera Blue OS]\033[0m DNF est géré par abpm. Utilisez : \033[1;32mabpm\033[0m (ou /usr/bin/dnf pour forcer)"; false'
alias flatpak='echo -e "\033[1;33m[Arrera Blue OS]\033[0m Flatpak est géré par abpm. Utilisez : \033[1;32mabpm\033[0m (ou /usr/bin/flatpak pour forcer)"; false'
EOF

%files
%doc README.md documentation.md
%{_bindir}/abpm
%config(noreplace) %{_sysconfdir}/profile.d/abpm-aliases.sh
%dir %{_localstatedir}/lib/abpm
%dir /opt/applications
%dir /opt/applications/.icons

%changelog
* Tue Oct 27 2026 Baptiste Pauchet <baptiste.pauchet13@gmail.com> - 2026.1.0-1
- Version officielle 2026.1.0 pour Arrera Blue OS.
- Support natif complet DNF5, Flatpak, AppImage et Archives (tar.gz, tar.xz, zip).
- Extraction automatique des icônes et gestion centralisée dans .icons/.
- Sélection interactive des icônes candidates et nouvelle commande set-icon.
- Sécurisation de l'environnement avec redirection dnf/flatpak.
