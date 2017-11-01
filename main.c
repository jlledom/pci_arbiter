/*
   Copyright (C) 2017 Free Software Foundation, Inc.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   The GNU Hurd is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the GNU Hurd.  If not, see <http://www.gnu.org/licenses/>.
*/

/* Translator initialization and demuxing */

#include <pci_arbiter.h>

#include <stdio.h>
#include <error.h>
#include <fcntl.h>
#include <version.h>
#include <unistd.h>
#include <hurd/netfs.h>

#include <pci_access.h>
#include <pci_conf_S.h>
#include "libnetfs/io_S.h"
#include "libnetfs/fs_S.h"
#include "libports/notify_S.h"
#include "libnetfs/fsys_S.h"
#include "libports/interrupt_S.h"
#include "libnetfs/ifsock_S.h"


/* Libnetfs stuff */
int netfs_maxsymlinks = 0;
char *netfs_server_name = "pci_arbiter";
char *netfs_server_version = HURD_VERSION;

int
netfs_demuxer (mach_msg_header_t * inp, mach_msg_header_t * outp)
{
  mig_routine_t routine;

  if ((routine = netfs_io_server_routine (inp)) ||
      (routine = netfs_fs_server_routine (inp)) ||
      (routine = ports_notify_server_routine (inp)) ||
      (routine = netfs_fsys_server_routine (inp)) ||
      (routine = ports_interrupt_server_routine (inp)) ||
      (routine = netfs_ifsock_server_routine (inp)) ||
      (routine = pci_conf_server_routine (inp)))
    {
      (*routine) (inp, outp);
      return TRUE;
    }
  else
    return FALSE;
}

int
main (int argc, char **argv)
{
  error_t err;
  mach_port_t bootstrap;
  file_t underlying_node;
  io_statbuf_t underlying_node_stat;

  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  if (bootstrap == MACH_PORT_NULL)
    error (1, 0, "must be started as a translator");

  /* Initialize netfs and start the translator. */
  netfs_init ();

  underlying_node = netfs_startup (bootstrap, O_READ);
  netfs_root_node = netfs_make_node (0);

  /* Initialize status from underlying node.  */
  err = io_stat (underlying_node, &underlying_node_stat);
  if (err)
    error (1, err, "io_stat");

  netfs_root_node->nn_stat = underlying_node_stat;
  netfs_root_node->nn_stat.st_fsid = getpid ();
  netfs_root_node->nn_stat.st_mode = S_IFDIR | (underlying_node_stat.st_mode
						& ~S_IFMT & ~S_ITRANS);
  netfs_root_node->nn_translated = netfs_root_node->nn_stat.st_mode;

  /* If the underlying node isn't a directory, enhance the stat
     information.  */
  if (!S_ISDIR (underlying_node_stat.st_mode))
    {
      if (underlying_node_stat.st_mode & S_IRUSR)
	netfs_root_node->nn_stat.st_mode |= S_IXUSR;
      if (underlying_node_stat.st_mode & S_IRGRP)
	netfs_root_node->nn_stat.st_mode |= S_IXGRP;
      if (underlying_node_stat.st_mode & S_IROTH)
	netfs_root_node->nn_stat.st_mode |= S_IXOTH;
    }

  /* Start the PCI system */
  err = pci_system_init ();
  if (err)
    error (1, err, "Error starting the PCI system");

  netfs_server_loop ();		/* Never returns.  */

  return 0;
}
