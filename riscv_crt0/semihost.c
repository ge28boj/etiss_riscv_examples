/*
 * original implementation of semi-hosting calls has been modified to fit for this project
 * Only Supports minimal functionality  1. open 2.close 3.read 4.write and "semi_write" which write to debug console
 * No buffered IO.
 * "open" system calls does't check if file to open exits. So user has to take care of following
 *         1. Read a file that does't exits
 *         2. Create a new file which already exits with same name
 *  To avoid any complications delete gmon.out file before re-running the program to generate profiling data
 */

/*
 * This file is part of the µOS++ distribution.
 *   (https://github.com/micro-os-plus)
 * Copyright (c) 2015 Liviu Ionescu.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */





#include<stdio.h>
#include <sys/fcntl.h>
#include <errno.h>
#include "semihost.h"
#include "proto.h"


#define LEN 128

struct file opened_files[OS_INTEGER_SEMIHOSTING_MAX_OPEN_FILES];




 enum semihosting_operation_numbers
  {
/*
 * ARM semihosting operations, in lexicographic order.
 */
SEMIHOSTING_ENTER_SVC = 0x17, /* DEPRECATED */

SEMIHOSTING_SYS_CLOSE = 0x02,
SEMIHOSTING_SYS_CLOCK = 0x10,
SEMIHOSTING_SYS_ELAPSED = 0x30,
SEMIHOSTING_SYS_ERRNO = 0x13,
SEMIHOSTING_SYS_EXIT = 0x18,
SEMIHOSTING_SYS_EXIT_EXTENDED = 0x20,
SEMIHOSTING_SYS_FLEN = 0x0C,
SEMIHOSTING_SYS_GET_CMDLINE = 0x15,
SEMIHOSTING_SYS_HEAPINFO = 0x16,
SEMIHOSTING_SYS_ISERROR = 0x08,
SEMIHOSTING_SYS_ISTTY = 0x09,
SEMIHOSTING_SYS_OPEN = 0x01,
SEMIHOSTING_SYS_READ = 0x06,
SEMIHOSTING_SYS_READC = 0x07,
SEMIHOSTING_SYS_REMOVE = 0x0E,
SEMIHOSTING_SYS_RENAME = 0x0F,
SEMIHOSTING_SYS_SEEK = 0x0A,
SEMIHOSTING_SYS_SYSTEM = 0x12,
SEMIHOSTING_SYS_TICKFREQ = 0x31,
SEMIHOSTING_SYS_TIME = 0x11,
SEMIHOSTING_SYS_TMPNAM = 0x0D,
SEMIHOSTING_SYS_WRITE = 0x05,
SEMIHOSTING_SYS_WRITEC = 0x03,
SEMIHOSTING_SYS_WRITE0 = 0x04,
  };





static inline int
  __attribute__ ((always_inline))
  os_semihosting_call_host (int reason, void* arg)
  {
register int value asm ("a0") = reason;
register void* ptr asm ("a1") = arg;

asm volatile (

    // Workaround for RISC-V lack of multiple EBREAKs.
    " .option push \n"
    " .option norvc \n"
    " slli x0, x0, 0x1f \n"
    " ebreak \n"
    " srai x0, x0, %[swi] \n"
    " .option pop \n"

    : "=r" (value) // Outputs
    : "0" (value), "r" (ptr), [swi] "i" (RISCV_SEMIHOSTING_CALL_NUMBER) // Inputs
    : "memory" // Clobbers
);

return value;
}




int semi_write (const void* buf, int nbyte)   // Debug channel write.
{
 char tmp[OS_INTEGER_TRACE_SEMIHOSTING_BUFF_ARRAY_SIZE];
 int togo = nbyte;
  if (buf == NULL || nbyte == 0)
    {
      return 0;
    }

  const char* cbuf = (const char*) buf;

  // Since the single character debug channel is quite slow, try to
  // optimize and send a null terminated string, if possible.
  if (cbuf[nbyte] == '\0')
    {
      // send string
      os_semihosting_call_host(SEMIHOSTING_SYS_WRITE0, (void*) cbuf);
    }
  else
    {
      // If not, use a local buffer to speed things up.
      // For re-entrance, this bugger must be allocated on the stack,
      // so be cautious with the size.

      while (togo > 0)
        {
          int n = ((togo < sizeof(tmp)) ? togo : sizeof(tmp) - 1);
          int i = 0;
          for (; i < n; ++i, ++cbuf)
            {
              tmp[i] = *cbuf;
            }
          tmp[i] = '\0';

          os_semihosting_call_host (SEMIHOSTING_SYS_WRITE0, (void*) tmp);

          togo -= n;
        }
    }

  // All bytes written.
  return (int) nbyte;
}

//NO_INSTRUMENT PROFILE

void init_handles(void){
	int i;
	 for (i = 0; i < OS_INTEGER_SEMIHOSTING_MAX_OPEN_FILES; i++)
	      opened_files[i].handle = -1;

}

int
 new_slot (void)
 {
   size_t i;
   for (i = 0; i < OS_INTEGER_SEMIHOSTING_MAX_OPEN_FILES; i++)
     {
       if (opened_files[i].handle == -1)
         {
           break;
         }
     }

   if (i == OS_INTEGER_SEMIHOSTING_MAX_OPEN_FILES)
     {
       return -1;
     }

   return (int) i;
 }

struct file*
find_slot (int fd)
{
  if ((size_t) fd >= OS_INTEGER_SEMIHOSTING_MAX_OPEN_FILES)
    {
      // File descriptor is out of range.
      return NULL;
    }

  if (opened_files[fd].handle == -1)
    {
      // File descriptor not in use.
      return NULL;
    }

  // Valid, return host file descriptor.
  return &opened_files[fd];
}

int
get_host_errno (void)
{
  return os_semihosting_call_host(SEMIHOSTING_SYS_ERRNO, NULL);
}

/**
 * Set errno and return result.
 */
int with_set_errno (int result)
{
  errno = get_host_errno ();
  return result;
}

int check_error (int result)
{
  if (result == -1)
    {
      return with_set_errno (-1);
    }

  return result;
}
