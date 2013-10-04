#! /usr/bin/env python

import sys

import pybindgen
from pybindgen import retval, param
from pybindgen import ReturnValue, Parameter, Module, Function, FileCodeSink
from pybindgen import CppMethod, CppConstructor, CppClass, Enum


def my_module_gen(out_file):
    mod = Module('cmrclient')
    mod.add_include('"cmrclient.h"')
    mod.add_container('std::vector<std::string>', 'std::string', 'vector');

    cl = mod.add_class('Client', allow_subclassing=True)
    cl.add_constructor([Parameter.new('const std::string&', 'id')])

    # HOST setting functions
    cl.add_method('reset_hosts', None, []);
    cl.add_method('add_host', None,
                  [Parameter.new('const std::string&', 'hostname'),
                   Parameter.new('int', 'port')])
    cl.add_method('send_host_list', None, [])

    # GET functions
    cl.add_method('get', retval('PyObject*', caller_owns_return=True),
                  [param('const std::string&', 'key')])
    cl.add_method('gets', retval('PyObject*', caller_owns_return=True),
                  [param('const std::string&', 'key')])
    cl.add_method('get_multi', retval('PyObject*', caller_owns_return=True),
                  [param('const std::vector<std::string>&', 'keys')])

    # SET functions
    cl.add_method('set', retval('bool'),
                  [param('const std::string&', 'key'),
                   param('PyObject*', 'val', transfer_ownership=False),
                   param('uint64_t', 'time', default_value='0')])

    cl.add_method('add', retval('bool'),
                  [param('const std::string&', 'key'),
                   param('PyObject*', 'val', transfer_ownership=False),
                   param('uint64_t', 'time', default_value='0')])

    cl.add_method('cas', retval('bool'),
                  [param('const std::string&', 'key'),
                   param('PyObject*', 'val', transfer_ownership=False),
                   param('uint64_t', 'cas_unique'),
                   param('uint64_t', 'time', default_value='0')])

    # INCREMENT functions
    cl.add_method('incr', retval('uint64_t'),
                  [param('const std::string&', 'key'),
                   param('int', 'offset')])
    cl.add_method('decr', retval('uint64_t'),
                  [param('const std::string&', 'key'),
                   param('int', 'offset')])

    # TEST functions
    cl.add_method('Echo', retval('std::string'),
                  [param('const std::vector<std::string>&', 'messages')])
    cl.add_method('Test', retval('PyObject*', caller_owns_return=True),
                  [param('PyObject*', 'obj', transfer_ownership=False)])
    cl.add_method('DecompressTest', retval('std::string'),
                  [param('const std::string&', 'input')])

    # Now generate code.
    mod.generate(FileCodeSink(out_file))

if __name__ == '__main__':
    my_module_gen(sys.stdout)
