Name:           vt420
Version:        %{pkgver}
Release:        1
Summary:        VT420 terminal emulator running the original DEC firmware
License:        AGPL-3.0-only
URL:            https://github.com/tenox7/vt420
%global _build_id_links none
%global debug_package %{nil}

Requires:       SDL2

%description
Emulates the real VT420 hardware: 8051 CPU, DC7166 video processor,
SCN2681 DUART and the LK201 keyboard, booting the factory ROM through
self-test into the real firmware. SDL2 graphical display, ANSI TUI,
headless and MCP server modes. Run with no arguments for a login shell
on the emulated terminal.

%install
mkdir -p %{buildroot}%{_bindir} \
         %{buildroot}%{_datadir}/applications \
         %{buildroot}%{_datadir}/icons/hicolor/256x256/apps \
         %{buildroot}%{_docdir}/vt420
install -m 755 %{srcroot}/vt420 %{buildroot}%{_bindir}/vt420
install -m 644 %{srcroot}/packaging/shared/vt420.desktop %{buildroot}%{_datadir}/applications/
install -m 644 %{srcroot}/packaging/shared/vt420.png %{buildroot}%{_datadir}/icons/hicolor/256x256/apps/
install -m 644 %{srcroot}/README.md %{srcroot}/LICENSE.md %{buildroot}%{_docdir}/vt420/

%files
%{_bindir}/vt420
%{_datadir}/applications/vt420.desktop
%{_datadir}/icons/hicolor/256x256/apps/vt420.png
%doc %{_docdir}/vt420
