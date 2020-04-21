# SPDX-License-Identifier: BSD-2-Clause

import re
import os
import sys
import fcntl
import subprocess

from enum import Enum
from .stdio import msg_print, raw_print

# Constants
TEST_TYPES = ['selftest', 'shellcmd']
TEST_TYPES_PRETTY = ['Self tests', 'Shell cmd tests']

KERNEL_DUMP_GCDA_STR = '** GCOV gcda files **'
KERNEL_DUMP_GCDA_END_STR = '** GCOV gcda files END **'

class Fail(Enum):
   success                 = 0
   invalid_args            = 1
   reboot                  = 2
   timeout                 = 3
   panic                   = 4
   shell_no_zero_exit      = 5
   gcov_error              = 6
   shell_unknown_exit_code = 7
   invalid_build_config    = 8
   invalid_system_config   = 9
   no_hello_message        = 10
   user_interruption       = 11
   qemu_msg_parsing_fail   = 12
   other                   = 13

def getFailByCode(err_code):

   for f in Fail:
      if f.value == err_code:
         return f

   return None

def is_cmake_opt_enabled(opt):
   return opt.lower() in ["on", "1", "true", "yes", "y"]

def fh_set_blocking_mode(fh, blocking):

   sys_fd = fh.fileno()

   fl = fcntl.fcntl(sys_fd, fcntl.F_GETFL)

   if not blocking:
      fcntl.fcntl(sys_fd, fcntl.F_SETFL, fl | os.O_NONBLOCK)
   else:
      fcntl.fcntl(sys_fd, fcntl.F_SETFL, fl & ~os.O_NONBLOCK)

class InvalidSystemConfig(Exception):
   def __init__(self, msg):
      super(InvalidSystemConfig, self).__init__(msg)

class NoTilckHelloMessage(Exception):
   def __init__(self, screen_text = None):
      super(NoTilckHelloMessage, self).__init__("NoTilckHelloMessage")
      self.screen_text = screen_text

class KernelPanicFailure(Exception):
   def __init__(self, screen_text = None):
      super(KernelPanicFailure, self).__init__("KernelPanicFailure")
      self.screen_text = screen_text

def run_gen_coverage_report_tool(gen_cov_tool):

   try:

      subprocess.check_output([gen_cov_tool, '--acc'])

   except Exception as e:

      msg_print(
         "{} generated the exception: {}".format(gen_cov_tool, str(e))
      )
      msg_print("Output of {} --acc:".format(gen_cov_tool))
      raw_print(getattr(e, 'output', '<no output>'))
      msg_print("--- end output ---")
      return False

   return True