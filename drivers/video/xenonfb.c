/*
 * framebuffer driver for Microsoft Xbox 360
 *
 * (c) 2006 ...
 * Original vesafb driver written by Gerd Knorr <kraxel@goldbach.in-berlin.de>
 *
 */

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fb.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/screen_info.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/dmi.h>

#include <asm/io.h>

#include <video/vga.h>

/* --------------------------------------------------------------------- */

static struct fb_var_screeninfo xenonfb_defined __initdata = {
	.activate		= FB_ACTIVATE_NOW,
	.height			= -1,
	.width			= -1,
	.right_margin		= 32,
	.upper_margin		= 16,
	.lower_margin		= 4,
	.vsync_len		= 4,
	.vmode			= FB_VMODE_NONINTERLACED,
};

static struct fb_fix_screeninfo xenonfb_fix __initdata = {
	.id			= "XENON FB",
	.type			= FB_TYPE_PACKED_PIXELS,
	.accel			= FB_ACCEL_NONE,
	.visual			= FB_VISUAL_TRUECOLOR,
};

typedef struct {
	uint32_t unknown1[4];
	uint32_t base;
	uint32_t unknown2[8];
	uint32_t width;
	uint32_t height;
} ati_info;

#define	DEFAULT_FB_MEM	1024*1024*16

/* --------------------------------------------------------------------- */

static int xenonfb_setcolreg(unsigned regno, unsigned red, unsigned green,
			    unsigned blue, unsigned transp,
			    struct fb_info *info)
{
	/*
	 *  Set a single color register. The values supplied are
	 *  already rounded down to the hardware's capabilities
	 *  (according to the entries in the `var' structure). Return
	 *  != 0 for invalid regno.
	 */

	if (regno >= info->cmap.len)
		return 1;

	if (regno < 16) {
		red   >>= 8;
		green >>= 8;
		blue  >>= 8;
		((u32 *)(info->pseudo_palette))[regno] =
			(red   << info->var.red.offset)   |
			(green << info->var.green.offset) |
			(blue  << info->var.blue.offset);
	}
	return 0;
}

#define XENON_XY_TO_STD_PTR(x,y) ((int*)(((char*)p->screen_base)+y*p->fix.line_length+x*(p->var.bits_per_pixel/8)))
#define XENON_XY_TO_XENON_PTR(x,y) xenon_convert(p, XENON_XY_TO_STD_PTR(x,y))

inline void xenon_pset(struct fb_info *p, int x, int y, int color)
{
	fb_writel(color, XENON_XY_TO_XENON_PTR(x,y));
}

inline int xenon_pget(struct fb_info *p, int x, int y)
{
	return fb_readl(XENON_XY_TO_XENON_PTR(x,y));
}

void xenon_fillrect(struct fb_info *p, const struct fb_fillrect *rect)
{

	__u32 x, y;
	for (y=0; y<rect->height; y++) {
		for (x=0; x<rect->width; x++) {
			xenon_pset(p, rect->dx+x, rect->dy+y, rect->color);

		}
	}
}

void xenon_copyarea(struct fb_info *p, const struct fb_copyarea *area)
{

	/* if the beginning of the target area might overlap with the end of
	the source area, be have to copy the area reverse. */
	if ((area->dy == area->sy && area->dx > area->sx) || (area->dy > area->sy)) {
		__s32 x, y;
		for (y=area->height-1; y>0; y--) {
			for (x=area->width-1; x>0; x--) {
				xenon_pset(p, area->dx+x, area->dy+y, xenon_pget(p, area->sx+x, area->sy+y));
			}
		}
	} else {
		__u32 x, y;
		for (y=0; y<area->height; y++) {
			for (x=0; x<area->width; x++) {
				xenon_pset(p, area->dx+x, area->dy+y, xenon_pget(p, area->sx+x, area->sy+y));
			}
		}
	}
}

static struct fb_ops xenonfb_ops = {
	.owner		= THIS_MODULE,
	.fb_setcolreg	= xenonfb_setcolreg,
	.fb_fillrect	= xenon_fillrect,
	.fb_copyarea	= xenon_copyarea,
	.fb_imageblit	= cfb_imageblit,
};

static int __init xenonfb_probe(struct platform_device *dev)
{
	struct fb_info *info;
	int err;
	unsigned int size_vmode;
	unsigned int size_remap;
	unsigned int size_total;

	volatile int *gfx = ioremap(0x200ec806000ULL, 0x1000);

	volatile ati_info *ai = ((void*)gfx) + 0x100;

			/* setup native resolution, i.e. disable scaling */
	int vxres = gfx[0x134/4];
	int vyres = gfx[0x138/4];

	int black_top = gfx[0x44/4];
	int offset = gfx[0x580/4];
	int offset_x = (offset >> 16) & 0xFFFF;
	int offset_y = offset & 0xFFFF;

	int nxres, nyres;
	int scl_h = gfx[0x5b4/4], scl_v = gfx[0x5c4/4];

	if (gfx[0x590/4] == 0)
		scl_h = scl_v = 0x01000000;

	nxres = (vxres - offset_x * 2) * 0x1000 / (scl_h/0x1000);
	nyres = (vyres - offset_y * 2) * 0x1000 / (scl_v/0x1000) + black_top * 2;


	printk("virtual resolution: %d x %d\n", vxres, vyres);
	printk("offset: x=%d, y=%d\n", offset_x, offset_y);
	printk("black: %d %d, %d %d\n",
		gfx[0x44/4], gfx[0x48/4], gfx[0x4c/4], gfx[0x50/4]);

	printk("native resolution: %d x %d\n", nxres, nyres);

	screen_info.lfb_depth = 32;
	screen_info.lfb_size = DEFAULT_FB_MEM / 0x10000;
	screen_info.pages=1;
	screen_info.blue_size = 8;
	screen_info.blue_pos = 24;
	screen_info.green_size = 8;
	screen_info.green_pos = 16;
	screen_info.red_size = 8;
	screen_info.red_pos = 8;
	screen_info.rsvd_size = 8;
	screen_info.rsvd_pos = 0;

	gfx[0x44/4] = 0; // disable black bar
	gfx[0x48/4] = 0;
	gfx[0x4c/4] = 0;
	gfx[0x50/4] = 0;

	gfx[0x590/4] = 0; // disable scaling
	gfx[0x584/4] = (nxres << 16) | nyres;
	gfx[0x580/4] = 0; // disable offset
	gfx[0x5e8/4] = (nxres * 4) / 0x10 - 1; // fix pitch
	gfx[0x134/4] = nxres;
	gfx[0x138/4] = nyres;

	ai->base &= ~0xFFFF; // page-align.

	screen_info.lfb_base = ai->base;
	screen_info.lfb_width = ai->width;
	screen_info.lfb_height = ai->height;
	screen_info.lfb_linelength = screen_info.lfb_width * screen_info.lfb_depth/4;

	gfx[0x120/4] = screen_info.lfb_linelength / 8; /* fixup pitch, in case we switched resolution */

	printk(KERN_INFO "xenonfb: detected %dx%d framebuffer @ 0x%08x\n", screen_info.lfb_width, screen_info.lfb_height, screen_info.lfb_base);

	iounmap(gfx);

	xenonfb_fix.smem_start = screen_info.lfb_base;
	xenonfb_defined.bits_per_pixel = screen_info.lfb_depth;
	xenonfb_defined.xres = screen_info.lfb_width;
	xenonfb_defined.yres = screen_info.lfb_height;
	xenonfb_defined.xoffset = 0;
	xenonfb_defined.yoffset = 0;
	xenonfb_fix.line_length = screen_info.lfb_linelength;

	/*   size_vmode -- that is the amount of memory needed for the
	 *                 used video mode, i.e. the minimum amount of
	 *                 memory we need. */
	size_vmode = xenonfb_defined.yres * xenonfb_fix.line_length;

	/*   size_total -- all video memory we have. Used for
	 *                 entries, ressource allocation and bounds
	 *                 checking. */
	size_total = screen_info.lfb_size * 65536;
	if (size_total < size_vmode)
		size_total = size_vmode;

	/*   size_remap -- the amount of video memory we are going to
	 *                 use for xenonfb.  With modern cards it is no
	 *                 option to simply use size_total as that
	 *                 wastes plenty of kernel address space. */
	size_remap  = size_vmode * 2;
	if (size_remap < size_vmode)
		size_remap = size_vmode;
	if (size_remap > size_total)
		size_remap = size_total;
	xenonfb_fix.smem_len = size_remap;

	if (!request_mem_region(xenonfb_fix.smem_start, size_total, "xenonfb")) {
		printk(KERN_WARNING
		       "xenonfb: cannot reserve video memory at 0x%lx\n",
			xenonfb_fix.smem_start);
		/* We cannot make this fatal. Sometimes this comes from magic
		   spaces our resource handlers simply don't know about */
	}

	info = framebuffer_alloc(sizeof(u32) * 16, &dev->dev);
	if (!info) {
		err = -ENOMEM;
		goto err_release_mem;
	}
	info->pseudo_palette = info->par;
	info->par = NULL;

	info->screen_base = ioremap(xenonfb_fix.smem_start, xenonfb_fix.smem_len);
	if (!info->screen_base) {
		printk(KERN_ERR "xenonfb: abort, cannot ioremap video memory "
				"0x%x @ 0x%lx\n",
			xenonfb_fix.smem_len, xenonfb_fix.smem_start);
		err = -EIO;
		goto err_unmap;
	}

	printk(KERN_INFO "xenonfb: framebuffer at 0x%lx, mapped to 0x%p, "
	       "using %dk, total %dk\n",
	       xenonfb_fix.smem_start, info->screen_base,
	       size_remap/1024, size_total/1024);
	printk(KERN_INFO "xenonfb: mode is %dx%dx%d, linelength=%d, pages=%d\n",
	       xenonfb_defined.xres, xenonfb_defined.yres,
	       xenonfb_defined.bits_per_pixel, xenonfb_fix.line_length,
	       screen_info.pages);

	xenonfb_defined.xres_virtual = xenonfb_defined.xres;
	xenonfb_defined.yres_virtual = xenonfb_fix.smem_len /
					xenonfb_fix.line_length;
	printk(KERN_INFO "xenonfb: scrolling: redraw\n");
	xenonfb_defined.yres_virtual = xenonfb_defined.yres;

	/* some dummy values for timing to make fbset happy */
	xenonfb_defined.pixclock     = 10000000 / xenonfb_defined.xres *
					1000 / xenonfb_defined.yres;
	xenonfb_defined.left_margin  = (xenonfb_defined.xres / 8) & 0xf8;
	xenonfb_defined.hsync_len    = (xenonfb_defined.xres / 8) & 0xf8;

	printk(KERN_INFO "xenonfb: pixclk=%ld left=%02x hsync=%02x\n",
		(unsigned long)xenonfb_defined.pixclock,
		xenonfb_defined.left_margin,
		xenonfb_defined.hsync_len);

	xenonfb_defined.red.offset    = screen_info.red_pos;
	xenonfb_defined.red.length    = screen_info.red_size;
	xenonfb_defined.green.offset  = screen_info.green_pos;
	xenonfb_defined.green.length  = screen_info.green_size;
	xenonfb_defined.blue.offset   = screen_info.blue_pos;
	xenonfb_defined.blue.length   = screen_info.blue_size;
	xenonfb_defined.transp.offset = screen_info.rsvd_pos;
	xenonfb_defined.transp.length = screen_info.rsvd_size;

	printk(KERN_INFO "xenonfb: %s: "
	       "size=%d:%d:%d:%d, shift=%d:%d:%d:%d\n",
	       "Truecolor",
	       screen_info.rsvd_size,
	       screen_info.red_size,
	       screen_info.green_size,
	       screen_info.blue_size,
	       screen_info.rsvd_pos,
	       screen_info.red_pos,
	       screen_info.green_pos,
	       screen_info.blue_pos);

	xenonfb_fix.ypanstep  = 0;
	xenonfb_fix.ywrapstep = 0;

	/* request failure does not faze us, as vgacon probably has this
	 * region already (FIXME) */
	request_region(0x3c0, 32, "xenonfb");

	info->fbops = &xenonfb_ops;
	info->var = xenonfb_defined;
	info->fix = xenonfb_fix;
	info->flags = FBINFO_FLAG_DEFAULT;

	if (fb_alloc_cmap(&info->cmap, 256, 0) < 0) {
		err = -ENOMEM;
		goto err_unmap;
	}
	if (register_framebuffer(info)<0) {
		err = -EINVAL;
		goto err_fb_dealoc;
	}
	printk(KERN_INFO "fb%d: %s frame buffer device\n",
	       info->node, info->fix.id);
	return 0;

err_fb_dealoc:
	fb_dealloc_cmap(&info->cmap);
err_unmap:
	iounmap(info->screen_base);
	framebuffer_release(info);
err_release_mem:
	release_mem_region(xenonfb_fix.smem_start, size_total);
	return err;
}

static struct platform_driver xenonfb_driver = {
	.probe	= xenonfb_probe,
	.driver	= {
		.name	= "xenonfb",
	},
};

static struct platform_device xenonfb_device = {
	.name	= "xenonfb",
};

static int __init xenonfb_init(void)
{
	int ret;

	ret = platform_driver_register(&xenonfb_driver);

	if (!ret) {
		ret = platform_device_register(&xenonfb_device);
		if (ret)
			platform_driver_unregister(&xenonfb_driver);
	}
	return ret;
}
module_init(xenonfb_init);

MODULE_LICENSE("GPL");

