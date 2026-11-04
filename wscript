#! /usr/bin/python3
# encoding: utf-8
from __future__ import print_function

import os

from waflib import Options

# see also common/JackConstants.h
VERSION = '1.9.23'
APPNAME = 'jack'
JACK_API_VERSION = '0.1.0'

# these variables are mandatory ('/' are converted automatically)
top = '.'
out = 'build'


def options(opt):
    # options provided by the modules
    opt.load('compiler_cxx')
    opt.load('compiler_c')

    # install directories
    opt.add_option('--libdir', type='string', help='Library directory [Default: <prefix>/lib]')
    opt.add_option('--pkgconfigdir', type='string', help='pkg-config file directory [Default: <libdir>/pkgconfig]')

    # options affecting binaries
    opt.add_option('--debug', action='store_true', default=False, dest='debug', help='Build debuggable binaries')

    # options affecting general jack functionality
    opt.add_option('--clients', default=256, type='int', dest='clients', help='Maximum number of JACK clients')
    opt.add_option(
        '--ports-per-application',
        default=2048,
        type='int',
        dest='application_ports',
        help='Maximum number of ports per application',
    )
    opt.add_option(
        '--tests',
        action='store_true',
        default=False,
        dest='tests',
        help='Build tests',
    )


def configure(conf):
    conf.load('compiler_cxx')
    conf.load('compiler_c')

    conf.env.append_unique('CFLAGS', '-Wall')
    conf.env.append_unique('CXXFLAGS', ['-Wall', '-Wno-invalid-offsetof'])
    conf.env.append_unique('CXXFLAGS', '-std=gnu++11')
    conf.env.append_unique('LDFLAGS', '-Wl,--no-undefined')

    # ALSA is the only supported hardware backend.
    conf.check_cfg(
        package='alsa >= 1.0.18',
        args='--cflags --libs',
        uselib_store='ALSA',
        mandatory=True)

    conf.recurse('common')

    conf.env['LIB_PTHREAD'] = ['pthread']
    conf.env['LIB_DL'] = ['dl']
    conf.env['LIB_RT'] = ['rt']
    conf.env['LIB_M'] = ['m']
    conf.env['LIB_STDC++'] = ['stdc++']
    conf.env['JACK_API_VERSION'] = JACK_API_VERSION
    conf.env['JACK_VERSION'] = VERSION

    conf.env['BUILD_DEBUG'] = Options.options.debug
    conf.env['BUILD_TESTS'] = Options.options.tests
    conf.env['BUILD_JACKD'] = True

    conf.env['BINDIR'] = conf.env['PREFIX'] + '/bin'

    if Options.options.libdir:
        conf.env['LIBDIR'] = Options.options.libdir
    else:
        conf.env['LIBDIR'] = conf.env['PREFIX'] + '/lib'

    if Options.options.pkgconfigdir:
        conf.env['PKGCONFDIR'] = Options.options.pkgconfigdir
    else:
        conf.env['PKGCONFDIR'] = conf.env['LIBDIR'] + '/pkgconfig'

    if conf.env['BUILD_DEBUG']:
        conf.env.append_unique('CXXFLAGS', '-g')
        conf.env.append_unique('CFLAGS', '-g')
        conf.env.append_unique('LINKFLAGS', '-g')

    conf.define('CLIENT_NUM', Options.options.clients)
    conf.define('PORT_NUM_FOR_CLIENT', Options.options.application_ports)

    conf.env['ADDON_DIR'] = os.path.normpath(os.path.join(conf.env['LIBDIR'], 'jack'))
    conf.define('ADDON_DIR', conf.env['ADDON_DIR'])
    conf.define('JACK_LOCATION', os.path.normpath(os.path.join(conf.env['PREFIX'], 'bin')))
    conf.define('JACKMP', 1)
    conf.define('JACK_VERSION', VERSION)
    conf.write_config_header('config.h', remove=False)

    print()
    print('JACK ' + VERSION)
    print('Maximum JACK clients                        : %d' % Options.options.clients)
    print('Maximum ports per application               : %d' % Options.options.application_ports)
    print('Install prefix                              : %s' % conf.env['PREFIX'])
    print('Library directory                           : %s' % conf.env['LIBDIR'])
    print('Drivers directory                           : %s' % conf.env['ADDON_DIR'])
    print('Build debuggable binaries                   : %s' % ('yes' if conf.env['BUILD_DEBUG'] else 'no'))
    print('Build ALSA driver                           : yes')
    print('Build tests                                 : %s' % ('yes' if conf.env['BUILD_TESTS'] else 'no'))
    print()


def obj_add_includes(bld, obj):
    obj.includes += ['linux', 'posix']


def build_jackd(bld):
    jackd = bld(
        features=['cxx', 'cxxprogram'],
        defines=['HAVE_CONFIG_H', 'SERVER_SIDE'],
        includes=['.', 'common', 'common/jack'],
        target='jackd',
        source=['common/Jackdmp.cpp'],
        use=['serverlib', 'DL', 'M', 'PTHREAD', 'RT', 'STDC++']
    )
    obj_add_includes(bld, jackd)
    return jackd


def create_driver_obj(bld, **kw):
    if 'use' in kw:
        kw['use'] += ['serverlib']
    else:
        kw['use'] = ['serverlib']

    driver = bld(
        features=['c', 'cxx', 'cshlib', 'cxxshlib'],
        defines=['HAVE_CONFIG_H', 'SERVER_SIDE'],
        includes=['.', 'common', 'common/jack'],
        install_path='${ADDON_DIR}/',
        **kw)
    driver.env['cxxshlib_PATTERN'] = 'jack_%s.so'
    obj_add_includes(bld, driver)
    return driver


def build_drivers(bld):
    create_driver_obj(
        bld,
        target='dummy',
        source=['common/JackDummyDriver.cpp'])

    create_driver_obj(
        bld,
        target='loopback',
        source=['common/JackLoopbackDriver.cpp'])

    create_driver_obj(
        bld,
        target='proxy',
        source=['common/JackProxyDriver.cpp'])

    alsa_src = [
        'common/memops.c',
        'linux/alsa/JackAlsaDriver.cpp',
        'linux/alsa/alsa_rawmidi.c',
        'linux/alsa/alsa_seqmidi.c',
        'linux/alsa/alsa_midi_jackmp.cpp',
        'linux/alsa/generic_hw.c',
        'linux/alsa/hdsp.c',
        'linux/alsa/alsa_driver.c',
        'linux/alsa/hammerfall.c',
        'linux/alsa/ice1712.c'
    ]

    alsarawmidi_src = [
        'linux/alsarawmidi/JackALSARawMidiDriver.cpp',
        'linux/alsarawmidi/JackALSARawMidiInputPort.cpp',
        'linux/alsarawmidi/JackALSARawMidiOutputPort.cpp',
        'linux/alsarawmidi/JackALSARawMidiPort.cpp',
        'linux/alsarawmidi/JackALSARawMidiReceiveQueue.cpp',
        'linux/alsarawmidi/JackALSARawMidiSendQueue.cpp',
        'linux/alsarawmidi/JackALSARawMidiUtil.cpp'
    ]

    create_driver_obj(
        bld,
        target='alsa',
        source=alsa_src,
        use=['ALSA'])

    create_driver_obj(
        bld,
        target='alsarawmidi',
        source=alsarawmidi_src,
        use=['ALSA'])


def build(bld):
    # process subfolders from here
    bld.recurse('common')

    if bld.env['BUILD_JACKD']:
        build_jackd(bld)

    build_drivers(bld)

    if bld.env['BUILD_TESTS']:
        bld.recurse('tests')
