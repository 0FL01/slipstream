# Set up a C and C++ project with useful defaults for debugging
project(
  'slipstream',
  ['c', 'cpp'],
  version: '0.1.0',
  license: 'Apache-2.0',
  meson_version: '>=1.0.0',
  default_options: [
    'buildtype=debugoptimized',
    'cpp_std=gnu++11',
    'c_std=gnu11',
    'default_library=shared',
    'warning_level=everything',
    'b_lto=false',
  ],
)

# Aliases to significant locations in the source tree
src = 'src'
extern = 'extern'
lua_resty = extern / 'lua-resty-base-encoding'
quick_args = extern / 'quick_arg_parser'
spcdns = extern / 'SPCDNS' / src

# Sources common to both client and server
common_sources = files(
  src / 'slipstream_flow.c',
  src / 'slipstream_pin.c',
  src / 'slipstream_bridge.c',
  src / 'slipstream_sockloop.c',
  src / 'slipstream_utils.c',
  src / 'slipstream_inline_dots.c',
  lua_resty / 'base32.c',
  spcdns / 'codec.c',
  spcdns / 'mappings.c',
  spcdns / 'netsimple.c',
  spcdns / 'output.c',
)

# Various directories with headers for the sources
include_dirs = include_directories('include', extern, quick_args, spcdns, lua_resty)

# Default linker arguments
link_args = []

# Set up static links
is_static = get_option('default_library') == 'static'
if is_static and host_machine.system() != 'darwin'
  link_args = [
    '-static-libgcc',
    '-static-libstdc++',
  ]
endif

# Import the CMake module
cmake = import('cmake')
python = import('python').find_installation('python3')
run_command(python, files('patches/apply-picoquic-patch.py'), check: true)

# Get the cmake options objects
picoquic_opts = cmake.subproject_options()

# Add necessary CMake defines
picoquic_opts.add_cmake_defines(
  [
    {'PICOQUIC_FETCH_PTLS': 'ON'},
    {'BUILD_HTTP': 'OFF'},
    {'ENABLE_ASAN': 'OFF'},
    {'ENABLE_UBSAN': 'OFF'},
    {'CMAKE_POLICY_VERSION_MINIMUM': '3.5'},
    {'BUILD_DEMO': 'OFF'},
    {'BUILD_TESTING': 'OFF'},
  ],
)
# Configure picoquic debug printing
picoquic_opts.add_cmake_defines({'DISABLE_DEBUG_PRINTF': 'ON'})
# Configure optional picoquic logging
loglib_enabled = get_option('build_loglib')
if loglib_enabled
  picoquic_opts.add_cmake_defines(
    {'BUILD_LOGREADER': 'ON'},
  )
  add_project_arguments('-DBUILD_LOGLIB', language: ['c', 'cpp'])
else
  picoquic_opts.add_cmake_defines(
    {'BUILD_LOGREADER': 'OFF'},
  )
endif

# Reference picoquic as a CMake subproject
picoquic_proj = cmake.subproject(
  'picoquic',
  options: picoquic_opts,
)

# Dependencies used by both executables
slipstream_cli_deps = [
  dependency('threads'), # for picoquic
  picoquic_proj.dependency('picoquic-core'),
  picoquic_proj.dependency('picotls-core'),
  picoquic_proj.dependency('picotls-openssl'),
  picoquic_proj.dependency('picotls-minicrypto'),
  picoquic_proj.dependency('picotls-fusion'),
  dependency('openssl'),
  meson.get_compiler('c').find_library('m'),
]
# Link optional picoquic logging
if loglib_enabled
  slipstream_cli_deps += picoquic_proj.dependency('picoquic-log')
endif

# Sources only used by client
client_sources = files(
  src / 'slipstream_client_runtime.c',
  src / 'slipstream_client.c',
  src / 'slipstream_client_cli.cpp',
)

# Sources only used by server
server_sources = files(
  src / 'slipstream_server_runtime.c',
  src / 'slipstream_server.c',
  src / 'slipstream_server_cc.c',
  src / 'slipstream_server_cli.cpp',
)

# The final client executable
client_exe = executable(
  'slipstream-client',
  client_sources + common_sources,
  dependencies: slipstream_cli_deps,
  include_directories: include_dirs,
  link_args: link_args,
  build_by_default: true,
)

# The final server executable
server_exe = executable(
  'slipstream-server',
  server_sources + common_sources,
  dependencies: slipstream_cli_deps,
  include_directories: include_dirs,
  link_args: link_args,
  build_by_default: true,
)

protocol_test = executable('test-protocol',
  files('tests/test_protocol.c', src / 'slipstream_flow.c', src / 'slipstream_bridge.c'),
  dependencies: slipstream_cli_deps, include_directories: include_dirs,
  c_args: ['-UNDEBUG'])
test('protocol', protocol_test)
path_test = executable('test-path-guard', files('tests/test_path_guard.c'),
  dependencies: slipstream_cli_deps, include_directories: include_dirs,
  c_args: ['-UNDEBUG'])
test('path-guard', path_test)
pin_test = executable('test-pin', files('tests/test_pin.c'),
  dependencies: slipstream_cli_deps, include_directories: include_dirs,
  c_args: ['-UNDEBUG'])
test('pin', pin_test)
poll_submit_test = executable('test-poll-submit',
  files('tests/test_poll_submit.c', src / 'slipstream_flow.c'),
  dependencies: slipstream_cli_deps, include_directories: include_dirs,
  c_args: ['-UNDEBUG'])
test('poll-submit', poll_submit_test)
poll_ack_test = executable('test-poll-ack', files('tests/test_poll_ack.c'),
  dependencies: slipstream_cli_deps, include_directories: include_dirs,
  c_args: ['-UNDEBUG'])
test('poll-ack', poll_ack_test)
path_ack_context_test = executable('test-path-ack-context', files('tests/test_path_ack_context.c'),
  dependencies: slipstream_cli_deps, include_directories: include_dirs,
  c_args: ['-UNDEBUG'])
test('path-ack-context', path_ack_context_test)
server_dns_test = executable('test-server-dns',
  files('tests/test_server_dns.c', src / 'slipstream_server.c',
    src / 'slipstream_inline_dots.c', src / 'slipstream_utils.c',
    lua_resty / 'base32.c', spcdns / 'codec.c'),
  dependencies: slipstream_cli_deps, include_directories: include_dirs,
  c_args: ['-UNDEBUG'])
test('server-dns', server_dns_test)
test('runtime', python,
  args: [files('tests/test_runtime.py'), meson.current_build_dir()],
  depends: [client_exe, server_exe], timeout: 120)
