/* Copyright (C) 2002 the late Joey Shutup.
 *
 * http://www.streetmap.co.uk/streetmap.dll?postcode2map?BS24+9TZ
 *
 * No guarantees or warranties or anything are provided or implied in any way
 * whatsoever.  Use this program at your own risk.  Permission to use this
 * program for any purpose is given, as long as the copyright is kept intact.
 */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* ---------------------------- included header files ----------------------- */

#include "config.h"

#include "libs/fvwmlib.h"
#include "libs/PictureBase.h"
#include "colorset.h"
#include "externs.h"
#include "fvwm.h"
#include "cursor.h"
#include "functions.h"
#include "misc.h"
#include "screen.h"
#include "module_interface.h"
#include "commands.h"
#include "libs/FShape.h"
#include "libs/Picture.h"
#include "libs/PictureUtils.h"
#include "libs/PictureGraphics.h"
#include "libs/FRenderInit.h"
#include "libs/Strings.h"

/* ---------------------------- local definitions --------------------------- */

/* ---------------------------- local macros -------------------------------- */

/* ---------------------------- imports ------------------------------------- */

extern int nColorsets;  /* in libs/Colorset.c */

/* ---------------------------- included code files ------------------------- */

/* ---------------------------- local types --------------------------------- */

/* When fvwm destroys pixmaps it puts them on a list and only destroys them
 * after some period of inactivity. This is necessary because changing colorset
 * options rapidly may result in a module redrawing itself due to the first
 * change while the second change is happening. If the module renders something
 * with the colorset affected by the second change there is a chance it may
 * reference pixmaps that FvwmTheme has destroyed, bad things would happen */
struct junklist
{
	struct junklist *prev;
	Pixmap pixmap;
};

/* ---------------------------- forward declarations ------------------------ */

/* ---------------------------- local variables ----------------------------- */

static char *black = "black";
static char *white = "white";
static char *gray = "gray";

static struct junklist *junk = NULL;
static Bool cleanup_scheduled = False;

static char *csetopts[] =
{
	"Foreground",
	"Fore",
	"fg",

	"Background",
	"Back",
	"bg",

	"Hilight",
	"Hilite",
	"hi",

	"Shadow",
	"Shade",
	"sh",

	"fgsh",
	"fg_alpha",
	"fgAlpha",

	/* these strings are used inside the cases in the switch below! */
	"Pixmap",
	"TiledPixmap",
	"AspectPixmap",

	/* these strings are used inside the cases in the switch below! */
	"Shape",
	"TiledShape",
	"AspectShape",

	/* switch off pixmaps and gradients */
	"Plain",
	/* switch off shape */
	"NoShape",

	/* Make the background transparent, copies the root window background */
	"Transparent",

	/* tint for the Pixmap or the gradient */
	"Tint",
	"PixmapTint",  /* ~ Tint */
	"ImageTint",   /* ~ Tint */
	"TintMask",    /* ~ Tint (backward compatibility) */
	"NoTint",      /* ~ Tint without argument */

	"fgTint",
	"bgTint",

	/* Dither the Pixmap or the gradient */
	"Dither",
	"NoDither",

	/* alpha for the Pixmap or the gradient */
	"Alpha",
	"PixmapAlpha",  /* ~ Alpha */
	"ImageAlpha",   /* ~ Alpha */

	/* Icon stuff */
	"DitherIcon",
	"NoDitherIcon",
	"IconTint",
	"NoIconTint",
	"IconAlpha",

	NULL
};

/* ---------------------------- exported variables (globals) ---------------- */

/* ---------------------------- local functions ----------------------------- */

static void add_to_junk(Pixmap pixmap)
{
	struct junklist *oldjunk = junk;

	junk = (struct junklist *)safemalloc(sizeof(struct junklist));
	junk->prev = oldjunk;
	junk->pixmap = pixmap;
	if (!cleanup_scheduled)
	{
		CMD_Schedule(
			NULL, NULL, 0, NULL, 0, "3000 CleanupColorsets", NULL);
		cleanup_scheduled = True;
	}

	return;
}

static char *get_simple_color(
	char *string, char **color, colorset_struct *cs, int supplied_color,
	int special_flag, char *special_string)
{
	char *rest;

	if (*color)
	{
		free(*color);
		*color = NULL;
	}
	rest = GetNextToken(string, color);
	if (*color)
	{
		if (special_string && StrEquals(*color, special_string))
		{
			free(*color);
			*color = NULL;
			cs->color_flags |= special_flag;
			cs->color_flags &= ~supplied_color;
		}
		else
		{
			cs->color_flags |= supplied_color;
			cs->color_flags &= ~special_flag;
		}
	}
	else
	{
		cs->color_flags &= ~(supplied_color | special_flag);
	}

	return rest;
}

static void SafeDestroyPicture(Display *dpy, FvwmPicture *picture)
{
	/* have to subvert destroy picture so that it doesn't free pixmaps,
	 * these are added to the junk list to be cleaned up after a timeout */
	if (picture->count < 2)
	{
		if (picture->picture)
		{
			add_to_junk(picture->picture);
			picture->picture = None;
		}
		if (picture->mask)
		{
			add_to_junk(picture->mask);
			picture->mask = None;
		}
		if (picture->alpha)
		{
			add_to_junk(picture->alpha);
			picture->alpha = None;
		}
	}
	/* all that this will now do is free the colors and the name */
	PDestroyFvwmPicture(dpy, picture);

	return;
}

static void free_colorset_background(colorset_struct *cs, Bool do_free_args)
{
	if (cs->picture != NULL)
	{
		if (cs->picture->picture != cs->pixmap)
		{
			add_to_junk(cs->pixmap);
		}
		SafeDestroyPicture(dpy, cs->picture);
		cs->picture = NULL;
		cs->pixmap = None;
		cs->alpha_pixmap = None; /* alaways equal to picture->alpha */
	}
	if (cs->pixmap && cs->pixmap != ParentRelative)
	{
		add_to_junk(cs->pixmap);
	}
	cs->pixmap = None;
	if (cs->mask)
	{
		add_to_junk(cs->mask);
		cs->mask = None;
	}
	if (cs->alpha_pixmap)
	{
		add_to_junk(cs->alpha_pixmap);
		cs->alpha_pixmap = None;
	}
	if (cs->pixels && cs->nalloc_pixels)
	{
		PictureFreeColors(
			dpy, Pcmap, cs->pixels, cs->nalloc_pixels, 0, False);
		free(cs->pixels);
		cs->pixels = NULL;
		cs->nalloc_pixels = 0;
	}
	if (do_free_args)
	{
		if (cs->pixmap_args != NULL)
		{
			free(cs->pixmap_args);
			cs->pixmap_args = NULL;
		}
		if (cs->gradient_args != NULL)
		{
			free(cs->gradient_args);
			cs->gradient_args = NULL;
		}
	}
}

static void reset_cs_pixmap(colorset_struct *cs, GC gc)
{
	if (Pdepth == cs->picture->depth)
	{
		XCopyArea(dpy, cs->picture->picture, cs->pixmap, gc,
			  0, 0, cs->width, cs->height, 0, 0);
	}
	else
	{
		XCopyPlane(dpy, cs->picture->picture, cs->pixmap, gc,
			   0, 0, cs->width, cs->height, 0, 0, 1);
	}

	return;
}

static void parse_pixmap(
	Window win, GC gc, colorset_struct *cs, Bool *pixmap_is_a_bitmap)
{
	static char *name = "parse_colorset(pixmap)";
	FvwmPictureAttributes fpa;

	
	/* dither */
	fpa.mask = 0;
	if (cs->dither && Pdepth <= 16)
	{
		fpa.mask = FPAM_DITHER;
	}

	/* read filename */
	if (!cs->pixmap_args)
	{
		return;
	}
	/* load the file */
	cs->picture = PCacheFvwmPicture(dpy, win, NULL, cs->pixmap_args, fpa);
	if (cs->picture == NULL)
	{
		fvwm_msg(ERR, name, "can't load picture %s", cs->pixmap_args);
		return;
	}
	if (cs->picture->depth != Pdepth)
	{
		*pixmap_is_a_bitmap = True;
		cs->pixmap = None; /* build cs->pixmap later */
	}
	/* copy the picture pixmap into the public structure */
	cs->width = cs->picture->width;
	cs->height = cs->picture->height;
	cs->alpha_pixmap = cs->picture->alpha;

	if (!*pixmap_is_a_bitmap)
	{
		cs->pixmap = XCreatePixmap(dpy, win,
					   cs->width, cs->height,
					   Pdepth);
		XSetClipMask(dpy, gc, cs->picture->mask);
		XCopyArea(dpy, cs->picture->picture, cs->pixmap, gc,
			  0, 0, cs->width, cs->height, 0, 0);
		XSetClipMask(dpy, gc, None);
	}
	if (cs->pixmap)
	{
		if (cs->picture->mask != None)
		{
			/* make an inverted copy of the mask */
			cs->mask = XCreatePixmap(dpy, win,
						 cs->width, cs->height,
						 1);
			if (cs->mask)
			{
				XCopyArea(dpy, cs->picture->mask, cs->mask,
					  Scr.MonoGC,
					  0, 0, cs->width, cs->height, 0, 0);
				/* Invert the mask. We use it to draw the
				 * background. */
				XSetFunction(dpy, Scr.MonoGC, GXinvert);
				XFillRectangle(dpy, cs->mask, Scr.MonoGC,
					       0, 0, cs->width, cs->height);
				XSetFunction(dpy, Scr.MonoGC, GXcopy);
			}
		}
	}
}

static void parse_shape(Window win, colorset_struct *cs, int i, char *args,
			int *has_shape_changed)
{
	char *token;
	static char *name = "parse_colorset(shape)";
	FvwmPicture *picture;
	FvwmPictureAttributes fpa;

	if (!FHaveShapeExtension)
	{
		cs->shape_mask = None;
		return;
	}

	/* read filename */
	token = PeekToken(args, &args);
	*has_shape_changed = True;
	if (cs->shape_mask)
	{
		add_to_junk(cs->shape_mask);
		cs->shape_mask = None;
	}

	/* set the flags */
	if (csetopts[i][0] == 'T')
	{
		cs->shape_type = SHAPE_TILED;
	}
	else if (csetopts[i][0] == 'A')
	{
		cs->shape_type = SHAPE_STRETCH_ASPECT;
	}
	else
	{
		cs->shape_type = SHAPE_STRETCH;
	}
	fpa.mask = 0;

	/* try to load the shape mask */
	if (!token)
	{
		return;
	}

	/* load the shape mask */
	picture = PCacheFvwmPicture(dpy, win, NULL, token, fpa);
	if (!picture)
	{
		fvwm_msg(ERR, name, "can't load picture %s", token);
	}
	else if (picture->depth != 1 && picture->mask == None)
	{
		fvwm_msg(ERR, name, "shape pixmap must be of depth 1");
		SafeDestroyPicture(dpy, picture);
	}
	else
	{
		Pixmap mask;

		/* okay, we have what we want */
		if (picture->mask != None)
		{
			mask = picture->mask;
		}
		else
		{
			mask = picture->picture;
		}
		cs->shape_width = picture->width;
		cs->shape_height = picture->height;

		if (mask != None)
		{
			cs->shape_mask = XCreatePixmap(
				dpy, mask, picture->width, picture->height, 1);
			if (cs->shape_mask != None)
			{
				XCopyPlane(dpy, mask, cs->shape_mask,
					   Scr.MonoGC, 0, 0, picture->width,
					   picture->height, 0, 0, 1);
			}
		}
	}
	if (picture)
	{
		SafeDestroyPicture(dpy, picture);
		picture = None;
	}
	return;
}

static void parse_pixmap_tint(
	Window win, GC gc, colorset_struct *cs, int i, char *args,
	char **tint, int *has_tint_changed, int *has_pixmap_changed)
{
	char *rest;
	static char *name = "parse_colorset(tint)";
	int tint_percent;

	rest = get_simple_color(args, tint, cs, TINT_SUPPLIED, 0, NULL);
	if (!GetIntegerArguments(rest, NULL, &tint_percent, 1))
	{
		fvwm_msg(WARN, name,
			 "Tint must have two arguments a color and an integer");
		tint_percent = -1;
		return;
	}
	*has_tint_changed = True;
	if (!*has_pixmap_changed && cs->picture != NULL && cs->picture->picture)
	{
		XSetClipMask(dpy, gc, cs->picture->mask);
		XCopyArea(dpy, cs->picture->picture, cs->pixmap, gc,
			  0, 0, cs->width, cs->height, 0, 0);
		XSetClipMask(dpy, gc, None);
		*has_pixmap_changed = True;
	}
	cs->tint_percent = (tint_percent > 100)? 100:tint_percent;
}

static void parse_simple_tint(
	colorset_struct *cs, char *args, char **tint, int supplied_color,
	int *has_tint_changed, int *percent)
{
	char *rest;
	static char *name = "parse_colorset (tint)";

	rest = get_simple_color(args, tint, cs, supplied_color, 0, NULL);
	if (!(cs->color_flags & supplied_color))
	{
		/* restore to default */
		*percent = 0;
		*has_tint_changed = True;
		cs->color_flags &= ~(supplied_color);
		return;
	}
	if (!GetIntegerArguments(rest, NULL, percent, 1))
	{
		fvwm_msg(WARN, name,
			 "Tint must have two arguments a color and an integer");
		return;
	}
	*has_tint_changed = True;
	if (*percent > 100)
	{
		*percent = 100;
	}
	else if (*percent < 0)
	{
		*percent = 0;
	}
}

/* ---------------------------- interface functions ------------------------- */

void cleanup_colorsets()
{
	struct junklist *oldjunk = junk;

	while (junk)
	{
		XFreePixmap(dpy, junk->pixmap);
		oldjunk = junk;
		junk = junk->prev;
		free(oldjunk);
	}
	cleanup_scheduled = False;
}

/* translate a colorset spec into a colorset structure */
void parse_colorset(int n, char *line)
{
	int i;
	int w;
	int h;
	int tmp;
	int percent;
	colorset_struct *cs;
	char *optstring;
	char *args;
	char *option;
	char *tmp_str;
	char *fg = NULL;
	char *bg = NULL;
	char *hi = NULL;
	char *sh = NULL;
	char *fgsh = NULL;
	char *tint = NULL;
	char *fg_tint = NULL;
	char *bg_tint = NULL;
	char *icon_tint = NULL;
	Bool have_pixels_changed = False;
	Bool has_icon_pixels_changed = False;
	Bool has_fg_changed = False;
	Bool has_bg_changed = False;
	Bool has_sh_changed = False;
	Bool has_hi_changed = False;
	Bool has_fgsh_changed = False;
	Bool has_fg_alpha_changed = False;
	Bool has_tint_changed = False;
	Bool has_fg_tint_changed = False;
	Bool has_bg_tint_changed = False;
	Bool has_icon_tint_changed = False;
	Bool has_pixmap_changed = False;
	Bool has_shape_changed = False;
	Bool has_image_alpha_changed = False;
	Bool pixmap_is_a_bitmap = False;
	Bool do_reload_pixmap = False;
	XColor color;
	XGCValues xgcv;
	static char *name = "parse_colorset";
	Window win = Scr.NoFocusWin;
	static GC gc = None;

	/* initialize statics */
	if (gc == None)
	{
		gc = fvwmlib_XCreateGC(dpy, win, 0, &xgcv);
	}

	/* make sure it exists and has sensible contents */
	alloc_colorset(n);
	cs = &Colorset[n];

	/* ---------- Parse the options ---------- */
	while (line && *line)
	{
		/* Read next option specification delimited by a comma or \0. */
		line = GetQuotedString(line, &optstring, ",", NULL, NULL, NULL);
		if (!optstring)
			break;
		args = GetNextToken(optstring, &option);
		if (!option)
		{
			free(optstring);
			break;
		}

		switch((i = GetTokenIndex(option, csetopts, 0, NULL)))
		{
		case 0: /* Foreground */
		case 1: /* Fore */
		case 2: /* fg */
			get_simple_color(
				args, &fg, cs, FG_SUPPLIED, FG_CONTRAST,
				"contrast");
			has_fg_changed = True;
			break;
		case 3: /* Background */
		case 4: /* Back */
		case 5: /* bg */
			get_simple_color(
				args, &bg, cs, BG_SUPPLIED, BG_AVERAGE,
				"average");
			has_bg_changed = True;
			break;
		case 6: /* Hilight */
		case 7: /* Hilite */
		case 8: /* hi */
			get_simple_color(args, &hi, cs, HI_SUPPLIED, 0, NULL);
			has_hi_changed = True;
			break;
		case 9: /* Shadow */
		case 10: /* Shade */
		case 11: /* sh */
			get_simple_color(args, &sh, cs, SH_SUPPLIED, 0, NULL);
			has_sh_changed = True;
			break;
		case 12: /* fgsh */
			get_simple_color(args, &fgsh, cs, FGSH_SUPPLIED, 0,NULL);
			has_fgsh_changed = True;
			break;
		case 13: /* fg_alpha */
		case 14: /* fgAlpha */
			if (GetIntegerArguments(args, NULL, &tmp, 1))
			{
				if (tmp > 100)
					tmp = 100;
				else if (tmp < 0)
					tmp = 0;
			}
			else
			{
				tmp = 100;
			}
			if (tmp != cs->fg_alpha)
			{
				cs->fg_alpha = tmp; 
				has_fg_alpha_changed = True;
			}
			break;
		case 15: /* TiledPixmap */
		case 16: /* Pixmap */
		case 17: /* AspectPixmap */
			has_pixmap_changed = True;
			free_colorset_background(cs, True);
			tmp_str = PeekToken(args, &args);
			if (tmp_str)
			{
				CopyString(&cs->pixmap_args, tmp_str);
				do_reload_pixmap = True;
				cs->gradient_type = 0;
				/* set the flags */
				if (csetopts[i][0] == 'T')
				{
					cs->pixmap_type = PIXMAP_TILED;
				}
				else if (csetopts[i][0] == 'A')
				{
					cs->pixmap_type = PIXMAP_STRETCH_ASPECT;
				}
				else
				{
					cs->pixmap_type = PIXMAP_STRETCH;
				}
			}
			/* the pixmap is build later */
			break;
		case 18: /* Shape */
		case 19: /* TiledShape */
		case 20: /* AspectShape */
			parse_shape(win, cs, i, args, &has_shape_changed);
			break;
		case 21: /* Plain */
			has_pixmap_changed = True;
			pixmap_is_a_bitmap = False;
			free_colorset_background(cs, True);
			break;
		case 22: /* NoShape */
			has_shape_changed = True;
			if (cs->shape_mask)
			{
				add_to_junk(cs->shape_mask);
				cs->shape_mask = None;
			}
			break;
		case 23: /* Transparent */

			/* This is only allowable when the root depth == fvwm
			 * visual depth otherwise bad match errors happen,
			 * it may be even more restrictive but my tests (on
			 * exceed 6.2) show that only == depth is necessary */

			if (Pdepth != DefaultDepth(dpy, (DefaultScreen(dpy))))
			{
				fvwm_msg(
					ERR, name, "can't do Transparent "
					"when root_depth!=fvwm_depth");
				break;
			}
			has_pixmap_changed = True;
			free_colorset_background(cs, True);
			cs->pixmap = ParentRelative;
			cs->pixmap_type = PIXMAP_STRETCH;
			break;
		case 24: /* Tint */
		case 25: /* PixmapTint */
		case 26: /* ImageTint */
		case 27: /* TintMask */
			parse_pixmap_tint(
				win, gc, cs, i, args, &tint,
				&has_tint_changed, &has_pixmap_changed);
			break;
		case 28: /* NoTint */
			has_pixmap_changed = True;
			/* restore the pixmap */
			if (cs->picture != NULL && cs->pixmap)
			{
				XSetClipMask(dpy, gc, cs->picture->mask);
				reset_cs_pixmap(cs, gc);
				XSetClipMask(dpy, gc, None);
			}
			cs->tint_percent = -1;
			cs->color_flags &= ~TINT_SUPPLIED;
			break;
		case 29: /* fgTint */
			parse_simple_tint(
				cs, args, &fg_tint, FG_TINT_SUPPLIED,
				&has_fg_tint_changed, &percent);
			cs->fg_tint_percent = percent;
			break;
		case 30: /* bgTint */
			parse_simple_tint(
				cs, args, &bg_tint, BG_TINT_SUPPLIED,
				&has_bg_tint_changed, &percent);
			cs->bg_tint_percent = percent;
			break;	
		case 31: /* dither */
			if (cs->pixmap_args || cs->gradient_args)
			{
				has_pixmap_changed = True;
				do_reload_pixmap = True;
				free_colorset_background(cs, False);
			}
			cs->dither = True;
			break;
		case 32: /* nodither */
			if (cs->pixmap_args || cs->gradient_args)
			{
				has_pixmap_changed = True;
				do_reload_pixmap = True;
				free_colorset_background(cs, False);
			}
			cs->dither = False;
			break;
		case 33: /* Alpha */
		case 34: /* PixmapAlpha */
		case 35: /* ImageAlpha */
			if (GetIntegerArguments(args, NULL, &tmp, 1))
			{
				if (tmp > 100)
					tmp = 100;
				else if (tmp < 0)
					tmp = 0;
			}
			else
			{
				tmp = 100;
			}
			if (tmp != cs->image_alpha_percent)
			{
				has_image_alpha_changed = True;
				cs->image_alpha_percent = tmp;
			}
			break;
                /* dither icon is not dynamic (yet) maybe a bad opt: default
		 * to False ? */
		case 36: /* ditherIcon */
			cs->do_dither_icon = True;
			break;
		case 37: /* DoNotDitherIcon */
			cs->do_dither_icon = False;
			break;
		case 38: /* IconTint */
			parse_simple_tint(
				cs, args, &icon_tint, ICON_TINT_SUPPLIED,
				&has_icon_tint_changed, &percent);
			if (has_icon_tint_changed && 
			    percent != cs->icon_tint_percent)
			{
				cs->icon_tint_percent = percent;
				has_icon_pixels_changed = True;
			}
			break;
		case 39: /* NoIconTint */
			has_icon_tint_changed = True;
			if (cs->icon_tint_percent != 0)
			{
				has_icon_pixels_changed = True;
			}
			cs->icon_tint_percent = 0;
			break;
		case 40: /* IconAlpha */
			if (GetIntegerArguments(args, NULL, &tmp, 1))
			{
				if (tmp > 100)
					tmp = 100;
				else if (tmp < 0)
					tmp = 0;
			}
			else
			{
				tmp = 100;
			}
			if (tmp != cs->icon_alpha)
			{
				has_icon_pixels_changed = True;
				cs->icon_alpha = tmp;
			}
			break;
		default:
			/* test for ?Gradient */
			if (option[0] && StrEquals(&option[1], "Gradient"))
			{
				cs->gradient_type = toupper(option[0]);
				if (!IsGradientTypeSupported(cs->gradient_type))
					break;
				has_pixmap_changed = True;
				free_colorset_background(cs, True);
				CopyString(&cs->gradient_args, args);
				do_reload_pixmap = True;
				if (cs->gradient_type == V_GRADIENT)
				{
					cs->pixmap_type = PIXMAP_STRETCH_Y;
				}
				else if (cs->gradient_type == H_GRADIENT)
					cs->pixmap_type = PIXMAP_STRETCH_X;
				else
					cs->pixmap_type = PIXMAP_STRETCH;
			}
			else
			{
				fvwm_msg(
					WARN, name, "bad colorset pixmap "
					"specifier %s %s", option, line);
			}
			break;
		} /* switch */

		if (option)
		{
			free(option);
			option = NULL;
		}
		free(optstring);
		optstring = NULL;
	} /* while (line && *line) */

	/*
	 * ---------- change the "pixmap" tint colour ----------
	 */
	if (has_tint_changed)
	{
		/* user specified colour */
		if (tint != NULL)
		{
			Pixel old_tint = cs->tint;
			PictureFreeColors(dpy, Pcmap, &cs->tint, 1, 0, True);
			cs->tint = GetColor(tint);
			if (old_tint != cs->tint)
			{
				have_pixels_changed = True;
			}
		}
		else if (tint == NULL)
		{
			/* default */
			Pixel old_tint = cs->tint;
			PictureFreeColors(dpy, Pcmap, &cs->tint, 1, 0, True);
			cs->tint = GetColor(black);
			if (old_tint != cs->tint)
			{
				have_pixels_changed = True;
			}
		}
	}

	/*
	 * reload the gradient if the tint or the alpha have changed.
	 * Do this too if we need to recompute the bg average and the
	 * gradient is tinted (perforemence issue).
	 */
	if ((has_tint_changed || has_image_alpha_changed ||
	     (has_bg_changed && (cs->color_flags & BG_AVERAGE) &&
	      cs->tint_percent > 0)) && cs->gradient_args)
	{
		do_reload_pixmap = True;
	}

	/*
	 * build the pixmap or the gradient
	 */
	if (do_reload_pixmap)
	{
		if (cs->pixmap_args)
		{
			parse_pixmap(win, gc, cs, &pixmap_is_a_bitmap);
		}
		else if (cs->gradient_args)
		{
			cs->pixmap = CreateGradientPixmapFromString(
				dpy, win, gc, cs->gradient_type,
				cs->gradient_args, &w, &h, &cs->pixels,
				&cs->nalloc_pixels, cs->dither);
			cs->width = w;
			cs->height = h;
		}
		has_pixmap_changed = True;
	}

	if (cs->picture != NULL && cs->picture->depth != Pdepth)
	{
		pixmap_is_a_bitmap = True;
	}

	/*
	 * ---------- change the background colour ----------
	 */
	if (has_bg_changed || has_pixmap_changed)
	{
		Bool do_set_default_background = False;

		/* note: no average for bitmap */
		if ((cs->color_flags & BG_AVERAGE) &&
		    cs->pixmap != None &&
		    cs->pixmap != ParentRelative &&
		    !pixmap_is_a_bitmap)
		{
			/* calculate average background color */
			XColor *colors;
			XImage *image;
			XImage *mask_image = None;
			unsigned int i, j, k = 0;
			unsigned long red = 0, blue = 0, green = 0;
			unsigned long tred, tblue, tgreen;
			double dred = 0.0, dblue = 0.0, dgreen = 0.0;
			Pixmap pix;

			/* chose the good pixmap */
			pix =
			   (cs->picture != NULL &&
			    cs->picture->picture != None) ?
				cs->picture->picture : cs->pixmap;
			/* create an array to store all the pixmap colors in */
			colors = (XColor *)safemalloc(
				cs->width * cs->height * sizeof(XColor));
			/* get the pixmap and mask into an image */
			image = XGetImage(
				dpy, pix, 0, 0, cs->width, cs->height,
				AllPlanes, ZPixmap);
			if (cs->mask != None)
			{
				mask_image = XGetImage(
					dpy, cs->mask, 0, 0, cs->width,
					cs->height, AllPlanes, ZPixmap);
			}
			/* only fetch the pixels that are not masked out */
			for (i = 0; i < cs->width; i++)
			{
				for (j = 0; j < cs->height; j++)
				{
					if ((cs->mask == None) || (XGetPixel(
						mask_image, i, j) == 0))
					{
						colors[k++].pixel =
							XGetPixel(image, i, j);
					}
				}
			}

			XDestroyImage(image);
			if (mask_image != None)
			{
				XDestroyImage(mask_image);
			}
			if (k == 0)
			{
				do_set_default_background = True;
			}
			else
			{
				/* look them all up, XQueryColors() can't
				 * handle more than 256 */
				for (i = 0; i < k; i += 256)
				{
					XQueryColors(
						dpy, Pcmap, &colors[i],
						min(k - i, 256));
				}
				/* calculate average, add overflows in a double
				 * .red is short, red is long */
				for (i = 0; i < k; i++)
				{
					tred = red;
					red += colors[i].red;
					if (red < tred)
					{
						dred += (double)tred;
						red = colors[i].red;
					}
					tgreen = green;
					green += colors[i].green;
					if (green < tgreen)
					{
						dgreen += (double)tgreen;
						green = colors[i].green;
					}
					tblue = blue;
					blue += colors[i].blue;
					if (blue < tblue)
					{
						dblue += (double)tblue;
						blue = colors[i].blue;
					}
				}
				dred += red;
				dgreen += green;
				dblue += blue;
				free(colors);
				/* get it */
				color.red = dred / k;
				color.green = dgreen / k;
				color.blue = dblue / k;
				{
					Pixel old_bg = cs->bg;
					
					PictureFreeColors(
						dpy, Pcmap, &cs->bg, 1, 0, True);
					PictureAllocColor(
						dpy, Pcmap, &color, True);
					cs->bg = color.pixel;
					if (old_bg != cs->bg)
					{
						have_pixels_changed = True;
					}
				}
			}
		} /* average */
		else if ((cs->color_flags & BG_SUPPLIED) && bg != NULL)
		{
			/* user specified colour */
			Pixel old_bg = cs->bg;

			PictureFreeColors(dpy, Pcmap, &cs->bg, 1, 0, True);
			cs->bg = GetColor(bg);
			if (old_bg != cs->bg)
			{
				have_pixels_changed = True;
			}
		} /* user specified */
		else if ((bg == NULL && has_bg_changed) ||
			 ((cs->color_flags & BG_AVERAGE) &&
			  (cs->pixmap == None || cs->pixmap == ParentRelative ||
			   pixmap_is_a_bitmap)))
		{
			/* default */
			do_set_default_background = True;
		} /* default */
		if (do_set_default_background)
		{
			Pixel old_bg = cs->bg;

			PictureFreeColors(dpy, Pcmap, &cs->bg, 1, 0, True);
			cs->bg = GetColor(white);
			if (old_bg != cs->bg)
			{
				have_pixels_changed = True;
			}
		}
		has_bg_changed = True;

		/* save the bg color for tinting */
		cs->bg_saved = cs->bg;
	} /* has_bg_changed */


	/*
	 * ---------- setup the bg tint colour ----------
	 */
	if (has_bg_tint_changed && cs->bg_tint_percent > 0 && bg_tint != NULL)
	{
		PictureFreeColors(dpy, Pcmap, &cs->bg_tint, 1, 0, True);
		cs->bg_tint = GetColor(bg_tint);
	}

	/*
	 * ---------- tint the bg colour ----------
	 */
	if (has_bg_tint_changed || (has_bg_changed && cs->bg_tint_percent > 0))
	{
		if (cs->bg_tint_percent == 0)
		{
			Pixel old_bg = cs->bg;

			PictureFreeColors(dpy, Pcmap, &cs->bg, 1, 0, True);
			cs->bg = cs->bg_saved;
			if (old_bg != cs->bg)
			{
				have_pixels_changed = True;
				has_bg_changed = True;
			}
		}
		else
		{
			Pixel old_bg = cs->bg;

			PictureFreeColors(dpy, Pcmap, &cs->bg, 1, 0, True);
			cs->bg = GetTintedPixel(
				cs->bg_saved, cs->bg_tint, cs->bg_tint_percent);
			if (old_bg != cs->bg)
			{
				have_pixels_changed = True;
				has_bg_changed = True;
			}
		}
	}

	/*
	 * ---------- setup the fg tint colour ----------
	 */
	if (has_fg_tint_changed && cs->fg_tint_percent > 0 && fg_tint != NULL)
	{
		PictureFreeColors(dpy, Pcmap, &cs->fg_tint, 1, 0, True);
		cs->fg_tint = GetColor(fg_tint);
	}

	/*
	 * ---------- change the foreground colour ----------
	 */
	if (has_fg_changed ||
	    (has_bg_changed && (cs->color_flags & FG_CONTRAST)))
	{
		if (cs->color_flags & FG_CONTRAST)
		{
			Pixel old_fg = cs->fg;

			/* calculate contrasting foreground color */
			color.pixel = cs->bg;
			XQueryColor(dpy, Pcmap, &color);
			color.red = (color.red > 32767) ? 0 : 65535;
			color.green = (color.green > 32767) ? 0 : 65535;
			color.blue = (color.blue > 32767) ? 0 : 65535;

			PictureFreeColors(dpy, Pcmap, &cs->fg, 1, 0, True);
			PictureAllocColor(dpy, Pcmap, &color, True);
			cs->fg = color.pixel;
			if (old_fg != cs->fg)
			{
				have_pixels_changed = True;
				has_fg_changed = 1;
			}
		} /* contrast */
		else if ((cs->color_flags & FG_SUPPLIED) && fg != NULL)
		{
			/* user specified colour */
			Pixel old_fg = cs->fg;

			PictureFreeColors(dpy, Pcmap, &cs->fg, 1, 0, True);
			cs->fg = GetColor(fg);
			if (old_fg != cs->fg)
			{
				have_pixels_changed = True;
				has_fg_changed = 1;
			}
		} /* user specified */
		else if (fg == NULL)
		{
			/* default */
			Pixel old_fg = cs->fg;

			PictureFreeColors(dpy, Pcmap, &cs->fg, 1, 0, True);
			cs->fg = GetColor(black);
			if (old_fg != cs->fg)
			{
				have_pixels_changed = True;
				has_fg_changed = 1;
			}
		}

		/* save the fg color for tinting */
		cs->fg_saved = cs->fg;

	} /* has_fg_changed */

	/*
	 * ---------- tint the foreground colour ----------
	 */
	if (has_fg_tint_changed || (has_fg_changed && cs->fg_tint_percent > 0))
	{
		if (cs->fg_tint_percent == 0)
		{
			
			Pixel old_fg = cs->fg;

			PictureFreeColors(dpy, Pcmap, &cs->fg, 1, 0, True);
			cs->fg = cs->fg_saved;
			if (old_fg != cs->fg)
			{
				have_pixels_changed = True;
				has_fg_changed = 1;
			}
		}
		else
		{
			Pixel old_fg = cs->fg;

			PictureFreeColors(dpy, Pcmap, &cs->fg, 1, 0, True);
			cs->fg = GetTintedPixel(
				cs->fg_saved, cs->fg_tint,
				cs->fg_tint_percent);
			if (old_fg != cs->fg)
			{
				have_pixels_changed = True;
				has_fg_changed = 1;
			}
		}
	}
	
	
	/*
	 * ---------- change the hilight colour ----------
	 */
	if (has_hi_changed || has_bg_changed)
	{
		has_hi_changed = 1;
		if ((cs->color_flags & HI_SUPPLIED) && hi != NULL)
		{
			/* user specified colour */
			Pixel old_hilite = cs->hilite;

			PictureFreeColors(dpy, Pcmap, &cs->hilite, 1, 0, True);
			cs->hilite = GetColor(hi);
			if (old_hilite != cs->hilite)
			{
					have_pixels_changed = True;
			}
		} /* user specified */
		else if (hi == NULL)
		{
			Pixel old_hilite = cs->hilite;

			PictureFreeColors(dpy, Pcmap, &cs->hilite, 1, 0, True);
			cs->hilite = GetHilite(cs->bg);
			if (old_hilite != cs->hilite)
			{
				have_pixels_changed = True;
			}
		}
	} /* has_hi_changed */

	/*
	 * ---------- change the shadow colour ----------
	 */
	if (has_sh_changed || has_bg_changed)
	{
		has_sh_changed = 1;
		if ((cs->color_flags & SH_SUPPLIED) && sh != NULL)
		{
			/* user specified colour */
			Pixel old_shadow = cs->shadow;

			PictureFreeColors(dpy, Pcmap, &cs->shadow, 1, 0, True);
			cs->shadow = GetColor(sh);
			if (old_shadow != cs->shadow)
			{
				have_pixels_changed = True;
			}
		} /* user specified */
		else if (sh == NULL)
		{
			Pixel old_shadow = cs->shadow;

			PictureFreeColors(dpy, Pcmap, &cs->shadow, 1, 0, True);
			cs->shadow = GetShadow(cs->bg);
			if (old_shadow != cs->shadow)
			{
				have_pixels_changed = True;
			}
		}
	} /* has_sh_changed */

	/*
	 * ---------- change the shadow foreground colour ----------
	 */
	if (has_fgsh_changed || has_fg_changed || has_bg_changed)
	{
		has_fgsh_changed = 1;
		if ((cs->color_flags & FGSH_SUPPLIED) && fgsh != NULL)
		{
			/* user specified colour */
			Pixel old_fgsh = cs->fgsh;

			PictureFreeColors(dpy, Pcmap, &cs->fgsh, 1, 0, True);
			cs->fgsh = GetColor(fgsh);
			if (old_fgsh != cs->fgsh)
			{
				have_pixels_changed = True;
			}
		} /* user specified */
		else if (fgsh == NULL)
		{
			Pixel old_fgsh = cs->fgsh;

			PictureFreeColors(dpy, Pcmap, &cs->fgsh, 1, 0, True);
			cs->fgsh = GetForeShadow(cs->fg, cs->bg);
			if (old_fgsh != cs->fgsh)
			{
				have_pixels_changed = True;
			}
		}
	} /* has_fgsh_changed */

	/*
	 * ------- the pixmap is a bitmap: create here cs->pixmap -------
	 */
	if (cs->picture != None && pixmap_is_a_bitmap &&
	    (has_pixmap_changed || has_bg_changed))
	{
		cs->pixmap = XCreatePixmap(
			dpy, win, cs->width, cs->height, Pdepth);
		XSetBackground(dpy, gc, cs->bg);
		XSetForeground(dpy, gc, cs->fg);
		reset_cs_pixmap(cs, gc);
	}

	/*
	 * ------- change the masked out parts of the background pixmap -------
	 */
	if (cs->pixmap != None && cs->pixmap != ParentRelative &&
	    (cs->mask != None || cs->alpha_pixmap != None ||
	     cs->image_alpha_percent < 100 || cs->tint_percent > 0) &&
	    (has_pixmap_changed || has_bg_changed || has_image_alpha_changed
	     || has_tint_changed))
	{
		/* Now that we know the background colour we can update the
		 * pixmap background. */
		FvwmRenderAttributes fra;
		Pixmap temp, mask, alpha;

		temp = XCreatePixmap(dpy, win, cs->width, cs->height, Pdepth);
		if (cs->picture != NULL)
		{
			mask = cs->picture->mask;
			alpha = cs->picture->alpha;
		}
		else
		{
			mask = None;
			alpha = None;
		}
		XSetForeground(dpy, gc, cs->bg);
		XFillRectangle(
			dpy, temp, gc, 0, 0, cs->width, cs->height);

		fra.mask = FRAM_HAVE_ADDED_ALPHA | FRAM_HAVE_TINT;
		fra.added_alpha_percent = cs->image_alpha_percent;
		fra.tint = cs->tint;
		fra.tint_percent = cs->tint_percent;
		PGraphicsRenderPixmaps(
			dpy, win, cs->pixmap, mask, alpha, Pdepth, &fra,
			temp, gc, Scr.MonoGC, None,
			0, 0, cs->width, cs->height,
			0, 0, cs->width, cs->height, False);
		add_to_junk(cs->pixmap);
		cs->pixmap = temp;
		has_pixmap_changed = True;
	} /* has_pixmap_changed */


	/*
	 * ---------- change the icon tint colour ----------
	 */
	if (has_icon_tint_changed)
	{
		/* user specified colour */
		if (icon_tint != NULL)
		{
			Pixel old_tint = cs->icon_tint;
			PictureFreeColors(
				dpy, Pcmap, &cs->icon_tint, 1, 0, True);
			cs->icon_tint = GetColor(icon_tint);
			if (old_tint != cs->icon_tint)
			{
				has_icon_pixels_changed = True;
			}
		}
		else
		{
			/* default */
			Pixel old_tint = cs->icon_tint;
			PictureFreeColors(
				dpy, Pcmap, &cs->icon_tint, 1, 0, True);
			cs->icon_tint = GetColor(black);
			if (old_tint != cs->icon_tint)
			{
				has_icon_pixels_changed = True;
			}
		}
	}

	/*
	 * ---------- send new colorset to fvwm and clean up ----------
	 */
	/* make sure the server has this to avoid races */
	XSync(dpy, False);

	/* inform modules of the change */
	if (have_pixels_changed || has_pixmap_changed || has_shape_changed ||
	    has_fg_alpha_changed || has_icon_pixels_changed)
	{
		BroadcastColorset(n);
	}

	if (fg)
	{
		free(fg);
	}
	if (bg)
	{
		free(bg);
	}
	if (hi)
	{
		free(hi);
	}
	if (sh)
	{
		free(sh);
	}
	if (fgsh)
	{
		free(fgsh);
	}
	if (tint)
	{
		free(tint);
	}
	if (fg_tint)
	{
		free(fg_tint);
	}
	if (bg_tint)
	{
		free(bg_tint);
	}
	if (icon_tint)
	{
		free(icon_tint);
	}
	return;
}

/*****************************************************************************
 * alloc_colorset() grows the size of the Colorset array to include set n
 * Colorset_struct *Colorset will be altered
 * returns the address of the member
 *****************************************************************************/
void alloc_colorset(int n)
{
	/* do nothing if it already exists */
	if (n < nColorsets)
	{
		return;
	}
	else
	{
		Colorset = (colorset_struct *)saferealloc(
			(char *)Colorset, (n + 1) * sizeof(colorset_struct));
		memset(
			&Colorset[nColorsets], 0,
			(n + 1 - nColorsets) * sizeof(colorset_struct));
	}

	/* initialize new colorsets to black on gray */
	while (nColorsets <= n)
	{
		colorset_struct *ncs = &Colorset[nColorsets];

		if (Pdepth < 2)
		{
			char g_bits[] = {0x0a, 0x05, 0x0a, 0x05,
					 0x08, 0x02, 0x08, 0x02,
					 0x01, 0x02, 0x04, 0x08};
			/* monochrome monitors get black on white */
			/* with a gray pixmap background */
			ncs->fg = GetColor(black);
			ncs->bg = GetColor(white);
			ncs->hilite = GetColor(white);
			ncs->shadow = GetColor(black);
			ncs->fgsh = GetColor(white);
			ncs->pixmap = XCreatePixmapFromBitmapData(
				dpy, Scr.NoFocusWin,
				&g_bits[4 * (nColorsets % 3)], 4, 4,
				BlackPixel(dpy, Scr.screen),
				WhitePixel(dpy, Scr.screen), Pdepth);
			ncs->width = 4;
			ncs->height = 4;
		}
		else
		{
			ncs->fg = GetColor(black);
			ncs->bg = GetColor(gray);
			ncs->hilite = GetHilite(ncs->bg);
			ncs->shadow = GetShadow(ncs->bg);
			ncs->fgsh = GetForeShadow(ncs->fg, ncs->bg);
		}
		ncs->tint = ncs->fg_tint = ncs->bg_tint = ncs->icon_tint =
			GetColor(black);
		/* set flags for fg contrast, bg average */
		/* in case just a pixmap is given */
		ncs->color_flags = FG_CONTRAST | BG_AVERAGE;
		ncs->fg_saved = ncs->fg;
		ncs->fg_alpha = 100;
		ncs->image_alpha_percent = 100;
		ncs->icon_alpha = 100;
		ncs->icon_tint_percent = 0;
		ncs->fg_tint_percent = ncs->bg_tint_percent = -1;
		ncs->dither = (Pdepth <= 8)? True:False;
		nColorsets++;
	}
}

/* ---------------------------- builtin commands ---------------------------- */

void CMD_ReadWriteColors(F_CMD_ARGS)
{
	fvwm_msg(WARN, "CMD_ReadWriteColors", "ReadWriteColors is obsolete");

	return;
}

