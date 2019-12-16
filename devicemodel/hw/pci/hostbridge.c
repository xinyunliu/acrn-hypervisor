/*-
 * Copyright (c) 2011 NetApp, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY NETAPP, INC ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL NETAPP, INC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include "pci_core.h"

static int
pci_hostbridge_init(struct vmctx *ctx, struct pci_vdev *pi, char *opts)
{
	/* config space */
	pci_set_cfgdata16(pi, PCIR_VENDOR, 0x1275);	/* NetApp */
	pci_set_cfgdata16(pi, PCIR_DEVICE, 0x1275);	/* NetApp */
	pci_set_cfgdata8(pi, PCIR_HDRTYPE, PCIM_HDRTYPE_NORMAL);
	pci_set_cfgdata8(pi, PCIR_CLASS, PCIC_BRIDGE);
	pci_set_cfgdata8(pi, PCIR_SUBCLASS, PCIS_BRIDGE_HOST);

	pci_set_cfgdata8(pi, 0x08, 0x0b);
	pci_set_cfgdata16(pi, 0x2c, 0x0000);
	pci_set_cfgdata16(pi, 0x2e, 0x0000);

	pci_emul_add_pciecap(pi, PCIEM_TYPE_ROOT_PORT);

	return 0;
}

static int
pci_amd_hostbridge_init(struct vmctx *ctx, struct pci_vdev *pi, char *opts)
{
	(void) pci_hostbridge_init(ctx, pi, opts);
	pci_set_cfgdata16(pi, PCIR_VENDOR, 0x1022);	/* AMD */
	pci_set_cfgdata16(pi, PCIR_DEVICE, 0x7432);	/* made up */

	return 0;
}

struct pci_vdev_ops pci_ops_amd_hostbridge = {
	.class_name	= "amd_hostbridge",
	.vdev_init	= pci_amd_hostbridge_init,
};
DEFINE_PCI_DEVTYPE(pci_ops_amd_hostbridge);

struct pci_vdev_ops pci_ops_hostbridge = {
	.class_name	= "hostbridge",
	.vdev_init	= pci_hostbridge_init,
};
DEFINE_PCI_DEVTYPE(pci_ops_hostbridge);
