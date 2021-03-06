testexecdir = join_paths(installed_test_bindir, 'a11y')
testdatadir = join_paths(installed_test_datadir, 'a11y')

a11y_tests = [
  'accessibility-dump',
  'tree-performance',
  'text',
  'children',
  'derive',
  'value',
  'misc',
  'tree-relationships',
  'util',
]

foreach t: a11y_tests
  test_prefix = 'a11y'

  test (t,
        executable (t, '@0@.c'.format(t),
                    install: get_option('install-tests'),
                    install_dir: testexecdir,
                    dependencies: libgtk_dep),
        args: [ '--tap', '-k', ],
        env: [ 'GIO_USE_VOLUME_MONITOR=unix',
               'GSETTINGS_BACKEND=memory',
               'GTK_CSD=1',
               'G_ENABLE_DIAGNOSTIC=0',
               'G_TEST_SRCDIR=@0@'.format(meson.current_source_dir()),
               'G_TEST_BUILDDIR=@0@'.format(meson.current_build_dir())
             ],
        suite: 'a11y')
endforeach

a11y_installed_tests = [
  'tests.test',
  'children.test',
  'tree.test',
  'value.test',
  'derive.test',
  'text.test',
  'util.test',
  'misc.test',
]

installed_test_data = [
  'hello-world.ui',
  'hello-world.txt',
  'mnemonic.ui',
  'mnemonic.txt',
  'accessible-name.ui',
  'accessible-name.txt',
  'notebook.ui',
  'notebook.txt',
  'range.ui',
  'range.txt',
  'link.ui',
  'link.txt',
  'text.ui',
  'text.txt',
  'buttons.ui',
  'buttons.txt',
  'colorchooser.ui',
  'colorchooser.txt',
  'about.ui',
  'about.txt',
  'messagedialog.ui',
  'messagedialog.txt',
  'expander.ui',
  'expander.txt',
  'assistant.ui',
  'assistant.txt',
  'pickers.ui',
  'pickers.txt',
  'label.ui',
  'label.txt',
  'lockbutton.ui',
  'lockbutton.txt',
  'spinner.ui',
  'spinner.txt',
  'progress.ui',
  'progress.txt',
  'infobar.ui',
  'infobar.txt',
  'calendar.ui',
  'calendar.txt',
  'statusbar.ui',
  'statusbar.txt',
  'paned.ui',
  'paned.txt',
  'iconview.ui',
  'iconview.txt',
  'entries.ui',
  'entries.txt',
  'scale-drawvalue.ui',
  'scale-drawvalue.txt',
  'placeholder-text.ui',
  'placeholder-text.txt',
  'menu.ui',
  'menu.txt',
  'menubutton.ui',
  'menubutton.txt',
  'menubutton2.ui',
  'menubutton2.txt',
  'menubutton3.ui',
  'menubutton3.txt',
  'combos.ui',
  'combos.txt',
  'listbox.ui',
  'listbox.txt',
  'stack.ui',
  'stack.txt',
  'headerbar.ui',
  'headerbar.txt',
  'tree.ui',
  'tree.txt',
  'actionbar.ui',
  'actionbar.txt',
  'tooltips.ui',
  'tooltips.txt',
]

if get_option('install-tests')
  test_cdata = configuration_data()
  test_cdata.set('libexecdir', gtk_libexecdir)

  foreach t: a11y_installed_tests
    configure_file(input: '@0@.in'.format(t),
                   output: t,
                   configuration: test_cdata,
                   install: true,
                   install_dir: testdatadir)
  endforeach

  install_data(installed_test_data, install_dir: testexecdir)
endif
