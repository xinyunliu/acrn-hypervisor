/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2015 Nahanni Systems, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/mman.h>

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <vmmapi.h>

#include <string.h>

#include <errno.h>

#include "console.h"
#include "inout.h"
#include "pci_core.h"
#include "rfb.h"
#include "vga.h"
#include "gc.h"
#include "dm_string.h"

/*
 * Framebuffer device emulation.
 * BAR0 points to the current mode information.
 * BAR1 is the 32-bit framebuffer address.
 *
 *  -s <b>,fbuf,wait,vga=on|io|off,rfb=<ip>:port,w=width,h=height
 */

static int fbuf_debug = 4;
#define	DEBUG_INFO	1
#define	DEBUG_VERBOSE	4
#define	DPRINTF(level, params)  if (level <= fbuf_debug) printf params


#define	KB	(1024UL)
#define	MB	(1024 * 1024UL)

#define	DMEMSZ	128

#define	FB_SIZE		(16*MB)

#define COLS_MAX	1920
#define	ROWS_MAX	1200

#define COLS_DEFAULT	1024
#define ROWS_DEFAULT	768

#define COLS_MIN	640
#define ROWS_MIN	480

struct pci_fbuf_vdev {
	struct pci_vdev *dev;
	struct {
		uint32_t fbsize;
		uint16_t width;
		uint16_t height;
		uint16_t depth;
		uint16_t refreshrate;
		uint8_t  reserved[116];
	} __attribute__((packed)) memregs;

	/* rfb server */
	char      *rfb_host;
	char      *rfb_password;
	int       rfb_port;
	int       rfb_wait;
	int       vga_enabled;
	int	  vga_full;

	uint32_t  fbaddr;
	char      *fb_base;
	uint16_t  gc_width;
	uint16_t  gc_height;
	void      *vga_dev;
	struct gfx_ctx_image *gc_image;
};

static struct pci_fbuf_vdev *fbuf_dev;

#define	PCI_FBUF_MSI_MSGS	 4

static void
pci_fbuf_usage(char *opt)
{

	fprintf(stderr, "Invalid fbuf emulation option \"%s\"\r\n", opt);
	fprintf(stderr, "fbuf: {wait,}{vga=on|io|off,}rfb=<ip>:port"
	    "{,w=width}{,h=height}\r\n");
}

static void
pci_fbuf_write(struct vmctx *ctx, int vcpu, struct pci_vdev *dev,
	       int baridx, uint64_t offset, int size, uint64_t value)
{
	struct pci_fbuf_vdev *fb;
	uint8_t *p;

	assert(baridx == 0);

	fb = dev->arg;

	DPRINTF(DEBUG_VERBOSE,
	    ("fbuf wr: offset 0x%lx, size: %d, value: 0x%lx\n",
	    offset, size, value));

	if (offset + size > DMEMSZ) {
		printf("fbuf: write too large, offset %ld size %d\n",
		       offset, size);
		return;
	}

	p = (uint8_t *)&fb->memregs + offset;

	switch (size) {
	case 1:
		*p = value;
		break;
	case 2:
		*(uint16_t *)p = value;
		break;
	case 4:
		*(uint32_t *)p = value;
		break;
	case 8:
		*(uint64_t *)p = value;
		break;
	default:
		printf("fbuf: write unknown size %d\n", size);
		break;
	}

	if (!fb->gc_image->vgamode && fb->memregs.width == 0 &&
	    fb->memregs.height == 0) {
		DPRINTF(DEBUG_INFO, ("switching to VGA mode\r\n"));
		fb->gc_image->vgamode = 1;
		fb->gc_width = 0;
		fb->gc_height = 0;
	} else if (fb->gc_image->vgamode && fb->memregs.width != 0 &&
	    fb->memregs.height != 0) {
		DPRINTF(DEBUG_INFO, ("switching to VESA mode\r\n"));
		fb->gc_image->vgamode = 0;
	}
}

uint64_t
pci_fbuf_read(struct vmctx *ctx, int vcpu, struct pci_vdev *dev,
	      int baridx, uint64_t offset, int size)
{
	struct pci_fbuf_vdev *fb;
	uint8_t *p;
	uint64_t value;

	assert(baridx == 0);

	fb = dev->arg;


	if (offset + size > DMEMSZ) {
		printf("fbuf: read too large, offset %ld size %d\n",
		       offset, size);
		return (0);
	}

	p = (uint8_t *)&fb->memregs + offset;
	value = 0;
	switch (size) {
	case 1:
		value = *p;
		break;
	case 2:
		value = *(uint16_t *)p;
		break;
	case 4:
		value = *(uint32_t *)p;
		break;
	case 8:
		value = *(uint64_t *)p;
		break;
	default:
		printf("fbuf: read unknown size %d\n", size);
		break;
	}

	DPRINTF(DEBUG_VERBOSE,
	    ("fbuf rd: offset 0x%lx, size: %d, value: 0x%lx\n",
	     offset, size, value));

	return (value);
}

static int
pci_fbuf_parse_opts(struct pci_fbuf_vdev *fb, char *opts)
{
	char	*uopts, *xopts, *config, *tmp;
	char	*tmpstr;
	int	ret;
	unsigned int	val;

	ret = 0;
	uopts = strdup(opts);
	for (xopts = strtok_r(uopts, ",", &tmp);
	     xopts != NULL;
	     xopts = strtok_r(NULL, ",", &tmp)) {
		if (strcmp(xopts, "wait") == 0) {
			fb->rfb_wait = 1;
			continue;
		}

		if ((config = strchr(xopts, '=')) == NULL) {
			pci_fbuf_usage(xopts);
			ret = -1;
			goto done;
		}

		*config++ = '\0';

		DPRINTF(DEBUG_VERBOSE, ("pci_fbuf_vdev option %s = %s\r\n",
		   xopts, config));

		if (!strcmp(xopts, "tcp") || !strcmp(xopts, "rfb")) {
			/*
			 * IPv4 -- host-ip:port
			 * IPv6 -- [host-ip%zone]:port
			 * XXX for now port is mandatory.
			 */
			tmpstr = strsep(&config, "]");
			if (config) {
				if (tmpstr[0] == '[')
					tmpstr++;
				fb->rfb_host = tmpstr;
				if (config[0] == ':')
					config++;
				else {
					pci_fbuf_usage(xopts);
					ret = -1;
					goto done;
				}
				ret = dm_strtoi(config, &config, 10, &fb->rfb_port);
				if (ret)
					goto done;
			} else {
				config = tmpstr;
				tmpstr = strsep(&config, ":");
				if (!config)
					ret = dm_strtoi(tmpstr, &tmpstr, 10, &fb->rfb_port);
				else {
					ret = dm_strtoi(config, &config, 10, &fb->rfb_port);
					fb->rfb_host = tmpstr;
				}
				if (ret)
					goto done;
			}
	        } else if (!strcmp(xopts, "vga")) {
			if (!strcmp(config, "off")) {
				fb->vga_enabled = 0;
			} else if (!strcmp(config, "io")) {
				fb->vga_enabled = 1;
				fb->vga_full = 0;
			} else if (!strcmp(config, "on")) {
				fb->vga_enabled = 1;
				fb->vga_full = 1;
			} else {
				pci_fbuf_usage(xopts);
				ret = -1;
				goto done;
			}
	        } else if (!strcmp(xopts, "w")) {
			if (!dm_strtoui(config, &config, 10, &val) &&
				val == (uint16_t)val)
				fb->memregs.width = val;
			else {
				ret = -1;
				goto done;
			}
			if (fb->memregs.width > COLS_MAX) {
				pci_fbuf_usage(xopts);
				ret = -1;
				goto done;
			} else if (fb->memregs.width == 0)
				fb->memregs.width = 1920;
		} else if (!strcmp(xopts, "h")) {
			if (!dm_strtoui(config, &config, 10, &val) &&
				val == (uint16_t)val)
				fb->memregs.height = val;
			else {
				ret = -1;
				goto done;
			}
			if (fb->memregs.height > ROWS_MAX) {
				pci_fbuf_usage(xopts);
				ret = -1;
				goto done;
			} else if (fb->memregs.height == 0)
				fb->memregs.height = 1080;
		} else if (!strcmp(xopts, "password")) {
			fb->rfb_password = config;
		} else {
			pci_fbuf_usage(xopts);
			ret = -1;
			goto done;
		}
	}

done:
	printf("################# fb->memregs.height=%d fb->memregs.width=%d fb->rfb_port=%d ###########", fb->memregs.height,fb->memregs.width,fb->rfb_port);
	return (ret);
}


extern void vga_render(struct gfx_ctx *gc, void *arg);

void
pci_fbuf_render(struct gfx_ctx *gc, void *arg)
{
	struct pci_fbuf_vdev *fb;

	fb = arg;

	if (fb->vga_full && fb->gc_image->vgamode) {
		/* TODO: mode switching to vga and vesa should use the special
		 *      EFI-bhyve protocol port.
		 */
		vga_render(gc, fb->vga_dev);
		return;
	}
	if (fb->gc_width != fb->memregs.width ||
	    fb->gc_height != fb->memregs.height) {
		gc_resize(gc, fb->memregs.width, fb->memregs.height);
		fb->gc_width = fb->memregs.width;
		fb->gc_height = fb->memregs.height;
	}

	return;
}

static int
pci_fbuf_init(struct vmctx *ctx, struct pci_vdev *dev, char *opts)
{
	int error, prot;
	struct pci_fbuf_vdev *fb;

	if (fbuf_dev != NULL) {
		fprintf(stderr, "Only one frame buffer device is allowed.\n");
		return (-1);
	}

	fb = calloc(1, sizeof(struct pci_fbuf_vdev));

	dev->arg = fb;

	/* initialize config space */
	pci_set_cfgdata16(dev, PCIR_DEVICE, 0x40FB);
	pci_set_cfgdata16(dev, PCIR_VENDOR, 0xFB5D);
	pci_set_cfgdata8(dev, PCIR_CLASS, PCIC_DISPLAY);
	pci_set_cfgdata8(dev, PCIR_SUBCLASS, PCIS_DISPLAY_VGA);

	error = pci_emul_alloc_bar(dev, 0, PCIBAR_MEM32, DMEMSZ);
	assert(error == 0);

	error = pci_emul_alloc_bar(dev, 1, PCIBAR_MEM32, FB_SIZE);
	assert(error == 0);

	error = pci_emul_add_msicap(dev, PCI_FBUF_MSI_MSGS);
	assert(error == 0);

	fb->fbaddr = dev->bar[1].addr;
	fb->memregs.fbsize = FB_SIZE;
	fb->memregs.width  = COLS_DEFAULT;
	fb->memregs.height = ROWS_DEFAULT;
	fb->memregs.depth  = 32;

	fb->vga_enabled = 1;
	fb->vga_full = 0;

	fb->dev = dev;

	error = pci_fbuf_parse_opts(fb, opts);
	if (error != 0)
		goto done;

	/* XXX until VGA rendering is enabled */
	if (fb->vga_full != 0) {
		fprintf(stderr, "pci_fbuf: VGA rendering not enabled\r\n");
		goto done;
	}

	fb->fb_base = ctx->fb_base;
	DPRINTF(DEBUG_INFO, ("fbuf frame buffer base: %p [sz 0x%lx]\r\n",
	        fb->fb_base, FB_SIZE));

	/*
	 * Map the framebuffer into the guest address space.
	 * XXX This may fail if the BAR is different than a prior
	 * run. In this case flag the error. This will be fixed
	 * when a change_memseg api is available.
	 */
	prot = PROT_READ | PROT_WRITE;
	if (vm_map_memseg_vma(ctx, FB_SIZE, fb->fbaddr, (uint64_t)fb->fb_base, prot) != 0) {
		fprintf(stderr, "pci_fbuf: mapseg failed - try deleting VM and restarting\n");
		error = -1;
		goto done;
	}

	console_init(fb->memregs.width, fb->memregs.height, fb->fb_base);
	console_fb_register(pci_fbuf_render, fb);

	if (fb->vga_enabled)
		fb->vga_dev = vga_init(!fb->vga_full);
	fb->gc_image = console_get_image();

	fbuf_dev = fb;

	memset((void *)fb->fb_base, 0, FB_SIZE);

	error = rfb_init(fb->rfb_host, fb->rfb_port, fb->rfb_wait, fb->rfb_password);
done:
	if (error)
		free(fb);

	return (error);
}

struct pci_vdev_ops pci_fbuf = {
	.class_name =	"fbuf",
	.vdev_init =	pci_fbuf_init,
	.vdev_barwrite =pci_fbuf_write,
	.vdev_barread =	pci_fbuf_read
};
DEFINE_PCI_DEVTYPE(pci_fbuf);
