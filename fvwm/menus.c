/* This program is free software; you can redistribute it and/or modify
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*****************************************************************************
 * This module is all original code
 * by Rob Nation
 * Copyright 1993, Robert Nation
 *     You may use this code for any purpose, as long as the original
 *     copyright remains in the source code and all documentation
 ****************************************************************************/

/* Almost a complete rewrite by
   Greg J. Badros, Apr 6, Apr 16-19, 1997
   gjb@cs.washington.edu

   Fixed bugs w/ Escape leaving a menu item highlighted the next
      time it pops up
   Added Checking for reused hotkeys
   Added Win-like drawing code (motivated by fvwm95)
   Splitting of long (i.e. tall) menus into "More..." popup menus
   Integrated LEFT_MENUS compile-time option as a bug fix
      and added new menu style MWMLeft to builtin.c
   Drastically Improved handling of keyboard-movement keystrokes --
      moves one item at a time
   Animated animated menus (along with animated move)
   */

/* German Gomez Garcia, Nov 1998
   german@pinon.ccu.uniovi.es

   Implemented new menu style definition, allowing multiple definitios and
   gradients and pixmaps 'ala' ButtonStyle. See doc/README.styles for more
   info.  */


/***********************************************************************
 *
 * fvwm menu code
 *
 ***********************************************************************/
/* #define FVWM_DEBUG_MSGS */
#include "config.h"

#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <X11/keysym.h>
#include <sys/types.h>
#include <sys/time.h>

#include "fvwm.h"
#include "events.h"
#include "menus.h"
#include "misc.h"
#include "parse.h"
#include "repeat.h"
#include "screen.h"

/* IMPORTANT NOTE: Do *not* use any constant numbers in this file. All values
 * have to be #defined in the section below to ensure full control over the
 * menus. */

/***************************************************************
 * These values may be adjusted to fine tune the menu looks.
 ***************************************************************/

/* prevent core dumping */
#define MAX_MENU_COPIES             10
#define MAX_ITEM_LABELS              3

/* Default item formats for left and right submenus. */
#define DEFAULT_ITEM_FORMAT          "%.4s%.1|%.5i%.5l%.5l%.5r%.5i%2.3>%1|"
#define DEFAULT_LEFT_ITEM_FORMAT     "%.1|%3.2<%5i%5l%5l%5r%5i%1|%4s"

#define GRADIENT_PIXMAP_THICKNESS    5

#define DEFAULT_MENU_BORDER_WIDTH    2
#define MAX_MENU_BORDER_WIDTH        50
#define MAX_ITEM_RELIEF_THICKNESS    50

#define POPUP_NOW_RATIO              3/4

#define PARENT_MENU_FORCE_VISIBLE_WIDTH 20

/* Size of the submenu triangle. */
#define TRIANGLE_WIDTH               5
#define TRIANGLE_HEIGHT              9

#define UNDERLINE_THICKNESS       1
#define UNDERLINE_GAP             1
#define UNDERLINE_HEIGHT          (UNDERLINE_THICKNESS + UNDERLINE_GAP)

#define SEPARATOR_SHORT_X_OFFSET  5
#define SEPARATOR_Y_OFFSET        2
#define SEPARATOR_HEIGHT          2
#define SEPARATOR_TOTAL_HEIGHT    (SEPARATOR_HEIGHT + SEPARATOR_Y_OFFSET)

/* gap above item text */
#define ITEM_TEXT_Y_OFFSET        0
/* gap below item text */
#define ITEM_TEXT_Y_OFFSET2       1
#define ITEM_EXTRA_HEIGHT        (ITEM_TEXT_Y_OFFSET + ITEM_TEXT_Y_OFFSET2)

#define TITLE_TEXT_Y_OFFSET       ITEM_TEXT_Y_OFFSET
#define TITLE_TEXT_Y_OFFSET2      ITEM_TEXT_Y_OFFSET2

#define ROUNDING_ERROR_TOLERANCE  0.005

/***************************************************************
 * typedefs
 ***************************************************************/

typedef struct
{
  unsigned int keystate;
  unsigned int keycode;
  Time timestamp;
} double_keypress;

/* patch to pass the last popups position hints to popup_func */
typedef struct
{
  struct
  {
    unsigned is_last_menu_pos_hints_valid : 1;
    unsigned do_ignore_pos_hints : 1;
  } flags;
  MenuPosHints pos_hints;
} saved_pos_hints;

/***************************************************************
 * statics
 ***************************************************************/

/* This global is saved and restored every time a function is called that
 * might modify them, so we can safely let it live outside a function. */
static saved_pos_hints last_saved_pos_hints =
{
  { False, False },
  { 0, 0, 0.0, 0.0, False }
};

/***************************************************************
 * externals
 ***************************************************************/

/* This external is safe. It's written only during startup. */
extern XContext MenuContext;

/* We need to use this structure to make sure the other parts of fvwm work with
 * the most recent event. */
extern XEvent Event;

/* This one is only read, never written */
extern Time lastTimestamp;


/***************************************************************
 * forward declarations
 ***************************************************************/

static void draw_triangle_pattern(Window,GC,GC,GC,int,int,int,int,char,Bool);
static void draw_separator(Window, GC,GC,int, int,int,int,int);
static void draw_underline(MenuRoot *mr, GC gc, int x, int y,
			   char *txt, int off);
static MenuStatus MenuInteraction(
  MenuParameters *pmp, double_keypress *pdkp, Bool *pdo_warp_to_item,
  Bool *phas_mouse_moved);
static int pop_menu_up(
  MenuRoot **pmenu, MenuRoot *parent_menu, FvwmWindow **pfw, int *pcontext,
  int x, int y, Bool prefer_left_submenu, Bool do_warp_to_item,
  MenuOptions *pops, Bool *ret_overlap, Bool *pdo_warp_to_title);
static void pop_menu_down(MenuRoot **pmr, MenuParameters *pmp);
static void pop_menu_down_and_repaint_parent(
  MenuRoot **pmr, Bool *fSubmenuOverlaps, MenuParameters *pmp);
static void get_popup_options(MenuRoot *mr, MenuItem *mi, MenuOptions *pops);
static void paint_item(MenuRoot *mr, MenuItem *mi, FvwmWindow *fw,
		       Bool do_redraw_menu_border);
static void paint_menu(MenuRoot *, XEvent *, FvwmWindow *fw);
static void select_menu_item(MenuRoot *mr, MenuItem *mi, Bool select,
			     FvwmWindow *fw);
static MenuRoot *copy_menu_root(MenuRoot *menu);
static void make_menu_window(MenuRoot *mr);
static void get_xy_from_position_hints(
  MenuPosHints *ph, int width, int height, int context_width,
  Bool do_reverse_x, int *ret_x, int *ret_y);


/***************************************************************
 *
 * subroutines
 *
 ***************************************************************/

/***************************************************************
 * functions dealing with coordinates
 ***************************************************************/

static int item_middle_y_offset(MenuRoot *mr, MenuItem *mi)
{
  int r;
  if (!mi)
    return MR_BORDER_WIDTH(mr);
  r = (MI_IS_SELECTABLE(mi)) ? MR_STYLE(mr)->look.ReliefThickness : 0;
  return MI_Y_OFFSET(mi) + (MI_HEIGHT(mi) + r) / 2;
}

static int menu_middle_x_offset(MenuRoot *mr)
{
  return MR_ITEM_X_OFFSET(mr) + MR_ITEM_WIDTH(mr) / 2;
}

static Bool pointer_in_active_item_area(int x_offset, MenuRoot *mr)
{
  if (MR_STYLE(mr)->feel.flags.use_left_submenus)
  {
    return (x_offset <=
	    MR_ITEM_X_OFFSET(mr) + MR_ITEM_WIDTH(mr) -
	    MR_ITEM_WIDTH(mr) * POPUP_NOW_RATIO);
  }
  else
  {
    return (x_offset >=
	    MR_ITEM_X_OFFSET(mr) + MR_ITEM_WIDTH(mr) * POPUP_NOW_RATIO);
  }
}

static Bool pointer_in_passive_item_area(int x_offset, MenuRoot *mr)
{
  if (MR_STYLE(mr)->feel.flags.use_left_submenus)
  {
    return (x_offset >=
	    MR_ITEM_X_OFFSET(mr) + MR_ITEM_WIDTH(mr) * POPUP_NOW_RATIO);
  }
  else
  {
    return (x_offset <=
	    MR_ITEM_X_OFFSET(mr) + MR_ITEM_WIDTH(mr) -
	    MR_ITEM_WIDTH(mr) * POPUP_NOW_RATIO);
  }
}

static int get_left_popup_x_position(MenuRoot *mr, MenuRoot *submenu, int x)
{
  if (MR_STYLE(mr)->feel.flags.use_left_submenus)
  {
    return (x - MR_STYLE(mr)->feel.PopupOffsetAdd - MR_WIDTH(submenu) +
	    MR_WIDTH(mr) * (100 - MR_STYLE(mr)->feel.PopupOffsetPercent)/100);
  }
  else
  {
    return (x - MR_WIDTH(submenu) + MR_BORDER_WIDTH(mr));
  }
}

static int get_right_popup_x_position(MenuRoot *mr, MenuRoot *submenu, int x)
{
  if (MR_STYLE(mr)->feel.flags.use_left_submenus)
  {
    return (x + MR_WIDTH(mr) - MR_BORDER_WIDTH(mr));
  }
  else
  {
    return (x + MR_WIDTH(mr) * MR_STYLE(mr)->feel.PopupOffsetPercent / 100 +
	    MR_STYLE(mr)->feel.PopupOffsetAdd);
  }
}

static void get_prefered_popup_position(
  MenuRoot *mr, MenuRoot *submenu, int *px, int *py,
  Bool *pprefer_left_submenus)
{
  int menu_x, menu_y;

  XGetGeometry(dpy, MR_WINDOW(mr), &JunkRoot, &menu_x, &menu_y,
	       &JunkWidth, &JunkHeight, &JunkBW, &JunkDepth);
  /* set the direction flag */
  *pprefer_left_submenus =
    (MR_HAS_POPPED_UP_LEFT(mr) ||
     (MR_STYLE(mr)->feel.flags.use_left_submenus &&
      !MR_HAS_POPPED_UP_RIGHT(mr)));
  /* get the x position */
  if (*pprefer_left_submenus)
  {
    *px = get_left_popup_x_position(mr, submenu, menu_x);
  }
  else
  {
    *px = get_right_popup_x_position(mr, submenu, menu_x);
  }
  /* get the y position */
  if(MR_SELECTED_ITEM(mr))
  {
    *py = menu_y + MR_SELECTED_ITEM(mr)->y_offset -
      MR_BORDER_WIDTH(mr) + MR_STYLE(mr)->look.ReliefThickness;
  }
  else
  {
    *py = menu_y;
  }
}


/***************************************************************
 * warping functions
 ***************************************************************/

static void warp_pointer_to_title(MenuRoot *menu)
{
  XWarpPointer(dpy, 0, MR_WINDOW(menu), 0, 0, 0, 0, menu_middle_x_offset(menu),
	       item_middle_y_offset(menu, MR_FIRST_ITEM(menu)));
}

static MenuItem *warp_pointer_to_item(MenuRoot *mr, MenuItem *mi,
				      Bool do_skip_title)
{
  if (do_skip_title)
  {
    while (MI_NEXT_ITEM(mi) != NULL && (MI_IS_SEPARATOR(mi) || MI_IS_TITLE(mi)))
    {
      /* skip separators and titles until the first 'real' item is found */
      mi = MI_NEXT_ITEM(mi);
    }
  }
  if (mi == NULL)
    mi = MR_LAST_ITEM(mr);
  if (mi == NULL)
    return mi;
  XWarpPointer(dpy, 0, MR_WINDOW(mr), 0, 0, 0, 0,
	       menu_middle_x_offset(mr), item_middle_y_offset(mr, mi));
  return mi;
}


/***************************************************************
 * functions dealing with menu items and indices
 ***************************************************************/

/***********************************************************************
 * find_entry()
 *
 * Returns the menu item the pointer is over and optionally the offset
 * from the left side of the menu entry (if px_offset is != NULL) and
 * the MenuRoot the pointer is over (if pmr is != NULL).
 ***********************************************************************/
static MenuItem *find_entry(
  int *px_offset /*NULL means don't return this value */,
  MenuRoot **pmr /*NULL means don't return this value */)
{
  MenuItem *mi;
  MenuRoot *mr;
  int root_x, root_y;
  int x, y;
  Window Child;
  int r;

  /* x_offset returns the x offset of the pointer in the found menu item */
  if (px_offset)
    *px_offset = 0;

  if (pmr)
    *pmr = NULL;

  XQueryPointer(dpy, Scr.Root, &JunkRoot, &Child,
		&root_x,&root_y, &JunkX, &JunkY, &JunkMask);
  if (XFindContext(dpy, Child, MenuContext, (caddr_t *)&mr) == XCNOENT)
  {
    return NULL;
  }

  if (pmr)
    *pmr = mr;
  /* now get position in that child window */
  XQueryPointer(dpy, Child, &JunkRoot, &JunkChild,
		&root_x,&root_y, &x, &y, &JunkMask);

  r = MR_STYLE(mr)->look.ReliefThickness / 2;
  /* look for the entry that the mouse is in */
  for(mi = MR_FIRST_ITEM(mr); mi; mi = MI_NEXT_ITEM(mi))
  {
    int a ;
    int b ;

    a = (MI_PREV_ITEM(mi) && MI_IS_SELECTABLE(MI_PREV_ITEM(mi))) ? r : 0;
    b = (MI_NEXT_ITEM(mi) && MI_IS_SELECTABLE(MI_NEXT_ITEM(mi))) ? r : 0;
    if (y >= MI_Y_OFFSET(mi) - a &&
	y < MI_Y_OFFSET(mi) + MI_HEIGHT(mi) + b -
	((MI_IS_SELECTABLE(mi)) ? MR_STYLE(mr)->look.ReliefThickness : 0))
    {
      break;
    }
  }
  if (x < MR_ITEM_X_OFFSET(mr) ||
      x >= MR_ITEM_X_OFFSET(mr) + MR_ITEM_WIDTH(mr) - 1)
  {
    mi = NULL;
  }

  if (mi && px_offset)
    *px_offset = x;

  return mi;
}


/***************************************************************
 * Returns the position of the item in the menu, but counts
 * only items that can be selected (i.e. nor separators or
 * titles=. The count begins with 0.
 ***************************************************************/
static int get_selectable_item_index(MenuRoot *mr, MenuItem *miTarget)
{
  int i = 0;
  MenuItem *mi;

  for (mi = MR_FIRST_ITEM(mr); mi && mi != miTarget; mi = MI_NEXT_ITEM(mi))
  {
    if (MI_IS_SELECTABLE(mi))
      i++;
  }
  if (mi == miTarget)
  {
    return i;
  }
  return -1;
}

static MenuItem *get_selectable_item_from_index(MenuRoot *mr, int index)
{
  int i = -1;
  MenuItem *mi;
  MenuItem *miLastOk = NULL;

  for (mi = MR_FIRST_ITEM(mr); mi && (i < index || miLastOk == NULL);
       mi=MI_NEXT_ITEM(mi))
  {
    if (!MI_IS_SEPARATOR(mi) && !MI_IS_TITLE(mi))
    {
      miLastOk = mi;
      i++;
    }
  }
  /* DBUG("get_selectable_item_from_index","%d = %s",index,miLastOk->item); */
  return miLastOk;
}

static int get_selectable_item_count(MenuRoot *mr)
{
  return get_selectable_item_index(mr, MR_LAST_ITEM(mr));
}



/***************************************************************
 * functions dealing with submenus
 ***************************************************************/

/* Search for a submenu that was popped up by the given item in the given
 * instance of the menu. */
static MenuRoot *seek_submenu_instance(MenuRoot *parent_menu,
				       MenuItem *parent_item)
{
  MenuRoot *menu;

  for (menu = Menus.all; menu != NULL; menu = MR_NEXT_MENU(menu))
  {
    if (MR_PARENT_MENU(menu) == parent_menu &&
	MR_PARENT_ITEM(menu) == parent_item)
    {
      /* here it is */
      break;
    }
  }
  return menu;
}

static Bool is_submenu_mapped(MenuRoot *parent_menu, MenuItem *parent_item)
{
  XWindowAttributes win_attribs;
  MenuRoot *menu;

  menu = seek_submenu_instance(parent_menu, parent_item);
  if (menu == NULL)
    return False;

  if (MR_WINDOW(menu) == None)
    return False;
  XGetWindowAttributes(dpy, MR_WINDOW(menu), &win_attribs);
  return (win_attribs.map_state == IsViewable);
}

/* Returns a menu root that a given menu item pops up */
static MenuRoot *mr_popup_for_mi(MenuRoot *mr, MenuItem *mi)
{
  char *menu_name;
  MenuRoot *menu = NULL;

  /* This checks if mi is != NULL too */
  if (!mi || !MI_IS_POPUP(mi))
    return NULL;

  /* first look for a menu that is aleady mapped */
  menu = seek_submenu_instance(mr, mi);
  if (menu)
    return menu;

  /* just look past "Popup " in the action, and find that menu root */
  GetNextToken(SkipNTokens(mi->action, 1), &menu_name);
  menu = FindPopup(menu_name);
  if (menu_name != NULL)
    free(menu_name);
  return menu;
}

/***************************************************************
 * functions
 ***************************************************************/

/****************************************************************************
 *
 * Initiates a menu pop-up
 *
 * fStick = True  = sticky menu, stays up on initial button release.
 * fStick = False = transient menu, drops on initial release.
 *
 * eventp = 0: menu opened by mouse, do not warp
 * eventp > 1: root menu opened by keypress with 'Menu', warp pointer and
 *             allow 'double-keypress'.
 * eventp = 1: menu opened by keypress, warp but forbid 'double-keypress'
 *             this should always be used except in the call in 'staysup_func'
 *             in builtin.c
 *
 * Returns one of MENU_NOP, MENU_ERROR, MENU_ABORTED, MENU_DONE
 ***************************************************************************/
MenuStatus do_menu(MenuParameters *pmp)
{
  extern XEvent Event;

  MenuStatus retval = MENU_NOP;
  int x,y;
  Bool fFailedPopup = False;
  Bool fWasAlreadyPopped = False;
  Bool key_press;
  Bool fDoubleClick = False;
  Time t0 = lastTimestamp;
  XEvent tmpevent;
  double_keypress dkp;

  /* must be saved before launching parallel menus (by using the dynamic
   * actions). */
  static Bool do_warp_to_title = False;
  /* don't save these ones, we want them to work even within recursive menus
   * popped up by dynamic actions. */
  static int cindirectDeep = 0;
  static int x_start;
  static int y_start;
  static Bool has_mouse_moved = False;

  DBUG("do_menu","called");

  if (pmp->flags.is_sticky && !pmp->flags.is_submenu)
  {
    XCheckTypedEvent(dpy, ButtonPressMask, &tmpevent);
  }
  key_press = (pmp->eventp && (pmp->eventp == (XEvent *)1 ||
			       pmp->eventp->type == KeyPress));
  if(pmp->menu == NULL)
  {
    return MENU_ERROR;
  }

  /* Try to pick a root-relative optimal x,y to
     put the mouse right on the title w/o warping */
  XQueryPointer(dpy, Scr.Root, &JunkRoot, &JunkChild,
		&x, &y, &JunkX, &JunkY, &JunkMask);
  /* Save these-- we want to warp back here if this is a top level
     menu brought up by a keystroke */
  if (!pmp->flags.is_submenu && cindirectDeep == 0)
  {
    if (key_press)
    {
      x_start = x;
      y_start = y;
    }
    else
    {
      x_start = -1;
      y_start = -1;
    }
  }
  /* Figure out where we should popup, if possible */
  if (!pmp->flags.is_already_mapped)
  {
    Bool prefer_left_submenus;

    if(pmp->flags.is_submenu)
    {
      /* this is a submenu popup */
      get_prefered_popup_position(
	pmp->parent_menu, pmp->menu, &x, &y, &prefer_left_submenus);
    }
    else
    {
      /* we're a top level menu */
      has_mouse_moved = False;
      if(!GrabEm(MENU))
      {
	/* GrabEm specifies the cursor to use */
	XBell(dpy, 0);
	return MENU_ABORTED;
      }
      /* Make the menu appear under the pointer rather than warping */
      x -= menu_middle_x_offset(pmp->menu);
      y -= item_middle_y_offset(pmp->menu, MR_FIRST_ITEM(pmp->menu));
    }

    /* pop_menu_up may move the x,y to make it fit on screen more nicely */
    /* it might also move parent_menu out of the way */
    if (!pop_menu_up(&(pmp->menu), pmp->parent_menu, pmp->pTmp_win,
		     pmp->pcontext, x, y, prefer_left_submenus,
		     key_press /*warp*/, pmp->pops, NULL, &do_warp_to_title))
    {
      fFailedPopup = True;
      XBell(dpy, 0);
      UngrabEm();
      return MENU_ERROR;
    }
  }
  else
  {
    fWasAlreadyPopped = True;
    if (key_press)
    {
      warp_pointer_to_item(pmp->menu, MR_FIRST_ITEM(pmp->menu),
			   True /* skip Title */);
    }
  }
  do_warp_to_title = False;

  /* Remember the key that popped up the root menu. */
  if (!pmp->flags.is_submenu)
  {
    if (pmp->eventp && pmp->eventp != (XEvent *)1)
    {
      /* we have a real key event */
      dkp.keystate = pmp->eventp->xkey.state;
      dkp.keycode = pmp->eventp->xkey.keycode;
    }
    dkp.timestamp = (key_press) ? t0 : 0;
  }
  if (!fFailedPopup)
    retval = MenuInteraction(pmp, &dkp, &do_warp_to_title, &has_mouse_moved);
  else
    retval = MENU_ABORTED;
  if (!fWasAlreadyPopped)
    pop_menu_down(&pmp->menu, pmp);
  pmp->flags.is_menu_from_frame_or_window_or_titlebar = False;
  XFlush(dpy);

  if (!pmp->flags.is_submenu && x_start >= 0 && y_start >= 0 &&
      IS_MENU_BUTTON(retval))
  {
    /* warp pointer back to where invoked if this was brought up
     * with a keypress and we're returning from a top level menu,
     * and a button release event didn't end it */
    XWarpPointer(dpy, 0, Scr.Root, 0, 0, Scr.MyDisplayWidth,
		 Scr.MyDisplayHeight,x_start, y_start);
  }

  if (lastTimestamp-t0 < Menus.DoubleClickTime && !has_mouse_moved &&
      (!key_press || (dkp.timestamp != 0 && !pmp->flags.is_submenu)))
  {
    /* dkp.timestamp is non-zero if a double-keypress occured! */
    fDoubleClick = True;
  }
  dkp.timestamp = 0;
  if(!pmp->flags.is_submenu)
  {
    UngrabEm();
    WaitForButtonsUp();
    if (retval == MENU_DONE || retval == MENU_DONE_BUTTON)
    {
      if (pmp->ret_paction && *(pmp->ret_paction) && !fDoubleClick)
      {
	cindirectDeep++;
	ExecuteFunctionSaveTmpWin(*(pmp->ret_paction),pmp->button_window,
				  &Event, *(pmp->pcontext), -1,
				  EXPAND_COMMAND);
	cindirectDeep--;
	free(*(pmp->ret_paction));
        *(pmp->ret_paction) = NULL;
      }
      last_saved_pos_hints.flags.do_ignore_pos_hints = False;
      last_saved_pos_hints.flags.is_last_menu_pos_hints_valid = False;
    }
  }

  if (fDoubleClick)
    retval = MENU_DOUBLE_CLICKED;
  return retval;
}


/***********************************************************************
 * Procedure
 * 	menuShortcuts() - Menu keyboard processing
 *
 * Function called from MenuInteraction instead of Keyboard_Shortcuts()
 * when a KeyPress event is received.  If the key is alphanumeric,
 * then the menu is scanned for a matching hot key.  Otherwise if
 * it was the escape key then the menu processing is aborted.
 * If none of these conditions are true, then the default processing
 * routine is called.
 * TKP - uses XLookupString so that keypad numbers work with windowlist
 ***********************************************************************/
static MenuStatus menuShortcuts(MenuRoot *menu, XEvent *event,
				MenuItem **pmiCurrent, double_keypress *pdkp)
{
  int fControlKey = event->xkey.state & ControlMask? True : False;
  int fShiftedKey = event->xkey.state & ShiftMask? True: False;
  KeySym keysym;
  char keychar;
  MenuItem *newItem = NULL;
  MenuItem *miCurrent = pmiCurrent ? *pmiCurrent : NULL;
  int index;
  int mx;
  int my;
  int menu_x;
  int menu_y;
  int menu_width;
  int menu_height;
  Bool is_up;

  /* handle double-keypress */
  if (pdkp->timestamp &&
      lastTimestamp - pdkp->timestamp < Menus.DoubleClickTime &&
      event->xkey.state == pdkp->keystate &&
      event->xkey.keycode == pdkp->keycode)
  {
    *pmiCurrent = NULL;
    return MENU_SELECTED;
  }
  pdkp->timestamp = 0;
  /* Is it okay to treat keysym-s as Ascii? */
  /* No, because the keypad numbers don't work. Use XlookupString */
  index = XLookupString(&(event->xkey), &keychar, 1, &keysym, NULL);
  /* Try to match hot keys */
  /* Need isascii here - isgraph might coredump! */
  if (index == 1 && isascii((int)keychar) && isgraph((int)keychar) &&
      fControlKey == False)
  {
    /* allow any printable character to be a keysym, but be sure control
       isn't pressed */
    MenuItem *mi;
    MenuItem *mi1;
    char key;
    int countHotkey = 0;      /* Added by MMH mikehan@best.com 2/7/99 */

    /* if this is a letter set it to lower case */
    if (isupper(keychar))
      keychar = tolower((int)keychar) ;

    /* MMH mikehan@best.com 2/7/99
     * Multiple hotkeys per menu
     * Search menu for matching hotkey;
     * remember how many we found and where we found it */
    mi = ( miCurrent == NULL || miCurrent == MR_LAST_ITEM(menu)) ?
      MR_FIRST_ITEM(menu) : miCurrent->next;
    mi1 = mi;
    do
    {
      key = tolower( mi->chHotkey );
      if ( keychar == key )
      {
	if ( ++countHotkey == 1 )
	  newItem = mi;

      }
      mi = (mi == MR_LAST_ITEM(menu)) ? MR_FIRST_ITEM(menu) : MI_NEXT_ITEM(mi);
    }
    while (mi != mi1);

    /* For multiple instances of a single hotkey, just move the selection */
    if ( countHotkey > 1 )
    {
      *pmiCurrent = newItem;
      return MENU_NEWITEM;
    }
    /* Do things the old way for unique hotkeys in the menu */
    else if ( countHotkey == 1  )
    {
      *pmiCurrent = newItem;
      if (newItem && MI_IS_POPUP(newItem))
	return MENU_POPUP;
      else
	return MENU_SELECTED;
    }
    /* MMH mikehan@best.com 2/7/99 */
  }

  is_up = False;
  switch(keysym)		/* Other special keyboard handling	*/
    {
    case XK_Escape:		/* Escape key pressed. Abort		*/
      return MENU_ABORTED;
      break;

    case XK_Return:
    case XK_KP_Enter:
      return MENU_SELECTED;
      break;

    case XK_Left:
    case XK_KP_4:
      if (MR_STYLE(menu)->feel.flags.use_left_submenus)
      {
	if (miCurrent && MI_IS_POPUP(miCurrent))
	  return MENU_POPUP;
      }
      else
	return MENU_POPDOWN;
      break;

    case XK_b: /* back */
    case XK_h: /* vi left */
      return MENU_POPDOWN;
      break;

    case XK_Right:
    case XK_KP_6:
      if (!MR_STYLE(menu)->feel.flags.use_left_submenus)
      {
	if (miCurrent && MI_IS_POPUP(miCurrent))
	  return MENU_POPUP;
      }
      else
	return MENU_POPDOWN;
      break;

    case XK_f: /* forward */
    case XK_l: /* vi right */
      if (miCurrent && MI_IS_POPUP(miCurrent))
	return MENU_POPUP;
      break;

    case XK_Up:
    case XK_KP_8:
    case XK_k: /* vi up */
    case XK_p: /* prior */
      is_up = True;
      /* fall through */
    case XK_Down:
    case XK_KP_2:
    case XK_j: /* vi down */
    case XK_n: /* next */
      if (!miCurrent)
      {
	XGetGeometry(dpy, MR_WINDOW(menu), &JunkRoot, &menu_x, &menu_y,
		     &menu_width, &menu_height, &JunkBW, &JunkDepth);
	XQueryPointer(dpy, Scr.Root, &JunkRoot, &JunkChild,
		      &mx, &my, &JunkX, &JunkY, &JunkMask);
	if (my < menu_y + MR_BORDER_WIDTH(menu))
	{
	  if ((*pmiCurrent = get_selectable_item_from_index(menu,0)) != NULL)
	    return MENU_NEWITEM;
	  else
	    return MENU_NOP;
	}
	else if (my > menu_y + menu_height - MR_BORDER_WIDTH(menu))
	{
	  if ((*pmiCurrent = MR_LAST_ITEM(menu)) != NULL)
	    return MENU_NEWITEM;
	  else
	    return MENU_NOP;
	}
	else
	{
	  /*Warp the pointer back into the menu. */
	  XWarpPointer(dpy, 0, MR_WINDOW(menu), 0, 0, 0, 0,
		       menu_middle_x_offset(menu), my - menu_y);
	  *pmiCurrent = find_entry(NULL, NULL);
	  return MENU_NEWITEM;
	}
      }

      /* Need isascii here - isgraph might coredump! */
      if (isascii(keysym) && isgraph(keysym))
	fControlKey = False; /* don't use control modifier
				for j or n, since those might
				be shortcuts too-- C-j, C-n will
				always work to do a single down */
      if (is_up)
      {
	index = get_selectable_item_index(menu, miCurrent);
	if (index == 0)
	  /* wraparound */
	  index = get_selectable_item_count(menu);
	else if (fShiftedKey)
	  index = 0;
	else
	  {
	    index -= (fControlKey?5:1);
	  }
      }
      else
      {
	if (fShiftedKey)
	  index = get_selectable_item_count(menu);
	else
	{
	  index = get_selectable_item_index(menu, miCurrent) +
	    (fControlKey ? 5 : 1);
	  /* correct for the case that we're between items */
	  if (MI_IS_SEPARATOR(miCurrent) || MI_IS_TITLE(miCurrent))
	    index--;
	}
      }
      newItem = get_selectable_item_from_index(menu, index);
      if (!is_up)
      {
	if (newItem == miCurrent)
	  newItem = get_selectable_item_from_index(menu, 0);
      }
      if (newItem)
      {
	*pmiCurrent = newItem;
	return MENU_NEWITEM;
      }
      else
	return MENU_NOP;
      break;

    case XK_Page_Up:
      if ((*pmiCurrent = get_selectable_item_from_index(menu,0)) != NULL)
	return MENU_NEWITEM;
      else
	return MENU_NOP;
      break;

    case XK_Page_Down:
      if ((*pmiCurrent = MR_LAST_ITEM(menu)) != NULL)
	return MENU_NEWITEM;
      else
	return MENU_NOP;
      break;

#ifndef NO_TEAR_OFF_MENUS
    case XK_BackSpace:
fprintf(stderr,"menu torn off\n");
      return MENU_TEAR_OFF;
      break;
#endif

    default:
      /* Nothing special --- Allow other shortcuts */
      /* There are no useful shortcuts, so don't do that.
       * (Dominik Vogt, 11-Nov-1998)
       * Keyboard_shortcuts(event, NULL, ButtonRelease); */
      break;
    }

  return MENU_NOP;
}

/***********************************************************************
 *
 *  Procedure:
 *	Interacts with user to Update menu display and start new submenus,
 *      return to old sub menus, etc.
 *  Input
 *      sticks = 0, transient style menu, drops on button release
 *      sticks = 1, sticky style, stays up on a click (i.e. initial release
 *                  comes soon after initial press.
 *  Returns:
 *      MENU_ERROR on error condition
 *      MENU_SUBMENU_DONE on return from submenu to parent menu
 *      MENU_DONE on completion by non-button-release (e.g. keyboard)
 *      MENU_DONE_BUTTON on completion by button release
 *      MENU_ABORT on abort of menu by non-button-release (e.g. keyboard)
 *      MENU_ABORT_BUTTON on aborting by button release
 *      MENU_SELECTED on item selection -- returns action * in *ret_paction
 *
 ***********************************************************************/
static MenuStatus MenuInteraction(
  MenuParameters *pmp, double_keypress *pdkp, Bool *pdo_warp_to_title,
  Bool *phas_mouse_moved)
{
  extern XEvent Event;
  MenuItem *mi = NULL;
  MenuItem *tmi;
  MenuRoot *mrMi = NULL;
  MenuRoot *tmrMi = NULL;
  MenuRoot *mrPopup = NULL;
  MenuRoot *mrMiPopup = NULL;
  MenuRoot *mrNeedsPainting = NULL;
  int x_init = 0, y_init = 0;
  int x_offset = 0;
  MenuStatus retval = MENU_NOP;
  int c10msDelays = 0;
  MenuOptions mops;
  struct
  {
    unsigned do_popup_immediately : 1;
    /* used for delay popups, to just popup the menu */
    unsigned do_popup_now : 1;
    /* used for keystrokes, to popup and move to that menu */
    unsigned do_popup_and_warp : 1;
    unsigned is_key_press : 1;
    unsigned do_force_reposition : 1;
    unsigned is_off_menu_allowed : 1;
    unsigned do_popdown : 1;
    unsigned do_popup : 1;
    unsigned do_menu : 1;
    unsigned is_motion_first : 1;
    unsigned is_release_first : 1;
    unsigned is_motion_faked : 1;
    unsigned is_submenu_mapped : 1;
  } flags;
  /* Can't make this a member of the flags as we have to take its address. */
  Bool does_submenu_overlap = False;

  memset(&flags, 0, sizeof(flags));
  flags.do_force_reposition = 1;

  memset(&(mops.flags), 0, sizeof(mops.flags));
  flags.do_popup_immediately =
    (MR_STYLE(pmp->menu)->feel.flags.do_popup_immediately &&
     (Menus.PopupDelay10ms > 0));

  /* remember where the pointer was so we can tell if it has moved */
  XQueryPointer( dpy, Scr.Root, &JunkRoot, &JunkChild,
		 &x_init, &y_init, &JunkX, &JunkY, &JunkMask);

  while (True)
  {
      flags.do_popup_and_warp = False;
      flags.do_popup_now = False;
      flags.is_key_press = False;
      if (flags.do_force_reposition)
      {
	Event.type = MotionNotify;
	Event.xmotion.time = lastTimestamp;
	flags.is_motion_faked = True;
	flags.do_force_reposition = False;
      }
      else if (!XCheckMaskEvent(dpy,ExposureMask,&Event))
      {
	/* handle exposure events first */
	if (Menus.PopupDelay10ms > 0)
	{
	  while (XCheckMaskEvent(dpy,
				 ButtonPressMask|ButtonReleaseMask|
				 ExposureMask|KeyPressMask|
				 VisibilityChangeMask|ButtonMotionMask,
				 &Event) == False)
	  {
	    usleep(10000 /* 10 ms*/);
	    if (c10msDelays++ == Menus.PopupDelay10ms)
	    {
	      DBUG("MenuInteraction","Faking motion");
	      /* fake a motion event, and set flags.do_popup_now */
	      Event.type = MotionNotify;
	      Event.xmotion.time = lastTimestamp;
	      flags.is_motion_faked = True;
	      flags.do_popup_now = True;
	      break;
	    }
	  }
	}
	else
	{
	  /* block until there is an event */
	  XMaskEvent(dpy,
		     ButtonPressMask|ButtonReleaseMask|ExposureMask |
		     KeyPressMask|VisibilityChangeMask|ButtonMotionMask,
		     &Event);
	}
      }
      /*DBUG("MenuInteraction","mrPopup=%s",mrPopup?mrPopup->name:"(none)");*/

      StashEventTime(&Event);
      if (Event.type == MotionNotify)
      {
	/* discard any extra motion events before a release */
	while((XCheckMaskEvent(dpy,ButtonMotionMask|ButtonReleaseMask,
			       &Event))&&(Event.type != ButtonRelease))
	  ;
      }

      retval = 0;
      switch(Event.type)
	{
	case ButtonRelease:
	  mi = find_entry(&x_offset, &mrMi);
	  /* hold the menu up when the button is released
	   * for the first time if released OFF of the menu */
	  if(pmp->flags.is_sticky && !flags.is_motion_first)
	  {
	    flags.is_release_first = True;
	    pmp->flags.is_sticky = False;
	    continue;
	    /* break; */
	  }
	  retval = (mi) ? MENU_SELECTED : MENU_ABORTED;
	  if (retval == MENU_SELECTED && mi && MI_IS_POPUP(mi) &&
	      !MR_STYLE(pmp->menu)->feel.flags.do_popup_as_root_menu)
	  {
	      retval = MENU_POPUP;
	      flags.do_popup_now = True;
	      break;
	  }
	  pdkp->timestamp = 0;
	  goto DO_RETURN;

	case ButtonPress:
	  /* if the first event is a button press allow the release to
	     select something */
	  pmp->flags.is_sticky = False;
	  continue;

	case VisibilityNotify:
	  continue;

	case KeyPress:
	  /* Handle a key press events to allow mouseless operation */
	  flags.is_key_press = True;
	  x_offset = menu_middle_x_offset(pmp->menu);
	  retval = menuShortcuts(pmp->menu, &Event, &mi, pdkp);
	  if (retval == MENU_SELECTED && mi && MI_IS_POPUP(mi) &&
	      !MR_STYLE(pmp->menu)->feel.flags.do_popup_as_root_menu)
	    retval = MENU_POPUP;
	  if (retval == MENU_POPDOWN ||
	      retval == MENU_ABORTED ||
	      retval == MENU_SELECTED
#ifndef NO_TEAR_OFF_MENUS
	      || retval == MENU_TEAR_OFF
#endif
)
	    goto DO_RETURN;
	  /* now warp to the new menu-item, if any */
	  if (mi && (mi != find_entry(NULL, &tmrMi) || pmp->menu != tmrMi))
	  {
	    if (tmrMi == NULL)
	      tmrMi = pmp->menu;
	    warp_pointer_to_item(tmrMi, mi, False);
	    /* DBUG("MenuInteraction","Warping on keystroke to %s",mi->item);*/
	  }
	  if (retval == MENU_POPUP && mi && MI_IS_POPUP(mi))
	  {
	    flags.do_popup_and_warp = True;
	    DBUG("MenuInteraction","flags.do_popup_and_warp = True");
	    break;
	  }
	  break;

	case MotionNotify:
	  if (*phas_mouse_moved == False)
	  {
	    int x_root, y_root;
	    XQueryPointer(dpy, Scr.Root, &JunkRoot, &JunkChild,
			  &x_root,&y_root, &JunkX, &JunkY, &JunkMask);
	    if(x_root - x_init > Scr.MoveThreshold ||
	       x_init - x_root > Scr.MoveThreshold ||
	       y_root - y_init > Scr.MoveThreshold ||
	       y_init - y_root > Scr.MoveThreshold)
	    {
	      /* global variable remember that this isn't just
		 a click any more since the pointer moved */
	      *phas_mouse_moved = True;
	    }
	  }
	  mi = find_entry(&x_offset, &mrMi);
	  if (!flags.is_release_first && !flags.is_motion_faked &&
	      *phas_mouse_moved)
	    flags.is_motion_first = True;
	  flags.is_motion_faked = False;
	  break;

	case Expose:
	  /* grab our expose events, let the rest go through */
	  if((XFindContext(dpy, Event.xany.window, MenuContext,
			   (caddr_t *)&mrNeedsPainting) != XCNOENT))
	  {
	    flush_expose(Event.xany.window);
	    paint_menu(mrNeedsPainting, &Event, (*pmp->pTmp_win));
	  }
	  /* we want to dispatch this too so window decorations get redrawn
	   * after being obscured by menus. */
	  /* We need to preserve the Tmp_win here! */
	  DispatchEvent(True);
	  continue;

	default:
	  DispatchEvent(False);
	  break;
	} /* switch (Event.type) */

      /* Now handle new menu items, whether it is from a keypress or
       * a pointer motion event */
      if (mi)
      {
	/* we're on a menu item */
	flags.is_off_menu_allowed = False;
	mrMiPopup = mr_popup_for_mi(pmp->menu, mi);
	if (mrMi != pmp->menu && mrMi != mrPopup)
	{
	  /* we're on an item from a prior menu */
	  retval = MENU_POPDOWN;
	  pdkp->timestamp = 0;
	  goto DO_RETURN;
	}

	/* see if we're on a new item of this menu */
	if (mi != MR_SELECTED_ITEM(pmp->menu) && mrMi == pmp->menu)
	{
	  c10msDelays = 0;
	  /* we're on the same menu, but a different item,
	     so we need to unselect the old item */
	  if (MR_SELECTED_ITEM(pmp->menu))
	  {
	    /* something else was already selected on this menu */
	    if (mrPopup)
	    {
	      pop_menu_down_and_repaint_parent(&mrPopup, &does_submenu_overlap,
					       pmp);
	      mrPopup = NULL;
	    }
	    /* We have to pop down the menu before unselecting the item in case
	     * we are using gradient menus. The recalled image would paint over
	     * the submenu. */
	    select_menu_item(pmp->menu, MR_SELECTED_ITEM(pmp->menu), False,
			     (*pmp->pTmp_win));
	  }
	  /* do stuff to highlight the new item; this
	   * sets MR_SELECTED_ITEM(menu), too */
	  select_menu_item(pmp->menu, mi, True, (*pmp->pTmp_win));
	} /* new item of the same menu */

	/* check what has to be done with the item */
	flags.do_popdown = False;
	flags.do_popup = False;
	flags.do_menu = False;
	flags.is_submenu_mapped = False;

	if (mrMi == mrPopup)
	{
	  /* must make current popup menu a real menu */
	  flags.do_menu = True;
	  flags.is_submenu_mapped = True;
	}
	else if (flags.do_popup_and_warp)
	{
	  /* must create a real menu and warp into it */
	  if (mrPopup == NULL || mrPopup != mrMiPopup)
	  {
	    flags.do_popup = True;
	  }
	  else
	  {
	    XRaiseWindow(dpy, MR_WINDOW(mrPopup));
	    warp_pointer_to_item(mrPopup, MR_FIRST_ITEM(mrPopup), True);
	    flags.do_menu = True;
	    flags.is_submenu_mapped = True;
	  }
	}
	else if (mi && MI_IS_POPUP(mi))
	{
	  if (pointer_in_active_item_area(x_offset, mrMi) ||
	      flags.do_popup_now || flags.do_popup_immediately)
	  {
	    /* must create a new menu or popup */
	    if (mrPopup == NULL || mrPopup != mrMiPopup)
	    {
	      flags.do_popup = True;
	    }
	    else if (flags.do_popup_and_warp)
	    {
	      warp_pointer_to_item(mrPopup, MR_FIRST_ITEM(mrPopup), True);
	    }
	  }
	} /* else if (mi && MI_IS_POPUP(mi)) */
	if (flags.do_popup && mrPopup && mrPopup != mrMiPopup)
	{
	  /* must remove previous popup first */
	  flags.do_popdown = True;
	}

	if (flags.do_popdown)
	{
	  DBUG("MenuInteraction","Popping down");
	  /* popdown previous popup */
	  if (mrPopup)
	  {
	    pop_menu_down_and_repaint_parent(&mrPopup, &does_submenu_overlap,
					     pmp);
	    mrPopup = NULL;
	  }
	  flags.do_popdown = False;
	}
	if (flags.do_popup)
	{
	  DBUG("MenuInteraction","Popping up");
          /* get pos hints for item's action */
          get_popup_options(pmp->menu, mi, &mops);
	  mrPopup = mrMiPopup;
	  if (!mrPopup)
	  {
	    flags.do_menu = False;
	    flags.do_popdown = False;
	  }
	  else
	  {
	    /* create a popup menu */
	    if (!is_submenu_mapped(pmp->menu, mi))
	    {
	      /* We want to pop prepop menus so it doesn't *have* to be
		 unpopped; do_menu pops down any menus it pops up, but we
		 want to be able to popdown w/o actually removing the menu */
	      int x;
	      int y;
	      Bool prefer_left_submenus;

	      get_prefered_popup_position(
		pmp->menu, mrPopup, &x, &y, &prefer_left_submenus);
	      /* Note that we don't care if popping up the menu works. If it
	       * doesn't we'll catch it below. */
	      pop_menu_up(&mrPopup, pmp->menu, pmp->pTmp_win, pmp->pcontext, x,
			  y, prefer_left_submenus, flags.do_popup_and_warp,
			  &mops, &does_submenu_overlap, pdo_warp_to_title);
	      if (mrPopup == NULL)
	      {
		/* the menu deleted itself when execution the dynamic popup
		 * action */
		retval = MENU_ERROR;
		goto DO_RETURN;
	      }
	    }
	    if (MR_PARENT_MENU(mrPopup) == pmp->menu)
	    {
	      mi = find_entry(NULL, &mrMi);
	      if (mi && mrMi == mrPopup)
	      {
		flags.do_menu = True;
		flags.is_submenu_mapped = True;
		flags.do_popdown =
		  !MR_STYLE(pmp->menu)->feel.flags.do_popup_immediately;
	      }
	    }
	    else
	    {
	      /* This menu must be already mapped somewhere else, so ignore
	       * it completely. This can only happen if we have reached the
	       * maximum allowed number of menu copies. */
	      flags.do_menu = False;
	      flags.do_popdown = False;
	      mrPopup = NULL;
	    }
	  } /* if (!mrPopup) */
	} /* if (flags.do_popup) */
	if (flags.do_menu)
	{
	  MenuParameters mp;

	  mp.menu = mrPopup;
	  mp.parent_menu = pmp->menu;
	  mp.parent_item = mi;
	  mp.pTmp_win = pmp->pTmp_win;
	  mp.button_window = pmp->button_window;
	  mp.pcontext = pmp->pcontext;
	  mp.flags.is_menu_from_frame_or_window_or_titlebar = False;
	  mp.flags.is_sticky = False;
	  mp.flags.is_submenu = True;
	  mp.flags.is_already_mapped = flags.is_submenu_mapped;
	  mp.eventp = (flags.do_popup_and_warp) ? (XEvent *)1 : NULL;
	  mp.pops = &mops;
	  mp.ret_paction = pmp->ret_paction;

	  /* recursively do the new menu we've moved into */
	  retval = do_menu(&mp);
	  if (IS_MENU_RETURN(retval))
	  {
	    pdkp->timestamp = 0;
	    goto DO_RETURN;
	  }
	  if ((flags.do_popdown ||
	       !MR_STYLE(pmp->menu)->feel.flags.do_popup_immediately) &&
	      MR_STYLE(pmp->menu)->feel.flags.do_unmap_submenu_on_popdown)
	  {
	    pop_menu_down_and_repaint_parent(&mrPopup, &does_submenu_overlap,
					     pmp);
	    mrPopup = NULL;
	  }
	  if (retval == MENU_POPDOWN)
	  {
	    c10msDelays = 0;
	    flags.do_force_reposition = True;
	  }
	} /* if (flags.do_menu) */

	/* Now check whether we can animate the current popup menu
	   back to the original place to unobscure the current menu;  this
	   happens only when using animation */
	tmi = find_entry(NULL, &tmrMi);
	if (mrPopup && MR_XANIMATION(mrPopup) && tmi &&
	    (tmi == MR_SELECTED_ITEM(pmp->menu) || tmrMi != pmp->menu))
	{
	  int x_popup, y_popup;
	  DBUG("MenuInteraction","Moving the popup menu back over");
	  XGetGeometry(dpy, MR_WINDOW(mrPopup), &JunkRoot, &x_popup, &y_popup,
		       &JunkWidth, &JunkHeight, &JunkBW, &JunkDepth);
	  /* move it back */
	  AnimatedMoveOfWindow(MR_WINDOW(mrPopup),x_popup,y_popup,
			       x_popup - MR_XANIMATION(mrPopup), y_popup,
			       False /* no warp ptr */,-1,NULL);
	  MR_XANIMATION(mrPopup) = 0;
	}
	/* now check whether we should animate the current real menu
	   over to the right to unobscure the prior menu; only a very
	   limited case where this might be helpful and not too disruptive */
	if (mrPopup == NULL && pmp->parent_menu != NULL &&
	    MR_XANIMATION(pmp->menu) != 0 &&
	    pointer_in_passive_item_area(x_offset, mrMi))
	{
	  int x_menu, y_menu;
	  DBUG("MenuInteraction","Moving the menu back over");
	  /* we have to see if we need menu to be moved */
	  XGetGeometry( dpy, MR_WINDOW(pmp->menu), &JunkRoot, &x_menu, &y_menu,
			&JunkWidth, &JunkHeight, &JunkBW, &JunkDepth);
	  /* move it back */
	  AnimatedMoveOfWindow(MR_WINDOW(pmp->menu),x_menu,y_menu,
			       x_menu - MR_XANIMATION(pmp->menu),y_menu,
			       True /* warp ptr */,-1,NULL);
	  MR_XANIMATION(pmp->menu) = 0;
	}

      } /* if (mi) */
      else
      {
        /* moved off menu, deselect selected item... */
        if (MR_SELECTED_ITEM(pmp->menu))
	{
          if (mrPopup && flags.is_off_menu_allowed == False)
	  {
	    int x, y, mx, my;
	    unsigned int mw, mh;
	    XQueryPointer(dpy, Scr.Root, &JunkRoot, &JunkChild,
			  &x, &y, &JunkX, &JunkY, &JunkMask);
	    XGetGeometry(dpy, MR_WINDOW(pmp->menu), &JunkRoot, &mx, &my,
			 &mw, &mh, &JunkBW, &JunkDepth);
	    if ((!MR_IS_LEFT(mrPopup)  && x < mx)    ||
		(!MR_IS_RIGHT(mrPopup) && x > mx + mw) ||
		(!MR_IS_UP(mrPopup)    && y < my)    ||
		(!MR_IS_DOWN(mrPopup)  && y > my + mh))
	    {
	      select_menu_item(pmp->menu, MR_SELECTED_ITEM(pmp->menu), False,
			       (*pmp->pTmp_win));
	      pop_menu_down_and_repaint_parent(&mrPopup, &does_submenu_overlap,
					       pmp);
	      mrPopup = NULL;
	    }
	    else
	    {
	      flags.is_off_menu_allowed = True;
	    }
	  } /* if (mrPopup && flags.is_off_menu_allowed == False) */
	  else if (flags.is_off_menu_allowed == False)
	  {
	    select_menu_item(pmp->menu, MR_SELECTED_ITEM(pmp->menu), False,
			     (*pmp->pTmp_win));
	  }
        } /* if (MR_SELECTED_ITEM(menu)) */
      } /* else (!mi) */
      XFlush(dpy);
    } /* while (True) */

  DO_RETURN:
  if (retval == MENU_POPDOWN)
  {
    if (MR_SELECTED_ITEM(pmp->menu))
      select_menu_item(pmp->menu, MR_SELECTED_ITEM(pmp->menu), False,
		       (*pmp->pTmp_win));
    if (flags.is_key_press)
    {
      if (!pmp->flags.is_submenu)
	/* abort a root menu rather than pop it down */
	retval = MENU_ABORTED;
      if (pmp->parent_menu && MR_SELECTED_ITEM(pmp->parent_menu))
      {
	warp_pointer_to_item(pmp->parent_menu,
			     MR_SELECTED_ITEM(pmp->parent_menu), False);
	tmi = find_entry(NULL, &tmrMi);
	if (pmp->parent_menu != tmrMi && MR_XANIMATION(pmp->menu) == 0)
	{
	  /* Warping didn't take us to the correct menu, i.e. the spot we want
	   * to warp to is obscured. So raise our window first. */
	  XRaiseWindow(dpy, MR_WINDOW(pmp->parent_menu));
	}
      }
    }
  }
  else if (retval == MENU_SELECTED)
  {
    /* save action to execute so that the menu may be destroyed now */
    if (pmp->ret_paction)
      *pmp->ret_paction = (mi) ? strdup(mi->action) : NULL;
    retval = MENU_ADD_BUTTON_IF(flags.is_key_press, MENU_DONE);
    if (pmp->ret_paction && *pmp->ret_paction && mi && MI_IS_MENU(mi))
    {
      get_popup_options(pmp->menu, mi, &mops);
      if (mops.flags.select_in_place)
      {
	MenuRoot *submenu;
	XWindowAttributes win_attribs;

	last_saved_pos_hints.flags.is_last_menu_pos_hints_valid = True;
	last_saved_pos_hints.pos_hints.x_offset = 0;
	last_saved_pos_hints.pos_hints.x_factor = 0;
	last_saved_pos_hints.pos_hints.context_x_factor = 0;
	last_saved_pos_hints.pos_hints.y_factor = 0;
	last_saved_pos_hints.pos_hints.is_relative = False;
	last_saved_pos_hints.pos_hints.is_menu_relative = False;
	submenu = mr_popup_for_mi(pmp->menu, mi);
	if (submenu &&
	    MR_WINDOW(submenu) != None &&
	    XGetWindowAttributes(dpy, MR_WINDOW(submenu), &win_attribs) &&
	    win_attribs.map_state == IsViewable &&
	    XGetGeometry(dpy, MR_WINDOW(submenu), &JunkRoot,
			 &last_saved_pos_hints.pos_hints.x,
			 &last_saved_pos_hints.pos_hints.y,
			 &JunkWidth, &JunkHeight, &JunkBW, &JunkDepth))
	{
	  /* The submenu is mapped, just take its position and put it in the
	   * position hints. */
	}
	else if (mops.flags.has_poshints)
	{
	  last_saved_pos_hints.pos_hints = mops.pos_hints;
	}
	else
	{
	  Bool dummy;

	  get_prefered_popup_position(
	    pmp->menu, submenu, &last_saved_pos_hints.pos_hints.x,
	    &last_saved_pos_hints.pos_hints.y, &dummy);
	}
	if (mops.flags.select_warp)
	{
	  *pdo_warp_to_title = True;
	}
      }
      else
      {
	last_saved_pos_hints.flags.do_ignore_pos_hints = True;
      } /* mops.flags.select_in_place */
    }
  }
#ifndef NO_TEAR_OFF_MENUS
  else if (retval == MENU_TEAR_OFF)
  {
fprintf(stderr,"got MENU_TEAR_OFF\n");
    AddWindow(MR_WINDOW(pmp->menu), NULL);
  }
#endif
  if (mrPopup)
  {
    pop_menu_down_and_repaint_parent(&mrPopup, &does_submenu_overlap, pmp);
  }
  return MENU_ADD_BUTTON_IF(flags.is_key_press,retval);
}

static int do_menus_overlap(
  MenuRoot *mr, int x, int y, int width, int height, int h_tolerance,
  int v_tolerance, int s_tolerance, Bool allow_popup_offset_tolerance)
{
  int prior_x, prior_y, x_overlap;
  unsigned int prior_width, prior_height;

  if (mr == NULL)
    return 0;

  XGetGeometry(dpy,MR_WINDOW(mr), &JunkRoot,&prior_x,&prior_y,
	       &prior_width,&prior_height, &JunkBW, &JunkDepth);
  x_overlap = 0;
#if DEBUG_DO_MENUS_OVERLAP
  fprintf(stderr,"do_menus_overlap called: (allow tolerance = %d)\n",
	  allow_popup_offset_tolerance);
  fprintf(stderr,"tolerances: h = %d, v = %d, s = %d\n", h_tolerance,
	  v_tolerance, s_tolerance);
  fprintf(stderr,"x  = %4d - %4d,  y = %4d - %4d\n",x,x+width-1,y,y+height-1);
  fprintf(stderr,"px = %4d - %4d, py = %4d - %4d\n",prior_x,
	  prior_x+prior_width-1,prior_y,prior_y+prior_height-1);
#endif
  if (allow_popup_offset_tolerance)
  {
    if (MR_STYLE(mr)->feel.PopupOffsetAdd < 0)
      s_tolerance = -MR_STYLE(mr)->feel.PopupOffsetAdd;
    if (MR_STYLE(mr)->feel.flags.use_left_submenus)
      prior_x += (100 - MR_STYLE(mr)->feel.PopupOffsetPercent) / 100;
    else
      prior_width *= (float)(MR_STYLE(mr)->feel.PopupOffsetPercent) / 100.0;
#if DEBUG_DO_MENUS_OVERLAP
    fprintf(stderr,
	    "modified:\npx = %4d - %4d, py = %4d - %4d, s_tolerance = %d\n",
	    prior_x,prior_x+prior_width-1,prior_y,prior_y+prior_height-1);
#endif
  }
  if (MR_STYLE(mr)->feel.flags.use_left_submenus)
  {
    int t = s_tolerance;
    s_tolerance = h_tolerance;
    h_tolerance = t;
  }
  if (y < prior_y + prior_height - v_tolerance &&
      prior_y < y + height - v_tolerance &&
      x < prior_x + prior_width - s_tolerance &&
      prior_x < x + width - h_tolerance)
  {
    x_overlap = x - prior_x;
    if (x <= prior_x)
    {
      x_overlap--;
    }
  }
  return x_overlap;
}

/***********************************************************************
 *
 *  Procedure:
 *	pop_menu_up - pop up a pull down menu
 *
 *  Inputs:
 *	x, y	  - location of upper left of menu
 *      do_warp_to_item - warp pointer to the first item after title
 *      pops      - pointer to the menu options for new menu
 *
 ***********************************************************************/
static Bool pop_menu_up(
  MenuRoot **pmenu, MenuRoot *parent_menu, FvwmWindow **pfw, int *pcontext,
  int x, int y, Bool prefer_left_submenu, Bool do_warp_to_item,
  MenuOptions *pops, Bool *ret_overlap, Bool *pdo_warp_to_title)
{
  Bool do_warp_to_title = False;
  int x_overlap = 0;
  int x_clipped_overlap;
  MenuRoot *mrMi;
  MenuRoot *menu;
  FvwmWindow *fw;
  int context;
  int bw = 0;
  int bwp = 0;

  DBUG("pop_menu_up","called");
  menu = *pmenu;
  if (!menu || MR_WINDOW(menu) == None ||
      (MR_MAPPED_COPIES(menu) > 0 && MR_COPIES(menu) >= MAX_MENU_COPIES))
  {
    *pdo_warp_to_title = False;
    return False;
  }

  /***************************************************************
   * handle dynamic menu actions
   ***************************************************************/

  /* First of all, execute the popup action (if defined). */
  if (MR_DYNAMIC(menu).popup_action)
  {
    char *menu_name;
    saved_pos_hints pos_hints;
    Bool f;
    Time t;
    extern XEvent Event;

    /* save variables that we still need but that may be overwritten */
    menu_name = strdup(MR_NAME(menu));
    pos_hints = last_saved_pos_hints;
    f = *pdo_warp_to_title;
    t = lastTimestamp;
    /* need to ungrab the mouse during function call */
    UngrabEm();
    /* Execute the action */
    ExecuteFunctionSaveTmpWin(MR_DYNAMIC(menu).popup_action, *pfw, &Event,
			      *pcontext, -2, DONT_EXPAND_COMMAND);
    /* restore the stuff we saved */
    lastTimestamp = t;
    *pdo_warp_to_title = f;
    last_saved_pos_hints = pos_hints;
    /* See if the window has been deleted */
    if (!check_if_fvwm_window_exists(*pfw))
    {
      *pfw = NULL;
      *pcontext = 0;
    }
    /* Now let's see if the menu still exists. It may have been destroyed and
     * recreated, so we have to look for a menu with the saved name. */
    *pmenu = FindPopup(menu_name);
    free(menu_name);
    menu = *pmenu;
    /* grab the mouse again */
    if(!GrabEm(MENU))
    {
      /* FIXME: If this happens in a submenu we're pissed. All kinds of
       * unpleasant things might happen. */
      free(menu_name);
      return False;
    }
    if (menu)
    {
      MakeMenu(menu);
    }
  }
  fw = *pfw;
  context = *pcontext;

  if(menu == NULL || MR_FIRST_ITEM(menu) == NULL || MR_ITEMS(menu) == 0)
  {
    /* The menu deleted itself or all its items or it has been empty from the
     * start. */
    *pdo_warp_to_title = False;
    return False;
  }

  /***************************************************************
   * Create a new menu instance (if necessary)
   ***************************************************************/

  if (MR_MAPPED_COPIES(menu) > 0)
  {
    /* create a new instance of the menu */
    *pmenu = copy_menu_root(*pmenu);
    if (!*pmenu)
      return False;
    make_menu_window(*pmenu);
    menu = *pmenu;
  }

  /***************************************************************
   * Evaluate position hints
   ***************************************************************/

  /* calculate position from position hints if available */
  if (pops->flags.has_poshints &&
      !last_saved_pos_hints.flags.do_ignore_pos_hints)
  {
    get_xy_from_position_hints(
      &(pops->pos_hints), MR_WIDTH(menu), MR_HEIGHT(menu),
      (parent_menu) ? MR_WIDTH(parent_menu) : 0, prefer_left_submenu, &x, &y);
  }

  /***************************************************************
   * Initialise new menu
   ***************************************************************/

  MR_PARENT_MENU(menu) = parent_menu;
  MR_PARENT_ITEM(menu) = (parent_menu) ? MR_SELECTED_ITEM(parent_menu) : NULL;
  MR_IS_PAINTED(menu) = 0;
  MR_IS_LEFT(menu) = 0;
  MR_IS_RIGHT(menu) = 0;
  MR_IS_UP(menu) = 0;
  MR_IS_DOWN(menu) = 0;
  MR_XANIMATION(menu) = 0;
  InstallRootColormap();

  /***************************************************************
   * Handle popups from button clicks on buttons in the title bar,
   * or the title bar itself. Position hints override this.
   ***************************************************************/

  if (!pops->flags.has_poshints && fw && parent_menu == NULL)
  {
    if(context&C_LALL)
    {
      y = fw->frame_g.y + fw->boundary_width + fw->title_g.height + 1;
      x = fw->frame_g.x + fw->boundary_width +
	ButtonPosition(context, fw) * fw->title_g.height + 1;
    }
    else if(context&C_RALL)
    {
      y = fw->frame_g.y + fw->boundary_width + fw->title_g.height + 1;
      x = fw->frame_g.x + fw->frame_g.width -
	fw->boundary_width - ButtonPosition(context, fw) *
	fw->title_g.height-MR_WIDTH(menu) + 1;
    }
    else if(context&C_TITLE)
    {
      y = fw->frame_g.y + fw->boundary_width + fw->title_g.height + 1;
      if(x < fw->frame_g.x + fw->title_g.x)
	x = fw->frame_g.x + fw->title_g.x;
      if((x + MR_WIDTH(menu)) >
	 (fw->frame_g.x + fw->title_g.x + fw->title_g.width))
	x = fw->frame_g.x + fw->title_g.x + fw->title_g.width-
	  MR_WIDTH(menu) +1;
    }
  } /* if (pops->flags.has_poshints) */

  /***************************************************************
   * Clip to screen
   ***************************************************************/

  if (parent_menu)
  {
    bw = MR_BORDER_WIDTH(menu);
    bwp = MR_BORDER_WIDTH(parent_menu);
    x_overlap =
      do_menus_overlap(parent_menu, x, y, MR_WIDTH(menu), MR_HEIGHT(menu),
		       bwp, bwp, bwp, True);
  }

  /* clip to screen */
  if (x + MR_WIDTH(menu) > Scr.MyDisplayWidth)
    x = Scr.MyDisplayWidth - MR_WIDTH(menu);
  if (y + MR_HEIGHT(menu) > Scr.MyDisplayHeight)
    y = Scr.MyDisplayHeight - MR_HEIGHT(menu);
  if (x < 0)
    x = 0;
  if (y < 0)
    y = 0;

  /***************************************************************
   * Calculate position and animate menus
   ***************************************************************/

  if (parent_menu == NULL)
  {
    MR_HAS_POPPED_UP_LEFT(menu) = 0;
    MR_HAS_POPPED_UP_RIGHT(menu) = 0;
  }
  else
  {
    int prev_x;
    int prev_y;
    int left_x;
    int right_x;
    unsigned int prev_width;
    unsigned int prev_height;
    Bool use_left_submenus = MR_STYLE(menu)->feel.flags.use_left_submenus;

    /* try to find a better place */
    XGetGeometry(dpy,MR_WINDOW(parent_menu), &JunkRoot,&prev_x,&prev_y,
		 &prev_width,&prev_height, &JunkBW, &JunkDepth);

    /* check if menus overlap */
    x_clipped_overlap =
      do_menus_overlap(parent_menu, x, y, MR_WIDTH(menu), MR_HEIGHT(menu),
		       bwp, bwp, bwp, True);
    if (x_clipped_overlap &&
	(!pops->flags.has_poshints || pops->pos_hints.is_relative == False ||
	 x_overlap == 0))
    {
      /* menus do overlap, but do not reposition if overlap was caused by
       * relative positioning hints */
      left_x = get_left_popup_x_position(parent_menu, menu, prev_x);
      right_x = get_right_popup_x_position(parent_menu, menu, prev_x);
      if (use_left_submenus)
      {
	if (left_x + MR_WIDTH(menu) < prev_x + bwp)
	  left_x = prev_x + bwp - MR_WIDTH(menu);
      }
      else
      {
	if (right_x > prev_x + prev_width - bwp)
	  right_x = prev_x + prev_width - bwp;
      }

      /*
       * Animate parent menu
       */

      if (MR_STYLE(menu)->feel.flags.is_animated)
      {
	/* animate previous out of the way */
	int a_left_x;
	int a_right_x;
	int end_x;

	if (use_left_submenus)
	{
	  if (prev_x - left_x < MR_WIDTH(menu))
	    a_right_x = x + (prev_x - left_x);
	  else
	    a_right_x = x + MR_WIDTH(menu) - bw;
	  a_left_x = x - MR_WIDTH(parent_menu);
	}
	else
	{
	  if (right_x - prev_x < prev_width)
	    a_left_x = x + (prev_x - right_x);
	  else
	    a_left_x = x - prev_width + bw;
	  a_right_x = x + MR_WIDTH(menu);
	}
	if (prefer_left_submenu)
	{
	  /* popup menu is left of old menu, try to move prior menu right */
	  if (a_right_x + prev_width <= Scr.MyDisplayWidth)
	    end_x = a_right_x;
	  else if (a_left_x >= 0)
	    end_x = a_left_x;
	  else
	    end_x = Scr.MyDisplayWidth - prev_width;
	}
	else
	{
	  /* popup menu is right of old menu, try to move prior menu left */
	  if (a_left_x >= 0)
	    end_x = a_left_x;
	  else if (a_right_x + prev_width <= Scr.MyDisplayWidth)
	    end_x = a_right_x;
	  else
	    end_x = 0;
	}
	if (end_x == a_left_x || end_x == 0)
	{
	  MR_HAS_POPPED_UP_LEFT(menu) = 0;
	  MR_HAS_POPPED_UP_RIGHT(menu) = 1;
	}
	else
	{
	  MR_HAS_POPPED_UP_LEFT(menu) = 1;
	  MR_HAS_POPPED_UP_RIGHT(menu) = 0;
	}
	MR_XANIMATION(parent_menu) += end_x - prev_x;
	AnimatedMoveOfWindow(MR_WINDOW(parent_menu),prev_x,prev_y,end_x,prev_y,
			     True, -1, NULL);
      } /* if (MR_STYLE(menu)->feel.flags.is_animated) */

      /*
       * Try the other side of the parent menu
       */

      else if (!pops->flags.fixed)
      {
	Bool may_use_left;
	Bool may_use_right;
	Bool do_use_left;
	Bool use_left_as_last_resort;

	use_left_as_last_resort =
	  (left_x > Scr.MyDisplayWidth - right_x - MR_WIDTH(menu));
	may_use_left = (left_x >= 0);
	may_use_right = (right_x + MR_WIDTH(menu) <= Scr.MyDisplayWidth);
	if (!may_use_left && !may_use_right)
	{
	  /* If everything goes wrong, put the submenu on the side with more
	   * free space. */
	  do_use_left = use_left_as_last_resort;
	}
	else if (may_use_left && (prefer_left_submenu || !may_use_right))
	  do_use_left = True;
	else
	  do_use_left = False;
	x = (do_use_left) ? left_x : right_x;
	MR_HAS_POPPED_UP_LEFT(menu) = do_use_left;
	MR_HAS_POPPED_UP_RIGHT(menu) = !do_use_left;

	/* Force the menu onto the screen, but leave at least
	 * PARENT_MENU_FORCE_VISIBLE_WIDTH pixels of the parent menu visible */
	if (x + MR_WIDTH(menu) > Scr.MyDisplayWidth)
	{
	  int d = x + MR_WIDTH(menu) - Scr.MyDisplayWidth;
	  int c;

	  if (prev_width >= PARENT_MENU_FORCE_VISIBLE_WIDTH)
	    c = prev_x + PARENT_MENU_FORCE_VISIBLE_WIDTH;
	  else
	    c = prev_x + prev_width;

	  if (x - c >= d || x <= prev_x)
	    x -= d;
	  else if (x > c)
	    x = c;
	}
	if (x < 0)
	{
	  int c = prev_width - PARENT_MENU_FORCE_VISIBLE_WIDTH;

	  if (c < 0)
	    c = 0;
	  if (-x > c)
	    x += c;
	  else
	    x = 0;
	}
      } /* else if (non-overlapping menu style) */
    } /* if (x_clipped_overlap && ...) */
    else
    {
      MR_HAS_POPPED_UP_LEFT(menu) = prefer_left_submenu;
      MR_HAS_POPPED_UP_RIGHT(menu) = !prefer_left_submenu;
    }

    if (x < prev_x)
      MR_IS_LEFT(menu) = 1;
    if (x + MR_WIDTH(menu) > prev_x + prev_width)
      MR_IS_RIGHT(menu) = 1;
    if (y < prev_y)
      MR_IS_UP(menu) = 1;
    if (y + MR_HEIGHT(menu) > prev_y + prev_height)
      MR_IS_DOWN(menu) = 1;
    if (!MR_IS_LEFT(menu) && !MR_IS_RIGHT(menu))
    {
      MR_IS_LEFT(menu) = 1;
      MR_IS_RIGHT(menu) = 1;
    }
  } /* if (parent_menu) */

  /***************************************************************
   * Pop up the menu
   ***************************************************************/

  XMoveWindow(dpy, MR_WINDOW(menu), x, y);
  XMapRaised(dpy, MR_WINDOW(menu));
  XFlush(dpy);
  MR_MAPPED_COPIES(menu)++;
  if (ret_overlap)
  {
    *ret_overlap =
      do_menus_overlap(parent_menu, x, y, MR_WIDTH(menu), MR_HEIGHT(menu),
		       0, 0, 0, False) ? True : False;
  }

  /***************************************************************
   * Warp the pointer
   ***************************************************************/

  if (!do_warp_to_item)
  {
    MenuItem *mi;

    mi = find_entry(NULL, &mrMi);
    if (mi && mrMi == menu && mi != MR_FIRST_ITEM(mrMi))
    {
      /* pointer is on an item of the popup */
      if (MR_STYLE(menu)->feel.flags.do_title_warp)
      {
	/* warp pointer if not on a root menu */
	do_warp_to_title = True;
      }
    }
  } /* if (!do_warp_to_item) */

  if (pops->flags.no_warp)
  {
    do_warp_to_title = False;
  }
  else if (pops->flags.warp_title)
  {
    do_warp_to_title = True;
  }
  if (*pdo_warp_to_title)
  {
    do_warp_to_title = True;
    *pdo_warp_to_title = False;
  }
  if (do_warp_to_item)
  {
    /* also warp */
    DBUG("pop_menu_up","Warping to item");
    MR_SELECTED_ITEM(menu) =
      warp_pointer_to_item(menu, MR_FIRST_ITEM(menu), True /* skip Title */);
    select_menu_item(menu, MR_SELECTED_ITEM(menu), True, fw);
  }
  else if(do_warp_to_title)
  {
    /* Warp pointer to middle of top line, since we don't
     * want the user to come up directly on an option */
    DBUG("pop_menu_up","Warping to title");
    warp_pointer_to_title(menu);
  }
  return True;
}

/* Set the selected-ness state of the menuitem passed in */
static void select_menu_item(MenuRoot *mr, MenuItem *mi, Bool select,
			     FvwmWindow *fw)
{
  if (select == True && MR_SELECTED_ITEM(mr) != NULL &&
      MR_SELECTED_ITEM(mr) != mi)
    select_menu_item(mr, MR_SELECTED_ITEM(mr), False, fw);
  else if (select == False && MR_SELECTED_ITEM(mr) == NULL)
    return;
  else if (select == True && MR_SELECTED_ITEM(mr) == mi)
    return;
  if (!MI_IS_SELECTABLE(mi))
    return;

  if (select == False)
    MI_WAS_DESELECTED(mi) = True;

#ifdef GRADIENT_BUTTONS
  switch (MR_STYLE(mr)->look.face.type)
  {
  case HGradMenu:
  case VGradMenu:
  case DGradMenu:
  case BGradMenu:
    if (select == True)
    {
      int iy;
      int ih;
      unsigned int mw;

      if (!MR_IS_PAINTED(mr))
      {
	paint_menu(mr, NULL, fw);
	flush_expose(MR_WINDOW(mr));
      }
      iy = MI_Y_OFFSET(mi);
      ih = MI_HEIGHT(mi) +
	(MI_IS_SELECTABLE(mi) ? MR_STYLE(mr)->look.ReliefThickness : 0);
      if (iy < 0)
      {
	ih += iy;
	iy = 0;
      }
      mw = MR_WIDTH(mr) - 2 * MR_BORDER_WIDTH(mr);
      if (iy + ih > MR_HEIGHT(mr))
	ih = MR_HEIGHT(mr) - iy;
      /* grab image */
      MR_STORED_ITEM(mr).stored =
	XCreatePixmap(dpy, Scr.NoFocusWin, mw, ih, Scr.depth);
      XCopyArea(
	dpy, MR_WINDOW(mr), MR_STORED_ITEM(mr).stored,
	MR_STYLE(mr)->look.MenuGC, MR_BORDER_WIDTH(mr), iy, mw, ih, 0, 0);
      MR_STORED_ITEM(mr).y = iy;
      MR_STORED_ITEM(mr).width = mw;
      MR_STORED_ITEM(mr).height = ih;
    }
    else if (select == False && MR_STORED_ITEM(mr).width != 0)
    {
      /* ungrab image */
      XCopyArea(dpy, MR_STORED_ITEM(mr).stored, MR_WINDOW(mr),
		MR_STYLE(mr)->look.MenuGC, 0, 0,
		MR_STORED_ITEM(mr).width, MR_STORED_ITEM(mr).height,
		MR_BORDER_WIDTH(mr), MR_STORED_ITEM(mr).y);

      if (MR_STORED_ITEM(mr).stored != None)
	XFreePixmap(dpy, MR_STORED_ITEM(mr).stored);
      MR_STORED_ITEM(mr).stored = None;
      MR_STORED_ITEM(mr).width = 0;
      MR_STORED_ITEM(mr).height = 0;
      MR_STORED_ITEM(mr).y = 0;
    }
    break;
  default:
    if (MR_STORED_ITEM(mr).width != 0)
    {
      if (MR_STORED_ITEM(mr).stored != None)
	XFreePixmap(dpy, MR_STORED_ITEM(mr).stored);
      MR_STORED_ITEM(mr).stored = None;
      MR_STORED_ITEM(mr).width = 0;
      MR_STORED_ITEM(mr).height = 0;
      MR_STORED_ITEM(mr).y = 0;
    }
    break;
  }
#endif

  MR_SELECTED_ITEM(mr) = (select) ? mi : NULL;
  paint_item(mr, mi, fw, False);
}

/* FindPopup expects a token as the input. Make sure you have used
 * GetNextToken before passing a menu name to remove quotes (if necessary) */
MenuRoot *FindPopup(char *popup_name)
{
  MenuRoot *mr;

  if(popup_name == NULL)
    return NULL;

  mr = Menus.all;
  while(mr != NULL)
  {
    if(MR_NAME(mr) != NULL)
    {
      if(mr == MR_ORIGINAL_MENU(mr) &&
	 strcasecmp(popup_name, MR_NAME(mr)) == 0)
      {
        return mr;
      }
    }
    mr = MR_NEXT_MENU(mr);
  }
  return NULL;

}

/* Returns the menu options for the menu that a given menu item pops up */
static void get_popup_options(MenuRoot *mr, MenuItem *mi, MenuOptions *pops)
{
  if (!mi)
    return;
  pops->flags.has_poshints = 0;
  /* just look past "Popup <name>" in the action */
  GetMenuOptions(SkipNTokens(mi->action, 2), MR_WINDOW(mr), NULL, mr, mi,
		 pops);
}


/***********************************************************************
 *
 *  Procedure:
 *	pop_menu_down - unhighlight the current menu selection and
 *                    take down the menus
 *
 *      mr     - menu to pop down; this pointer is invalid after the function
 *               returns. Don't use it anymore!
 *      parent - the menu that has spawned mr (may be NULL). this is
 *               used to see if mr was spawned by itself on some level.
 *               this is a hack to allow specifying 'Popup foo' within
 *               menu foo. You must use the MenuRoot that is currently
 *               being processed here. DO NOT USE MR_PARENT_MENU(mr) here!
 *
 ***********************************************************************/
static void pop_menu_down(MenuRoot **pmr, MenuParameters *pmp)
{
  MenuItem *mi;
  assert(*pmr);

  memset(&(MR_DYNAMIC_FLAGS(*pmr)), 0, sizeof(MR_DYNAMIC_FLAGS(*pmr)));
  XUnmapWindow(dpy, MR_WINDOW(*pmr));
  MR_MAPPED_COPIES(*pmr)--;

  UninstallRootColormap();
  XFlush(dpy);
  if (*(pmp->pcontext) & (C_WINDOW | C_FRAME | C_TITLE | C_SIDEBAR))
    pmp->flags.is_menu_from_frame_or_window_or_titlebar = True;
  else
    pmp->flags.is_menu_from_frame_or_window_or_titlebar = False;
  if ((mi = MR_SELECTED_ITEM(*pmr)) != NULL)
  {
    select_menu_item(*pmr, mi, False, (*pmp->pTmp_win));
  }

  /* DBUG("pop_menu_down","popped down %s",MR_NAME(*pmr)); */
  if (MR_COPIES(*pmr) > 1)
  {
    /* delete this instance of the menu */
    DestroyMenu(*pmr, False);
  }
  else if (MR_DYNAMIC(*pmr).popdown_action)
  {
    /* Finally execute the popdown action (if defined). */
    saved_pos_hints pos_hints;
    Time t;
    extern XEvent Event;

    /* save variables that we still need but that may be overwritten */
    pos_hints = last_saved_pos_hints;
    t = lastTimestamp;
    /* need to ungrab the mouse during function call */
    UngrabEm();
    /* Execute the action */
    ExecuteFunctionSaveTmpWin(MR_DYNAMIC(*pmr).popdown_action,
			      (*pmp->pTmp_win), &Event, *(pmp->pcontext), -2,
			      DONT_EXPAND_COMMAND);
    /* restore the stuff we saved */
    last_saved_pos_hints = pos_hints;
    lastTimestamp = t;
    /* grab the mouse again */
    GrabEm(MENU);
    /* FIXME: If the grab fails in a submenu we're pissed. All kinds of
     * unpleasant things might happen. */
    if (!check_if_fvwm_window_exists(*(pmp->pTmp_win)))
    {
      *(pmp->pTmp_win) = NULL;
      *(pmp->pcontext) = 0;
    }
  }

  return;
}

/***********************************************************************
 *
 *  Procedure:
 *      pop_menu_down_and_repaint_parent - Pops down a menu and repaints the
 *      overlapped portions of the parent menu. This is done only if
 *      *fSubmenuOverlaps is True. *fSubmenuOverlaps is set to False
 *      afterwards.
 *
 ***********************************************************************/
static void pop_menu_down_and_repaint_parent(
  MenuRoot **pmr, Bool *fSubmenuOverlaps, MenuParameters *pmp)
{
  MenuRoot *parent = MR_PARENT_MENU(*pmr);
  XEvent event;
  int mr_y;
  unsigned int mr_height;
  unsigned int mr_width;
  int parent_y;

  if (*fSubmenuOverlaps && parent)
  {
    XGetGeometry(dpy, MR_WINDOW(*pmr), &JunkRoot, &JunkX, &mr_y,
		 &mr_width, &mr_height, &JunkBW, &JunkDepth);
    XGetGeometry(dpy, MR_WINDOW(parent), &JunkRoot, &JunkX, &parent_y,
		 &JunkWidth, &JunkWidth, &JunkBW, &JunkDepth);
    pop_menu_down(pmr, pmp);
    /* Create a fake event to pass into paint_menu */
    event.type = Expose;
    event.xexpose.x = 0;
    event.xexpose.width = mr_width;
    event.xexpose.y = mr_y - parent_y;
    event.xexpose.height = mr_height;
    paint_menu(parent, &event, (*pmp->pTmp_win));
    flush_expose(MR_WINDOW(parent));
  }
  else
  {
    pop_menu_down(pmr, pmp);
  }
  *fSubmenuOverlaps = False;
  return;
}


/***********************************************************************
 *
 *  Procedure:
 *	RelieveHalfRectangle - add relief lines to the sides only of a
 *      rectangular window
 *
 ***********************************************************************/
static void RelieveHalfRectangle(Window win, int x, int y, int w, int h,
				 int bw, GC Hilite, GC Shadow)
{
  int i;
  for (i = bw; i-- > 0 ; )
  {
    XDrawLine(dpy, win, Hilite, x + i, y, x + i, y + h - 1);
    XDrawLine(dpy, win, Shadow, x + w - 1 - i, y, x + w - 1 - i, y + h - 1);
  }
}


/***********************************************************************
 *
 *  Procedure:
 *      paint_item - draws a single entry in a popped up menu
 *
 *      mr - the menu instance that holds the menu item
 *      mi - the menu item to redraw
 *      fw - the FvwmWindow structure to check against allowed functions
 *
 ***********************************************************************/
static void paint_item(MenuRoot *mr, MenuItem *mi, FvwmWindow *fw,
		       Bool do_redraw_menu_border)
{
  int y_offset;
  int text_y;
  int y_height;
  int x;
  int y;
  GC ShadowGC, ReliefGC, currentGC;
  short relief_thickness = MR_STYLE(mr)->look.ReliefThickness;
#ifdef GRADIENT_BUTTONS
  Bool is_gradient_menu;
#endif
  Bool is_item_selected;
  int i;
  int sx1;
  int sx2;
  int bw = MR_BORDER_WIDTH(mr);

  if (!mi)
    return;
  is_item_selected = (mi == MR_SELECTED_ITEM(mr));

#ifdef GRADIENT_BUTTONS
  switch (MR_STYLE(mr)->look.face.type)
  {
  case HGradMenu:
  case VGradMenu:
  case DGradMenu:
  case BGradMenu:
    is_gradient_menu = True;
    break;
  default:
    is_gradient_menu = False;
    break;
  }
#endif

  y_offset = MI_Y_OFFSET(mi);
  y_height = MI_HEIGHT(mi);
  if (MI_IS_SELECTABLE(mi))
  {
    text_y = y_offset + MR_ITEM_TEXT_Y_OFFSET(mr);
  }
  else
  {
    text_y = y_offset + MR_STYLE(mr)->look.pStdFont->y + TITLE_TEXT_Y_OFFSET;
  }
  /* center text vertically if the pixmap is taller */
  if (MI_PICTURE(mi))
  {
    text_y += MI_PICTURE(mi)->height;
  }
  for (i = 0; i < MAX_MINI_ICONS; i++)
  {
    if (MI_MINI_ICON(mi)[i])
    {
      y = MI_MINI_ICON(mi)[i]->height - MR_STYLE(mr)->look.pStdFont->height;
      if (y > 1)
      {
	text_y += y / 2;
      }
    }
  }

  ShadowGC = MR_STYLE(mr)->look.MenuShadowGC;
  if(Scr.depth<2)
    ReliefGC = MR_STYLE(mr)->look.MenuShadowGC;
  else
    ReliefGC = MR_STYLE(mr)->look.MenuReliefGC;

  /***************************************************************
   * Draw the shadows for the absolute outside of the menus
   * This stuff belongs in here, not in paint_menu, since we only
   * want to redraw it when we have too (i.e. on expose event)
   ***************************************************************/

  if (do_redraw_menu_border)
  {
    /* Top of the menu */
    if(mi == MR_FIRST_ITEM(mr))
    {
      for (i = bw; i-- ; )
      {
	XDrawLine(dpy, MR_WINDOW(mr), ReliefGC, 0, i, MR_WIDTH(mr) - 1 - i, i);
	if (i > 0)
	{
	  XDrawLine(dpy, MR_WINDOW(mr), ShadowGC, MR_WIDTH(mr) - i, i,
		    MR_WIDTH(mr) - 1, i);
	}
      }
    }

    /* Botton of the menu */
    if(MI_NEXT_ITEM(mi) == NULL)
    {
      for (i = bw; i-- ; )
      {
	XDrawLine(dpy, MR_WINDOW(mr), ShadowGC, i, MR_HEIGHT(mr) - 1 - i,
		  MR_WIDTH(mr) - 1, MR_HEIGHT(mr) - 1 - i);
	if (i > 0)
	{
	  XDrawLine(dpy, MR_WINDOW(mr), ReliefGC, 0, MR_HEIGHT(mr) - 1 - i,
		    i - 1, MR_HEIGHT(mr) - 1 - i);
	}
      }
    }

    /* Left and right side of the menu. */
    RelieveHalfRectangle(MR_WINDOW(mr), 0, y_offset, MR_WIDTH(mr),
			 y_height + ((!MI_NEXT_ITEM(mi) && MI_IS_SELECTABLE(mi)) ?
				     relief_thickness : 0),
			 bw, ReliefGC, ShadowGC);
  }

  /***************************************************************
   * Hilight the item.
   ***************************************************************/

  /* Hilight or clear the background. */
  if (is_item_selected && MR_STYLE(mr)->look.flags.do_hilight)
  {
    /* Hilight the background. */
    if (MR_HILIGHT_WIDTH(mr) - 2 * relief_thickness > 0)
    {
      XChangeGC(dpy, Scr.ScratchGC1, Globalgcm, &Globalgcv);
      XFillRectangle(dpy, MR_WINDOW(mr), MR_STYLE(mr)->look.MenuActiveBackGC,
		     MR_HILIGHT_X_OFFSET(mr) + relief_thickness,
		     y_offset + relief_thickness,
		     MR_HILIGHT_WIDTH(mr) - 2 * relief_thickness,
		     y_height - relief_thickness);
    }
  }
  else if (MI_WAS_DESELECTED(mi) &&
	   (relief_thickness > 0 || MR_STYLE(mr)->look.flags.do_hilight))
  {
    int d = 0;
    if (MI_PREV_ITEM(mi) && MR_SELECTED_ITEM(mr) == MI_PREV_ITEM(mi))
    {
      /* Don't paint over the hilight relief. */
      d = relief_thickness;
    }
    /* Undo the hilighting. */
    XClearArea(dpy, MR_WINDOW(mr), MR_ITEM_X_OFFSET(mr), y_offset + d,
	       MR_ITEM_WIDTH(mr), y_height + relief_thickness - d, 0);
  }

  MI_WAS_DESELECTED(mi) = False;
  /* Hilight 3D */
  if (is_item_selected && relief_thickness > 0)
  {
    if (MR_HILIGHT_WIDTH(mr) - 2 * relief_thickness > 0)
    {
      /* The relief reaches down into the next item, hence the value for the
       * second y coordinate: MI_HEIGHT(mi) + 1 */
      RelieveRectangle(dpy, MR_WINDOW(mr),
		       MR_HILIGHT_X_OFFSET(mr), y_offset,
		       MR_HILIGHT_WIDTH(mr) - 1,
		       MI_HEIGHT(mi) - 1 + relief_thickness,
		       ReliefGC, ShadowGC, relief_thickness);
    }
  }


  /***************************************************************
   * Draw the item itself.
   ***************************************************************/

  /* Calculate the separator offsets. */
  if (MR_STYLE(mr)->look.flags.has_long_separators)
  {
    sx1 = MR_ITEM_X_OFFSET(mr) + relief_thickness;
    sx2 = MR_ITEM_X_OFFSET(mr) + MR_ITEM_WIDTH(mr) - 1 - relief_thickness;
  }
  else
  {
    sx1 = MR_ITEM_X_OFFSET(mr) + relief_thickness + SEPARATOR_SHORT_X_OFFSET;
    sx2 = MR_ITEM_X_OFFSET(mr) + MR_ITEM_WIDTH(mr) - 1 -
      relief_thickness - SEPARATOR_SHORT_X_OFFSET;
  }
  if (MI_IS_SEPARATOR(mi))
  {
    if (sx1 < sx2)
    {
      /* It's a separator. */
      draw_separator(MR_WINDOW(mr), ShadowGC, ReliefGC,
		     sx1, y_offset + y_height - SEPARATOR_HEIGHT,
		     sx2, y_offset + y_height - SEPARATOR_HEIGHT, 1);
      /* Nothing else to do. */
    }
    return;
  }
  else if(MI_IS_TITLE(mi))
  {
    /* Underline the title. */
    switch (MR_STYLE(mr)->look.TitleUnderlines)
    {
    case 0:
      break;
    case 1:
      if(mi != MR_FIRST_ITEM(mr))
      {
	text_y += SEPARATOR_HEIGHT;
	y = y_offset + SEPARATOR_Y_OFFSET;
	if (sx1 < sx2)
	  draw_separator(MR_WINDOW(mr), ShadowGC, ReliefGC, sx1, y, sx2, y, 1);
      }
      if(MI_NEXT_ITEM(mi) != NULL)
      {
	y = y_offset + y_height - SEPARATOR_HEIGHT;
	draw_separator(MR_WINDOW(mr), ShadowGC, ReliefGC, sx1, y, sx2, y, 1);
      }
      break;
    default:
      for (i = MR_STYLE(mr)->look.TitleUnderlines; i-- > 0; )
      {
	y = y_offset + y_height - 1 - i * UNDERLINE_HEIGHT;
	XDrawLine(dpy, MR_WINDOW(mr), ShadowGC, sx1, y, sx2, y);
      }
      break;
    }
  }

  /* Note: it's ok to pass a NULL label to check_if_function_allowed. */
  if(check_if_function_allowed(mi->func_type, fw, False, MI_LABEL(mi)[0]))
  {
    currentGC = (is_item_selected) ?
      MR_STYLE(mr)->look.MenuActiveGC : MR_STYLE(mr)->look.MenuGC;
    if (MR_STYLE(mr)->look.flags.do_hilight &&
	!MR_STYLE(mr)->look.flags.has_active_fore &&
	is_item_selected)
    {
      /* Use a lighter color for highlighted windows menu items if the
       * background is hilighted */
      currentGC = MR_STYLE(mr)->look.MenuReliefGC;
    }
  }
  else
  {
    /* should be a shaded out word, not just re-colored. */
    currentGC = MR_STYLE(mr)->look.MenuStippleGC;
  }


  /***************************************************************
   * Draw the labels.
   ***************************************************************/

  for (i = MAX_ITEM_LABELS; i-- > 0; )
  {
    if (MI_LABEL(mi)[i] && *(MI_LABEL(mi)[i]))
    {
      XDrawString(dpy, MR_WINDOW(mr), currentGC, MI_LABEL_OFFSET(mi)[i],
		  text_y, MI_LABEL(mi)[i], MI_LABEL_STRLEN(mi)[i]);
    }
    if (mi->hotkey && !MI_IS_TITLE(mi) && mi->hotkey_column == i)
    {
      /* pete@tecc.co.uk: If the item has a hot key, underline it */
      draw_underline(mr, currentGC, MI_LABEL_OFFSET(mi)[i], text_y,
		     MI_LABEL(mi)[i], mi->hotkey - 1);
    }
  }


  /***************************************************************
   * Draw the submenu triangle.
   ***************************************************************/

  if(MI_IS_POPUP(mi))
  {
    y = y_offset + (y_height - TRIANGLE_HEIGHT + relief_thickness) / 2;
    if(is_item_selected)
    {
      draw_triangle_pattern(MR_WINDOW(mr), ShadowGC, ReliefGC, ReliefGC,
			    MR_TRIANGLE_X_OFFSET(mr), y,
			    MR_TRIANGLE_X_OFFSET(mr) + TRIANGLE_WIDTH,
			    y + TRIANGLE_HEIGHT - 1,
			    MR_STYLE(mr)->look.flags.has_triangle_relief,
			    MR_IS_LEFT_TRIANGLE(mr));
    }
    else
    {
      draw_triangle_pattern(MR_WINDOW(mr), ReliefGC, ShadowGC,
			    MR_STYLE(mr)->look.MenuGC,
			    MR_TRIANGLE_X_OFFSET(mr), y,
			    MR_TRIANGLE_X_OFFSET(mr) + TRIANGLE_WIDTH,
			    y + TRIANGLE_HEIGHT - 1,
			    MR_STYLE(mr)->look.flags.has_triangle_relief,
			    MR_IS_LEFT_TRIANGLE(mr));
    }
  }

  /***************************************************************
   * Draw the item picture.
   ***************************************************************/

  if(MI_PICTURE(mi))
  {
    x = menu_middle_x_offset(mr) - MI_PICTURE(mi)->width / 2;
    y = y_offset + ((MI_IS_SELECTABLE(mi)) ? relief_thickness : 0);

    if(MI_PICTURE(mi)->depth > 0) /* pixmap? */
    {
      Globalgcm = GCClipMask | GCClipXOrigin | GCClipYOrigin;
      Globalgcv.clip_mask = MI_PICTURE(mi)->mask;
      Globalgcv.clip_x_origin = x;
      Globalgcv.clip_y_origin = y;
      XChangeGC(dpy, ReliefGC, Globalgcm, &Globalgcv);
      XCopyArea(dpy, MI_PICTURE(mi)->picture, MR_WINDOW(mr), ReliefGC,
		0, 0, MI_PICTURE(mi)->width, MI_PICTURE(mi)->height, x, y);
      Globalgcm = GCClipMask;
      Globalgcv.clip_mask = None;
      XChangeGC(dpy, ReliefGC, Globalgcm, &Globalgcv);
    }
    else
    {
      XCopyPlane(dpy, MI_PICTURE(mi)->picture, MR_WINDOW(mr), currentGC,
		 0, 0, MI_PICTURE(mi)->width, MI_PICTURE(mi)->height, x, y, 1);
    }
  }

 /***************************************************************
  * Draw the mini icons.
  ***************************************************************/

  for (i = 0; i < MAX_MINI_ICONS; i++)
  {
    int k;

    /* We need to reverse the mini icon order for left submenu style. */
    k = (MR_STYLE(mr)->feel.flags.use_left_submenus) ?
      MAX_MINI_ICONS - 1 - i : i;

    if (MI_MINI_ICON(mi)[i])
    {
      if(MI_PICTURE(mi) && MI_HAS_TEXT(mi))
      {
	y = y_offset + MI_HEIGHT(mi) - MI_MINI_ICON(mi)[i]->height;
      }
      else
      {
	y = y_offset +
	  (MI_HEIGHT(mi) + ((MI_IS_SELECTABLE(mi)) ? relief_thickness : 0) -
	   MI_MINI_ICON(mi)[i]->height) / 2;
      }
      if(MI_MINI_ICON(mi)[i]->depth > 0) /* pixmap? */
      {
	Globalgcm = GCClipMask | GCClipXOrigin | GCClipYOrigin;
	Globalgcv.clip_mask = MI_MINI_ICON(mi)[i]->mask;
	Globalgcv.clip_x_origin = MR_ICON_X_OFFSET(mr)[k];
	Globalgcv.clip_y_origin = y;
	XChangeGC(dpy,ReliefGC,Globalgcm,&Globalgcv);
	XCopyArea(
	  dpy, MI_MINI_ICON(mi)[i]->picture, MR_WINDOW(mr), ReliefGC,
	  0, 0, MI_MINI_ICON(mi)[i]->width, MI_MINI_ICON(mi)[i]->height,
	  MR_ICON_X_OFFSET(mr)[k], y);
	Globalgcm = GCClipMask;
	Globalgcv.clip_mask = None;
	XChangeGC(dpy, ReliefGC, Globalgcm, &Globalgcv);
      }
      else
      {
	XCopyPlane(
	  dpy, MI_MINI_ICON(mi)[i]->picture, MR_WINDOW(mr), currentGC,
	  0, 0, MI_MINI_ICON(mi)[i]->width, MI_MINI_ICON(mi)[i]->height,
	  MR_ICON_X_OFFSET(mr)[k], y, 1);
      }
    }
  }

  return;
}

/************************************************************
 *
 * Draws a picture on the left side of the menu
 *
 ************************************************************/
static void paint_side_pic(MenuRoot *mr)
{
  GC ReliefGC, TextGC;
  Picture *sidePic;
  int ys;
  int yt;
  int h;
  int bw = MR_BORDER_WIDTH(mr);

  if (MR_SIDEPIC(mr))
    sidePic = MR_SIDEPIC(mr);
  else if (MR_STYLE(mr)->look.sidePic)
    sidePic = MR_STYLE(mr)->look.sidePic;
  else
    return;

  if(Scr.depth < 2)
  {
    ReliefGC = MR_STYLE(mr)->look.MenuShadowGC;
  }
  else
  {
    ReliefGC = MR_STYLE(mr)->look.MenuReliefGC;
  }
  TextGC = MR_STYLE(mr)->look.MenuGC;

  if(MR_HAS_SIDECOLOR(mr))
  {
    Globalgcv.foreground = MR_SIDECOLOR(mr);
  }
  else if (MR_STYLE(mr)->look.flags.has_side_color)
  {
    Globalgcv.foreground = MR_STYLE(mr)->look.sideColor;
  }
  if (MR_HAS_SIDECOLOR(mr) || MR_STYLE(mr)->look.flags.has_side_color)
  {
    Globalgcm = GCForeground;
    XChangeGC(dpy, Scr.ScratchGC1, Globalgcm, &Globalgcv);
    XFillRectangle(dpy, MR_WINDOW(mr), Scr.ScratchGC1,
		   MR_SIDEPIC_X_OFFSET(mr), bw,
		   sidePic->width, MR_HEIGHT(mr) - 2 * bw);
  }

  if (sidePic->height > MR_HEIGHT(mr) - 2 * bw)
  {
    h = MR_HEIGHT(mr) - 2 * bw;
    ys = sidePic->height - h;
    yt = bw;
  }
  else
  {
    h = sidePic->height;
    ys = 0;
    yt = MR_HEIGHT(mr) - bw - sidePic->height;
  }

  if(sidePic->depth > 0) /* pixmap? */
  {
    Globalgcm = GCClipMask;
    Globalgcv.clip_mask = sidePic->mask;
    XChangeGC(dpy, ReliefGC, Globalgcm, &Globalgcv);
    XCopyArea(dpy, sidePic->picture, MR_WINDOW(mr), ReliefGC,
	      0, ys, sidePic->width, h, MR_SIDEPIC_X_OFFSET(mr), yt);
    Globalgcv.clip_mask = None;
    XChangeGC(dpy, ReliefGC, Globalgcm, &Globalgcv);
  }
  else
  {
    XCopyPlane(dpy, sidePic->picture, MR_WINDOW(mr), TextGC,
	       0, ys, sidePic->width, h, MR_SIDEPIC_X_OFFSET(mr), yt, 1);
  }
}



/****************************************************************************
 * Procedure:
 *	draw_underline() - Underline a character in a string (pete@tecc.co.uk)
 *
 * Calculate the pixel offsets to the start of the character position we
 * want to underline and to the next character in the string.  Shrink by
 * one pixel from each end and the draw a line that long two pixels below
 * the character...
 *
 ****************************************************************************/
static void draw_underline(MenuRoot *mr, GC gc, int x, int y, char *txt,
			   int posn)
{
  int off1 = XTextWidth(MR_STYLE(mr)->look.pStdFont->font, txt, posn);
  int off2 = XTextWidth(MR_STYLE(mr)->look.pStdFont->font, txt, posn + 1) - 1;
  XDrawLine(dpy, MR_WINDOW(mr), gc, x + off1, y + 2, x + off2, y + 2);
}

/****************************************************************************
 *
 *  Draws two horizontal lines to form a separator
 *
 ****************************************************************************/
static void draw_separator(Window w, GC TopGC, GC BottomGC, int x1, int y1,
			   int x2, int y2, int extra_off)
{
  XDrawLine(dpy, w, TopGC   , x1,           y1,  x2,          y2);
  XDrawLine(dpy, w, BottomGC, x1-extra_off, y1+1,x2+extra_off,y2+1);
}

/****************************************************************************
 *
 *  Draws a little Triangle pattern within a window
 *
 ****************************************************************************/
static void draw_triangle_pattern(
  Window w, GC ReliefGC, GC ShadowGC, GC FillGC,
  int l, int u, int r, int b, char relief, Bool is_left_triangle)
{
  int m;
  int tmp;

  m = (u + b)/2;

  /**************************************************************************
   *
   * anti-ugliness patch by Adam Rice, wysiwyg@glympton.airtime.co.uk,
   * January 28th 1998.
   *
   **************************************************************************/

  /* ensure vertical symmetry */
  if (u-m > m-b)
  {
    u=2*m-b;
  }
  else if (u-m < m-b)
  {
    b=2*m-u;
  }

  /* make (r-l)/(m-b) or its inverse be a whole number */
  if (r-l > m-b)
  {
    r=((r-l)/(m-b))*(m-b)+l;
  }
  else if (r-l < m-b)
  {
    r=(m-b)/((((m-b)-1)/(r-l))+1)+l;
  }
  if (is_left_triangle)
  {
    tmp = l;
    l = r;
    r = tmp;
  }

  if (!relief)
  {
    /* solid triangle */
    XPoint points[3];
    points[0].x = l; points[0].y = u;
    points[1].x = l; points[1].y = b;
    points[2].x = r; points[2].y = m;
    XFillPolygon(dpy, w, FillGC, points, 3, Convex, CoordModeOrigin);
  }
  else
  {
    /* relief triangle */
    XDrawLine(dpy, w, (is_left_triangle) ? ShadowGC : ReliefGC, l, u, l, b);
    XDrawLine(dpy, w, ShadowGC, l, b, r, m);
    XDrawLine(dpy, w, ReliefGC, r, m, l, u);
  }
}

/***********************************************************************
 *
 *  Procedure:
 *	paint_menu - draws the entire menu
 *
 ***********************************************************************/
void paint_menu(MenuRoot *mr, XEvent *pevent, FvwmWindow *fw)
{
  MenuItem *mi;
  MenuStyle *ms = MR_STYLE(mr);
  register int type;
  XRectangle bounds;
  int bw = MR_BORDER_WIDTH(mr);
#ifdef PIXMAP_BUTTONS
  Picture *p;
  int width, height, x, y;
#endif

#ifdef GRADIENT_BUTTONS
  Pixmap pmap;
  GC	 pmapgc;
  XGCValues gcv;
  unsigned long gcm = 0;
  gcv.line_width=3;
  gcm = GCLineWidth;
#endif

  if (MR_IS_PAINTED(mr) && pevent &&
      (pevent->xexpose.x >= MR_WIDTH(mr) - bw ||
       pevent->xexpose.x < bw ||
       pevent->xexpose.y >= MR_HEIGHT(mr) - bw ||
       pevent->xexpose.y < bw))
  {
    /* Only the border was obscured. Redraw it centrally instead of redrawing
     * several menu items. */
    RelieveRectangle(
      dpy, MR_WINDOW(mr), 0, 0, MR_WIDTH(mr) - 1, MR_HEIGHT(mr) - 1,
      (Scr.depth<2) ?
      MR_STYLE(mr)->look.MenuShadowGC : MR_STYLE(mr)->look.MenuReliefGC,
      MR_STYLE(mr)->look.MenuShadowGC, bw);
    return;
  }
  MR_IS_PAINTED(mr) = 1;
  if (ms)
  {
    type = ms->look.face.type;
    switch(type)
    {
    case SolidMenu:
      XSetWindowBackground(dpy, MR_WINDOW(mr), MR_STYLE(mr)->look.face.u.back);
      flush_expose(MR_WINDOW(mr));
      XClearWindow(dpy,MR_WINDOW(mr));
      break;
#ifdef GRADIENT_BUTTONS
    case HGradMenu:
    case VGradMenu:
    case DGradMenu:
    case BGradMenu:
      bounds.x = bw;
      bounds.y = bw;
      bounds.width = MR_WIDTH(mr) - bw;
      bounds.height = MR_HEIGHT(mr) - bw;

      if (type == HGradMenu)
      {
	if (MR_IS_BACKGROUND_SET(mr) == False)
	{
	  register int i;
	  register int dw;

	  pmap = XCreatePixmap(dpy, MR_WINDOW(mr), MR_WIDTH(mr),
			       GRADIENT_PIXMAP_THICKNESS, Scr.depth);
	  pmapgc = XCreateGC(dpy, pmap, gcm, &gcv);
	  dw = (float) (bounds.width / ms->look.face.u.grad.npixels) + 1;
	  for (i = 0; i < ms->look.face.u.grad.npixels; i++)
	  {
	    unsigned short x = i * bounds.width/ms->look.face.u.grad.npixels;
	    XSetForeground(dpy, pmapgc, ms->look.face.u.grad.pixels[i]);
	    XFillRectangle(dpy, pmap, pmapgc, x, 0, dw,
			   GRADIENT_PIXMAP_THICKNESS);
	  }
	  XSetWindowBackgroundPixmap(dpy, MR_WINDOW(mr), pmap);
	  XFreeGC(dpy,pmapgc);
	  XFreePixmap(dpy,pmap);
	  MR_IS_BACKGROUND_SET(mr) = True;
	}
	XClearWindow(dpy, MR_WINDOW(mr));
      }
      else if (type == VGradMenu)
      {
	if (MR_IS_BACKGROUND_SET(mr) == False)
	{
	  register int i;
	  register int dh;

	  pmap = XCreatePixmap(dpy, MR_WINDOW(mr), GRADIENT_PIXMAP_THICKNESS,
			       MR_HEIGHT(mr), Scr.depth);
	  pmapgc = XCreateGC(dpy, pmap, gcm, &gcv);
	  dh = (float) (bounds.height / ms->look.face.u.grad.npixels) + 1;
	  for (i = 0; i < ms->look.face.u.grad.npixels; i++)
	  {
	    unsigned short y = i * bounds.height/ms->look.face.u.grad.npixels;
	    XSetForeground(dpy, pmapgc, ms->look.face.u.grad.pixels[i]);
	    XFillRectangle(dpy, pmap, pmapgc, 0, y, GRADIENT_PIXMAP_THICKNESS,
			   dh);
	  }
	  XSetWindowBackgroundPixmap(dpy, MR_WINDOW(mr), pmap);
	  XFreeGC(dpy,pmapgc);
	  XFreePixmap(dpy,pmap);
	  MR_IS_BACKGROUND_SET(mr) = True;
	}
	XClearWindow(dpy, MR_WINDOW(mr));
      }
      else /* D or BGradient */
      {
	register int i = 0, numLines;
	int cindex = -1;

	XSetClipMask(dpy, Scr.TransMaskGC, None);
	numLines = MR_WIDTH(mr) + MR_HEIGHT(mr) - 2 * bw;
	for(i = 0; i < numLines; i++)
        {
	  if((int)(i * ms->look.face.u.grad.npixels / numLines) > cindex)
          {
	    /* pick the next colour (skip if necc.) */
	    cindex = i * ms->look.face.u.grad.npixels / numLines;
	    XSetForeground(dpy, Scr.TransMaskGC,
			   ms->look.face.u.grad.pixels[cindex]);
	  }
	  if (type == DGradMenu)
	    XDrawLine(dpy, MR_WINDOW(mr), Scr.TransMaskGC, 0, i, i, 0);
	  else /* BGradient */
	    XDrawLine(dpy, MR_WINDOW(mr), Scr.TransMaskGC,
		      0, MR_HEIGHT(mr) - 1 - i, i, MR_HEIGHT(mr) - 1);
	}
      }
      break;
#endif  /* GRADIENT_BUTTONS */
#ifdef PIXMAP_BUTTONS
    case PixmapMenu:
      p = ms->look.face.u.p;

      width = MR_WIDTH(mr) - 2 * bw;
      height = MR_HEIGHT(mr) - 2 * bw;

#if 0
      /* these flags are never set at the moment */
      x = border;
      if (ms->look.FaceStyle & HOffCenter)
      {
	if (ms->look.FaceStyle & HRight)
	  x += (int)(width - p->width);
      }
      else
	x += (int)(width - p->width) / 2;

      y = border;
      if (ms->look.FaceStyle & VOffCenter)
      {
	if (ms->look.FaceStyle & VBottom)
	  y += (int)(height - p->height);
      }
      else...
#else
      y = (int)(height - p->height) / 2;
      x = (int)(width - p->width) / 2;
#endif

      if (x < bw)
	x = bw;
      if (y < bw)
	y = bw;
      if (width > p->width)
	width = p->width;
      if (height > p->height)
	height = p->height;
      if (width > MR_WIDTH(mr) - x - bw)
	width = MR_WIDTH(mr) - x - bw;
      if (height > MR_HEIGHT(mr) - y - bw)
	height = MR_HEIGHT(mr) - y - bw;

      XSetClipMask(dpy, Scr.TransMaskGC, p->mask);
      XSetClipOrigin(dpy, Scr.TransMaskGC, x, y);
      XCopyArea(dpy, p->picture, MR_WINDOW(mr), Scr.TransMaskGC,
		bw, bw, width, height, x, y);
      break;

   case TiledPixmapMenu:
     XSetWindowBackgroundPixmap(dpy, MR_WINDOW(mr),
				ms->look.face.u.p->picture);
     flush_expose(MR_WINDOW(mr));
     XClearWindow(dpy,MR_WINDOW(mr));
     break;
#endif /* PIXMAP_BUTTONS */
    }
  }

  for (mi = MR_FIRST_ITEM(mr); mi != NULL; mi = MI_NEXT_ITEM(mi))
  {
    /* be smart about handling the expose, redraw only the entries
     * that we need to */
    if((MR_STYLE(mr)->look.face.type != SolidMenu &&
	MR_STYLE(mr)->look.face.type != SimpleMenu) ||
       pevent == NULL ||
       (pevent->xexpose.y < (MI_Y_OFFSET(mi) + MI_HEIGHT(mi)) &&
	(pevent->xexpose.y + pevent->xexpose.height) > MI_Y_OFFSET(mi)))
    {
      paint_item(mr, mi, fw, True);
    }
  }
  paint_side_pic(mr);
  XSync(dpy, 0);

  return;
}


static void FreeMenuItem(MenuItem *mi)
{
  short i;

  if (!mi)
    return;
  for (i = 0; i < MAX_ITEM_LABELS; i++)
  {
    if (MI_LABEL(mi)[i] != NULL)
      free(MI_LABEL(mi)[i]);
  }
  if (mi->action != NULL)
    free(mi->action);
  if(MI_PICTURE(mi))
    DestroyPicture(dpy,MI_PICTURE(mi));
  for (i = 0; i < MAX_MINI_ICONS; i++)
  {
    if(MI_MINI_ICON(mi)[i])
      DestroyPicture(dpy, MI_MINI_ICON(mi)[i]);
  }
  free(mi);
}

void DestroyMenu(MenuRoot *mr, Bool recreate)
{
  MenuItem *mi,*tmp2;
  MenuRoot *tmp, *prev;

  if(mr == NULL)
    return;

  /* seek menu in master list */
  tmp = Menus.all;
  prev = NULL;
  while((tmp != NULL)&&(tmp != mr))
  {
    prev = tmp;
    tmp = MR_NEXT_MENU(tmp);
  }
  if(tmp != mr)
    /* no such menu */
    return;

  if (MR_MAPPED_COPIES(mr) > 0 && MR_COPIES(mr) == 1)
  {
    /* can't destroy a menu while in use */
    fvwm_msg(ERR,"DestroyMenu", "Menu %s is in use", MR_NAME(mr));
    return;
  }

  if (MR_COPIES(mr) > 1 && MR_ORIGINAL_MENU(mr) == mr)
  {
    MenuRootDynamic *temp_mrd;
    MenuRoot *temp_mr;

    tmp = Menus.all;
    prev = NULL;
    while (tmp && MR_ORIGINAL_MENU(tmp) != mr)
    {
      prev = tmp;
      tmp = MR_NEXT_MENU(tmp);
    }
    /* build one new valid original menu structure from both menus */
    temp_mrd = mr->d;
    mr->d = tmp->d;
    tmp->d = temp_mrd;
    temp_mr = mr;
    mr = tmp;
    tmp = temp_mr;
    /* update the context to point to the new MenuRoot */
    XDeleteContext(dpy, MR_WINDOW(tmp), MenuContext);
    XSaveContext(dpy, MR_WINDOW(tmp), MenuContext, (caddr_t)tmp);
    /* now dump the parts of mr we don't need any longer */
  }

#ifdef GRADIENT_BUTTONS
      if (MR_STORED_ITEM(mr).stored)
	XFreePixmap(dpy, MR_STORED_ITEM(mr).stored);
#endif

  MR_COPIES(mr)--;
  if (MR_COPIES(mr) > 0)
  {
    recreate = False;
    /* recursively destroy the menu continuations */
    if (MR_CONTINUATION_MENU(mr))
      DestroyMenu(MR_CONTINUATION_MENU(mr), False);
  }
  else
  {
    /* free all items */
    mi = MR_FIRST_ITEM(mr);
    while(mi != NULL)
    {
      tmp2 = MI_NEXT_ITEM(mi);
      FreeMenuItem(mi);
      mi = tmp2;
    }
    if (recreate)
    {
      /* just dump the menu items but keep the menu itself */
      MR_FIRST_ITEM(mr) = NULL;
      MR_LAST_ITEM(mr) = NULL;
      MR_SELECTED_ITEM(mr) = NULL;
      MR_CONTINUATION_MENU(mr) = NULL;
      MR_PARENT_MENU(mr) = NULL;
      MR_ITEMS(mr) = 0;
#ifdef GRADIENT_BUTTONS
      memset(&(MR_STORED_ITEM(mr)), 0 , sizeof(MR_STORED_ITEM(mr)));
#endif
      MakeMenu(mr);
      return;
    }
  }

  /* unlink menu from list */
  if(prev == NULL)
    Menus.all = MR_NEXT_MENU(mr);
  else
    MR_NEXT_MENU(prev) = MR_NEXT_MENU(mr);
  XDestroyWindow(dpy,MR_WINDOW(mr));
  XDeleteContext(dpy, MR_WINDOW(mr), MenuContext);

  if (MR_COPIES(mr) == 0)
  {
    if (MR_DYNAMIC(mr).popup_action)
      free(MR_DYNAMIC(mr).popup_action);
    if (MR_DYNAMIC(mr).popdown_action)
      free(MR_DYNAMIC(mr).popdown_action);
    free(MR_NAME(mr));
    if (MR_SIDEPIC(mr))
      DestroyPicture(dpy, MR_SIDEPIC(mr));
    free(mr->s);
  }

  free(mr->d);
  free(mr);
}

void destroy_menu(F_CMD_ARGS)
{
  MenuRoot *mr;
  MenuRoot *mrContinuation;
  Bool recreate = False;

  char *token;

  token = PeekToken(action, &action);
  if (!token)
    return;
  if (StrEquals(token, "recreate"))
  {
    recreate = True;
    token = PeekToken(action, NULL);
  }
  mr = FindPopup(token);
  if (Scr.last_added_item.type == ADDED_MENU)
    set_last_added_item(ADDED_NONE, NULL);
  while (mr)
  {
    /* save continuation before destroy */
    mrContinuation = MR_CONTINUATION_MENU(mr);
    DestroyMenu(mr, recreate);
    /* Don't recreate the continuations */
    recreate = False;
    mr = mrContinuation;
  }
  return;
}


/****************************************************************************
 *
 * Generates the windows for all menus
 *
 ****************************************************************************/
void MakeMenus(void)
{
  MenuRoot *mr;

  mr = Menus.all;
  while(mr != NULL)
  {
    MakeMenu(mr);
    mr = MR_NEXT_MENU(mr);
  }
}

/****************************************************************************
 *
 * Merge menu continuations back into the original menu.
 * Called by MakeMenu().
 *
 ****************************************************************************/
static void merge_continuation_menus(MenuRoot *mr)
{
  /* merge menu continuations into one menu again - needed when changing the
   * font size of a long menu. */
  while (MR_CONTINUATION_MENU(mr) != NULL)
  {
    MenuRoot *cont = MR_CONTINUATION_MENU(mr);

    /* link first item of continuation to item before 'more...' */
    MR_LAST_ITEM(mr)->prev->next = MR_FIRST_ITEM(cont);
    MR_FIRST_ITEM(cont)->prev = MR_LAST_ITEM(mr)->prev;
    FreeMenuItem(MR_LAST_ITEM(mr));
    MR_LAST_ITEM(mr) = MR_LAST_ITEM(cont);
    MR_CONTINUATION_MENU(mr) = MR_CONTINUATION_MENU(cont);
    /* fake an empty menu so that DestroyMenu does not destroy the items. */
    MR_FIRST_ITEM(cont) = NULL;
    DestroyMenu(cont, False);
  }
  return;
}

/****************************************************************************
 *
 * Calculate the positions of the columns in the menu.
 * Called by MakeMenu().
 *
 ****************************************************************************/
static void size_menu_horizontally(MenuRoot *mr)
{
  MenuItem *mi;
  struct
  {
    unsigned short label_width[MAX_ITEM_LABELS];
    unsigned short sidepic_width;
    unsigned short icon_width[MAX_MINI_ICONS];
    unsigned short picture_width;
    unsigned short triangle_width;
    unsigned short title_width;
  } max;
  Bool sidepic_is_left = True;
  int total_width;
  int sidepic_space = 0;
  int relief_gap = 0;
  unsigned short w;
  unsigned short label_offset[MAX_ITEM_LABELS];
  char lcr_column[MAX_ITEM_LABELS];
  int i;
  int j;
  int d;
  short relief_thickness = MR_STYLE(mr)->look.ReliefThickness;
  unsigned short *item_order[MAX_ITEM_LABELS + MAX_MINI_ICONS +
			    1 /* triangle */ + 2 /* relief markers */];
  short used_objects = 0;
  short left_objects = 0;
  short right_objects = 0;
  Bool is_last_object_left = True;

  memset(&max, 0, sizeof(max));
  memset(&item_order, 0, sizeof(item_order));
  for (i = 0; i < MAX_ITEM_LABELS; i++)
    lcr_column[i] = 'l';

  /* Calculate the widths for all columns of all items. */
  for (mi = MR_FIRST_ITEM(mr); mi != NULL; mi = MI_NEXT_ITEM(mi))
  {
    if(MI_IS_POPUP(mi))
    {
      max.triangle_width = TRIANGLE_WIDTH;
    }
    else if (MI_IS_TITLE(mi) && MI_PICTURE(mi) == NULL)
    {
      Bool is_formatted = False;

      /* titles stretch over the whole menu width, so count the maximum
       * separately if the title is unformatted. */
      for (j = 1; j < MAX_ITEM_LABELS; j++)
      {
	if (MI_LABEL(mi)[j] != NULL)
	{
	  is_formatted = True;
	  break;
	}
	else
	{
	  MI_LABEL_OFFSET(mi)[j] = 0;
	}
      }
      if (!is_formatted && MI_LABEL(mi)[0] != NULL)
      {
	MI_LABEL_STRLEN(mi)[0] = strlen(MI_LABEL(mi)[0]);
	w = XTextWidth(MR_STYLE(mr)->look.pStdFont->font, MI_LABEL(mi)[0],
		       MI_LABEL_STRLEN(mi)[0]);
	/* Negative value is used to indicate a title for now. */
	MI_LABEL_OFFSET(mi)[0] = w;
	MI_IS_TITLE_CENTERED(mi) = True;
	if (max.title_width < w)
	  max.title_width = w;
	continue;
      }
    }


    for (i = 0; i < MAX_ITEM_LABELS; i++)
    {
      if (MI_LABEL(mi)[i])
      {
	MI_LABEL_STRLEN(mi)[i] = strlen(MI_LABEL(mi)[i]);
	w = XTextWidth(MR_STYLE(mr)->look.pStdFont->font, MI_LABEL(mi)[i],
		       MI_LABEL_STRLEN(mi)[i]);
	MI_LABEL_OFFSET(mi)[i] = w;
	if (max.label_width[i] < w)
	  max.label_width[i] = w;
      }
    }
    if(MI_PICTURE(mi) && max.picture_width < MI_PICTURE(mi)->width)
    {
      max.picture_width = MI_PICTURE(mi)->width;
    }
    for (i = 0; i < MAX_MINI_ICONS; i++)
    {
      int k;

      /* We need to reverse the mini icon order for left submenu style. */
      k = (MR_STYLE(mr)->feel.flags.use_left_submenus) ?
	MAX_MINI_ICONS - 1 - i : i;

      if(MI_MINI_ICON(mi)[i] && max.icon_width[k] < MI_MINI_ICON(mi)[i]->width)
      {
	max.icon_width[k] = MI_MINI_ICON(mi)[i]->width;
      }
    }
  }

  if(MR_SIDEPIC(mr))
  {
    max.sidepic_width = MR_SIDEPIC(mr)->width;
  }
  else if (MR_STYLE(mr)->look.sidePic)
  {
    max.sidepic_width = MR_STYLE(mr)->look.sidePic->width;
  }

  /* Now calculate the offsets for the columns. */
  {
    int x;
    char *format;
    unsigned char columns_placed = 0;
    unsigned char icons_placed = 0;
    Bool sidepic_placed = False;
    Bool triangle_placed = False;
    Bool relief_begin_placed = False;
    Bool relief_end_placed = False;
    Bool first = True;
    Bool done = False;
    int gap_left;
    int gap_right;
    int chars;

    format = MR_STYLE(mr)->feel.item_format;
    if (!format)
    {
      format = (MR_STYLE(mr)->feel.flags.use_left_submenus) ?
	DEFAULT_LEFT_ITEM_FORMAT : DEFAULT_ITEM_FORMAT;
    }
    /* Place the individual items off the menu in case they are not set in the
     * format string. */
    for (i = 0; i < MAX_ITEM_LABELS; i++)
    {
      label_offset[i] = 2 * Scr.MyDisplayWidth;
    }

    x = MR_BORDER_WIDTH(mr);
    while (*format && !done)
    {
      switch (*format)
      {
      case '%':
	format++;
	gap_left = 0;
	gap_right = 0;

	/* Insert a gap of %d pixels. */
	if (sscanf(format, "%d.%d%n", &gap_left, &gap_right, &chars) >= 2 ||
	    sscanf(format, "%d%n", &gap_left, &chars) >= 1 ||
	    sscanf(format, ".%d%n", &gap_right, &chars) >= 1)
	{
	  if (gap_left >= Scr.MyDisplayWidth || gap_left < 0)
	  {
	    gap_left = 0;
	  }
	  if (gap_right >= Scr.MyDisplayWidth || gap_right < 0)
	  {
	    gap_right = 0;
	  }
	  /* Skip the number. */
	  format += chars;
	}
	if (!*format)
	  break;
	switch (*format)
	{
	case 'l':
	case 'c':
	case 'r':
	  /* A left, center or right aligned column. */
	  if (columns_placed < MAX_ITEM_LABELS)
	  {
	    if (max.label_width[columns_placed] > 0)
	    {
	      lcr_column[columns_placed] = *format;
	      x += gap_left;
	      label_offset[columns_placed] = x;
	      x += max.label_width[columns_placed] + gap_right;
	      item_order[used_objects++] = &(label_offset[columns_placed]);
	      if (is_last_object_left && (*format == 'l'))
		left_objects++;
	      else
	      {
		is_last_object_left = False;
		if (*format == 'r')
		  right_objects++;
		else
		  right_objects = 0;
	      }
	    }
	    columns_placed++;
	  }
	  break;
	case 's':
	  /* the sidepic */
	  if (!sidepic_placed)
	  {
	    sidepic_placed = True;
	    if (max.sidepic_width > 0)
	    {
	      x += gap_left;
	      if (!first)
	      {
		/* Stop parsing immediately. %s must be first or last in the
		 * format string. */
		sidepic_is_left = False;
		done = True;
	      }
	      MR_SIDEPIC_X_OFFSET(mr) = x;
	      if (first)
	      {
		sidepic_is_left = True;
	      }
	      sidepic_space =
		max.sidepic_width + ((sidepic_is_left) ? gap_left : gap_right);
	      x += max.sidepic_width + gap_right;
	    }
	  }
	  break;
	case 'i':
	  /* a mini icon */
	  if (icons_placed < MAX_MINI_ICONS)
	  {
	    if (max.icon_width[icons_placed] > 0)
	    {
	      x += gap_left;
	      MR_ICON_X_OFFSET(mr)[icons_placed] = x;
	      x += max.icon_width[icons_placed] + gap_right;
	      item_order[used_objects++] =
		&(MR_ICON_X_OFFSET(mr)[icons_placed]);
	      if (is_last_object_left)
		left_objects++;
	      else
		right_objects++;
	      icons_placed++;
	    }
	  }

	  break;
	case '|':
	  if (!relief_begin_placed)
	  {
	    relief_begin_placed = True;
	    x += gap_left;
	    MR_HILIGHT_X_OFFSET(mr) = x;
	    x += relief_thickness + gap_right;
	    relief_gap += gap_right;
	    item_order[used_objects++] = &(MR_HILIGHT_X_OFFSET(mr));
	    if (is_last_object_left)
	      left_objects++;
	    else
	      right_objects = 0;
	  }
	  else if (!relief_end_placed)
	  {
	    relief_end_placed = True;
	    x += relief_thickness + gap_left;
	    /* This is a hack: for now we record the x coordinate of the
	     * end of the hilight area, but later we'll place the width in
	     * here. */
	    MR_HILIGHT_WIDTH(mr) = x;
	    x += gap_right;
	    relief_gap += gap_left;
	    item_order[used_objects++] = &(MR_HILIGHT_WIDTH(mr));
	    right_objects++;
	  }
	  break;
	case '>':
	case '<':
	  /* the triangle for popup menus */
	  if (!triangle_placed)
	  {
	    triangle_placed = True;
	    if (max.triangle_width > 0)
	    {
	      x += gap_left;
	      MR_TRIANGLE_X_OFFSET(mr) = x;
	      MR_IS_LEFT_TRIANGLE(mr) = (*format == '<');
	      x += max.triangle_width + gap_right;
	      item_order[used_objects++] = &(MR_TRIANGLE_X_OFFSET(mr));
	      if (is_last_object_left)
	      {
		if (*format == '<')
		  left_objects++;
		else
		  is_last_object_left = False;
	      }
	      else
		right_objects++;
	    }
	  }
	  break;
	case 'p':
	  /* Simply add a gap. */
	  x += gap_right + gap_left;
	  break;
	default:
	  /* Ignore unknown characters. */
	  break;
	} /* switch (*format) */
	break;
      case ' ':
      case '\t':
	/* Advance the x position. */
	x += XTextWidth(MR_STYLE(mr)->look.pStdFont->font, format, 1);
	break;
      default:
	/* Ignore unknown characters. */
	break;
      } /* switch (*format) */
      format++;
      first = False;
    } /* while (*format) */

    /* Hide unplaced parts of the menu. */
    if (!sidepic_placed)
    {
      MR_SIDEPIC_X_OFFSET(mr) = 2 * Scr.MyDisplayWidth;
    }
    for (i = icons_placed; i < MAX_MINI_ICONS; i++)
    {
      MR_ICON_X_OFFSET(mr)[i] = 2 * Scr.MyDisplayWidth;
    }
    if (!triangle_placed)
    {
      MR_TRIANGLE_X_OFFSET(mr) = 2 * Scr.MyDisplayWidth;
    }

    total_width = x - MR_BORDER_WIDTH(mr);
    d = (sidepic_space + 2 * relief_thickness + max.title_width) -
      total_width;
    if (d > 0)
    {
      /* The title is larger than all menu items. Stretch the items to match
       * the title width. */
      for (i = 0; i < used_objects; i++)
      {
	if (i >= left_objects)
	{
	  if (i >= used_objects - right_objects)
	    /* Right aligned item. */
	    *(item_order[i]) += d;
	  else
	    /* Neither left nor right aligned item. */
	    *(item_order[i]) += d / 2;
	}
      }
      total_width += d;
      if (!sidepic_is_left)
      {
	MR_SIDEPIC_X_OFFSET(mr) += d;
      }
    }
    MR_WIDTH(mr) = total_width + 2 * MR_BORDER_WIDTH(mr);
    MR_ITEM_WIDTH(mr) = total_width - sidepic_space;
    MR_ITEM_X_OFFSET(mr) = MR_BORDER_WIDTH(mr);
    if (sidepic_is_left)
      MR_ITEM_X_OFFSET(mr) += sidepic_space;
    if (!relief_begin_placed)
      MR_HILIGHT_X_OFFSET(mr) = MR_ITEM_X_OFFSET(mr);
    if (relief_end_placed)
      MR_HILIGHT_WIDTH(mr) = MR_HILIGHT_WIDTH(mr) - MR_HILIGHT_X_OFFSET(mr);
    else
      MR_HILIGHT_WIDTH(mr) = MR_ITEM_WIDTH(mr);
  }

  /* Now calculate the offsets for the individual labels. */
  for (mi = MR_FIRST_ITEM(mr); mi != NULL; mi = MI_NEXT_ITEM(mi))
  {
    for (i = 0; i < MAX_ITEM_LABELS; i++)
    {
      if (MI_LABEL(mi)[i])
      {
	if (!MI_IS_TITLE(mi) || !MI_IS_TITLE_CENTERED(mi))
	{
	  switch (lcr_column[i])
	  {
	  case 'l':
	    MI_LABEL_OFFSET(mi)[i] = label_offset[i];
	    break;
	  case 'c':
	    MI_LABEL_OFFSET(mi)[i] = label_offset[i] +
	      (max.label_width[i] - MI_LABEL_OFFSET(mi)[i]) / 2;
	    break;
	  case 'r':
	    MI_LABEL_OFFSET(mi)[i] = label_offset[i] + max.label_width[i] -
	      MI_LABEL_OFFSET(mi)[i];
	    break;
	  }
	}
	else
	{
	  /* This is a centered title item (indicated by negative width). */
	  MI_LABEL_OFFSET(mi)[i] =
	    menu_middle_x_offset(mr) - MI_LABEL_OFFSET(mi)[i] / 2;
	}
      }
    } /* for */
  } /* for */

  return;
}


/****************************************************************************
 *
 * Calculate the positions of the columns in the menu.
 * Called by MakeMenu().
 *
 ****************************************************************************/
static void size_menu_vertically(MenuRoot *mr)
{
  MenuItem *mi;
  int y;
  int cItems;
  short relief_thickness = MR_STYLE(mr)->look.ReliefThickness;
  short simple_entry_height;
  int i;

  MR_ITEM_TEXT_Y_OFFSET(mr) =
    MR_STYLE(mr)->look.pStdFont->y + relief_thickness + ITEM_TEXT_Y_OFFSET;

  simple_entry_height =	MR_STYLE(mr)->look.pStdFont->height +
    ITEM_TEXT_Y_OFFSET + ITEM_TEXT_Y_OFFSET2;

  /* mi_prev trails one behind mi, since we need to move that
     into a newly-made menu if we run out of space */
  y = MR_BORDER_WIDTH(mr);
  for (cItems = 0, mi = MR_FIRST_ITEM(mr); mi != NULL; mi = MI_NEXT_ITEM(mi), cItems++)
  {
    int separator_height;
    Bool last_item_has_relief =
      (MI_PREV_ITEM(mi)) ? MI_IS_SELECTABLE(MI_PREV_ITEM(mi)) : False;

    separator_height = (last_item_has_relief) ?
      SEPARATOR_HEIGHT + relief_thickness : SEPARATOR_TOTAL_HEIGHT;

    MI_Y_OFFSET(mi) = y;
    if(MI_IS_TITLE(mi))
    {
      MI_HEIGHT(mi) = MR_STYLE(mr)->look.pStdFont->height +
	TITLE_TEXT_Y_OFFSET + TITLE_TEXT_Y_OFFSET2;
      switch (MR_STYLE(mr)->look.TitleUnderlines)
      {
      case 0:
	if (last_item_has_relief)
	  MI_HEIGHT(mi) += relief_thickness;
	break;
      case 1:
	if(mi != MR_FIRST_ITEM(mr))
	{
	  /* Space to draw the separator plus a gap above */
	  MI_HEIGHT(mi) += separator_height;
	}
	if(MI_NEXT_ITEM(mi) != NULL)
	{
	  /* Space to draw the separator */
	  MI_HEIGHT(mi) += SEPARATOR_HEIGHT;
	}
	break;
      default:
	/* Space to draw n underlines. */
	MI_HEIGHT(mi) += UNDERLINE_HEIGHT * MR_STYLE(mr)->look.TitleUnderlines;
	if (last_item_has_relief)
	  MI_HEIGHT(mi) += relief_thickness;
	break;
      }
    }
    else if (MI_IS_SEPARATOR(mi))
    {
      /* Separator */
      MI_HEIGHT(mi) = separator_height;
    }
    else
    {
      /* Normal text entry */
      if (MI_HAS_TEXT(mi))
      {
	MI_HEIGHT(mi) = simple_entry_height + relief_thickness;
      }
      else
      {
	MI_HEIGHT(mi) = ITEM_TEXT_Y_OFFSET + ITEM_TEXT_Y_OFFSET2 +
	  relief_thickness;
      }
    }
    if(MI_PICTURE(mi))
    {
      MI_HEIGHT(mi) += MI_PICTURE(mi)->height;
    }
    for (i = 0; i < MAX_MINI_ICONS; i++)
    {
      if(MI_MINI_ICON(mi)[i] &&
	 MI_HEIGHT(mi) < MI_MINI_ICON(mi)[i]->height + relief_thickness)
      {
	MI_HEIGHT(mi) = MI_MINI_ICON(mi)[i]->height + relief_thickness;
      }
    }
    y += MI_HEIGHT(mi);
    /* this item would have to be the last item, or else
     * we need to add a "More..." entry pointing to a new menu */
    if (y + MR_BORDER_WIDTH(mr) +
	((MI_IS_SELECTABLE(mi)) ? relief_thickness : 0)
	> Scr.MyDisplayHeight)
    {
      /* Item does not fit on screen any more. */
      Bool does_fit = False;
      char *szMenuContinuationActionAndName;
      MenuRoot *menuContinuation;

      /* Remove items form the menu until it fits (plus a 'More' entry). */
      while (MI_PREV_ITEM(mi) != NULL)
      {
	/* Remove current item. */
	y -= MI_HEIGHT(mi);
	mi = MI_PREV_ITEM(mi);
	cItems--;
	if (y + MR_BORDER_WIDTH(mr) + simple_entry_height + 2*relief_thickness
	    <= Scr.MyDisplayHeight)
	{
	  /* ok, it fits now */
	  does_fit = True;
	  break;
	}
      }
      if (!does_fit || MI_PREV_ITEM(mi) == NULL)
      {
	fvwm_msg(ERR, "size_menu_vertically",
		 "Menu entry does not fit on screen");
	exit(1);
      }

      szMenuContinuationActionAndName =
	(char *)safemalloc((8+strlen(MR_NAME(mr)))*sizeof(char));
      strcpy(szMenuContinuationActionAndName,"Popup ");
      strcat(szMenuContinuationActionAndName, MR_NAME(mr));
      strcat(szMenuContinuationActionAndName,"$");
      /* NewMenuRoot inserts at the head of the list of menus
	 but, we need it at the end */
      /* (Give it just the name, which is 6 chars past the action
	 since strlen("Popup ")==6 ) */
      menuContinuation = NewMenuRoot(szMenuContinuationActionAndName + 6);
      MR_CONTINUATION_MENU(mr) = menuContinuation;

      /* Now move this item and the remaining items into the new menu */
      MR_FIRST_ITEM(menuContinuation) = MI_NEXT_ITEM(mi);
      MR_LAST_ITEM(menuContinuation) = MR_LAST_ITEM(mr);
      MR_ITEMS(menuContinuation) = MR_ITEMS(mr) - cItems;
      MI_NEXT_ITEM(mi)->prev = NULL;

      /* mi_prev is now the last item in the mirent menu */
      MR_LAST_ITEM(mr) = mi;
      MR_ITEMS(mr) = cItems;
      MI_NEXT_ITEM(mi) = NULL;

      /* And add the entry pointing to the new menu */
      AddToMenu(mr, "More&...", szMenuContinuationActionAndName,
		False /* no pixmap scan */, False);
      MakeMenu(menuContinuation);
      free(szMenuContinuationActionAndName);
    }
  } /* for */
  /* The menu may be empty here! */
  if (MR_LAST_ITEM(mr) != NULL && MI_IS_SELECTABLE(MR_LAST_ITEM(mr)))
    y += relief_thickness;
  MR_HEIGHT(mr) = y + MR_BORDER_WIDTH(mr);
}


/****************************************************************************
 *
 * Creates the window for the menu.
 *
 ****************************************************************************/
static void make_menu_window(MenuRoot *mr)
{
  unsigned long valuemask;
  XSetWindowAttributes attributes;
  unsigned int w;
  unsigned int h;

  valuemask = CWBackPixel | CWEventMask | CWCursor | CWColormap
    | CWBorderPixel | CWSaveUnder;
  attributes.border_pixel = 0;
  attributes.colormap = Scr.cmap;
  attributes.background_pixel = MR_STYLE(mr)->look.MenuColors.back;
  attributes.event_mask = (ExposureMask | EnterWindowMask);
  attributes.cursor = Scr.FvwmCursors[MENU];
  attributes.save_under = True;

  if(MR_WINDOW(mr) != None)
    XDestroyWindow(dpy,MR_WINDOW(mr));
  w = MR_WIDTH(mr);
  if (w == 0)
    w = 1;
  h = MR_HEIGHT(mr);
  if (h == 0)
    h = 1;
  MR_WINDOW(mr) = XCreateWindow(dpy, Scr.Root, 0, 0, w, h,
				(unsigned int) 0, Scr.depth, InputOutput,
				Scr.viz, valuemask, &attributes);
  XSaveContext(dpy,MR_WINDOW(mr),MenuContext,(caddr_t)mr);
}


/****************************************************************************
 *
 * Generates the window for a menu
 *
 ****************************************************************************/
void MakeMenu(MenuRoot *mr)
{
  if(!Scr.flags.windows_captured)
    return;

  merge_continuation_menus(mr);
  size_menu_vertically(mr);
  size_menu_horizontally(mr);

  MR_XANIMATION(mr) = 0;
  memset(&(MR_DYNAMIC_FLAGS(mr)), 0, sizeof(MR_DYNAMIC_FLAGS(mr)));

  /* create a new window for the menu */
  make_menu_window(mr);

  return;
}


/***********************************************************************
 * Procedure:
 *	scanForHotkeys - Look for hotkey markers in a MenuItem
 * 							(pete@tecc.co.uk)
 *
 * Inputs:
 *	it	- MenuItem to scan
 * 	column 	- The column number in which to look for a hotkey.
 *
 ***********************************************************************/
static char scanForHotkeys(MenuItem *it, int column)
{
  char *start, *txt;

  start = it->label[column];	/* Get start of string	*/
  for (txt = start; *txt != '\0'; txt++)
    {
      /* Scan whole string	*/
      if (*txt == '&')
	{		/* A hotkey marker?			*/
	  if (txt[1] == '&')
	    {	/* Just an escaped &			*/
	      char *tmp;		/* Copy the string down over it	*/
	      for (tmp = txt; *tmp != '\0';
		   tmp++) tmp[0] = tmp[1];
	      continue;		/* ...And skip to the key char		*/
	    }
	  else {
	    char ch = txt[1];
	    /* It's a hot key marker - work out the offset value */
	    it->hotkey = 1 + (txt - start);
	    it->hotkey_column = column;
	    for (; *txt != '\0'; txt++)
	      /* Copy down.. */
	      txt[0] = txt[1];
	    return ch;			/* Only one hotkey per item...	*/
	    }
	}
    }
  it->hotkey = 0;		/* No hotkey found.  Set offset to zero	*/
  return '\0';
}


/* Side picture support: this scans for a color int the menu name
   for colorization */
static void scanForColor(char *instring, Pixel *p, Bool *flag, char identifier)
{
  char *tstart, *txt, *save_instring, *name;
  int i;

  *flag = False;

  /* save instring in case can't find pixmap */
  save_instring = (char *)safemalloc(strlen(instring)+1);
  name = (char *)safemalloc(strlen(instring)+1);
  strcpy(save_instring,instring);

  /* Scan whole string        */
  for (txt = instring; *txt != '\0'; txt++)
    {
      /* A hotkey marker? */
      if (*txt == identifier)
      {
        /* Just an escaped '^'  */
        if (txt[1] == identifier)
          {
            char *tmp;                /* Copy the string down over it */
            for (tmp = txt; *tmp != '\0'; tmp++) tmp[0] = tmp[1];
            continue;         /* ...And skip to the key char          */
          }
        /* It's a hot key marker - work out the offset value          */
        tstart = txt;
        txt++;
        i=0;
        while((*txt != identifier)&&(*txt != '\0'))
          {
            name[i] = *txt;
            txt++;
            i++;
          }
        name[i] = 0;

        *p = GetColor(name);
        *flag = True;

        if(*txt != '\0')
	  txt++;
        while(*txt != '\0')
	  *tstart++ = *txt++;
        *tstart = 0;
	break;
      }
    }
  free(name);
  free(save_instring);
  return;
}

static Bool scanForPixmap(char *instring, Picture **p, char identifier)
{
  char *tstart, *txt, *name;
  int i;
  Picture *pp;

  *p = NULL;
  if (!instring)
  {
    return False;
  }

  name = (char *)safemalloc(strlen(instring)+1);

  /* Scan whole string	*/
  for (txt = instring; *txt != '\0'; txt++)
  {
    /* A hotkey marker? */
    if (*txt == identifier)
    {
      /* Just an escaped &	*/
      if (txt[1] == identifier)
      {
	char *tmp;		/* Copy the string down over it	*/
	for (tmp = txt; *tmp != '\0'; tmp++)
	  tmp[0] = tmp[1];
	continue;		/* ...And skip to the key char		*/
      }
      /* It's a hot key marker - work out the offset value		*/
      tstart = txt;
      txt++;
      i=0;
      while ((*txt != identifier)&&(*txt != '\0'))
      {
	name[i] = *txt;
	txt++;
	i++;
      }
      name[i] = 0;

      /* Next, check for a color pixmap */
      pp = CachePicture(dpy, Scr.NoFocusWin, NULL, name, Scr.ColorLimit);
      if (*txt != '\0')
	txt++;
      while (*txt != '\0')
      {
	*tstart++ = *txt++;
      }
      *tstart = 0;
      if (pp)
	*p = pp;
      else
	fvwm_msg(WARN, "scanForPixmap", "Couldn't find pixmap %s", name);
      break;
    }
  }

  free(name);
  return (*p != NULL);
}

/* FollowMenuContinuations
 * Given an menu root, return the menu root to add to by
 * following continuation links until there are no more
 */
MenuRoot *FollowMenuContinuations(MenuRoot *mr, MenuRoot **pmrPrior )
{
  *pmrPrior = NULL;
  while ((mr != NULL) &&
	 (MR_CONTINUATION_MENU(mr) != NULL))
  {
    *pmrPrior = mr;
    mr = MR_CONTINUATION_MENU(mr);
  }
  return mr;
}

/***********************************************************************
 *
 *  Procedure:
 *	AddToMenu - add an item to a root menu
 *
 *  Returned Value:
 *	(MenuItem *)
 *
 *  Inputs:
 *	menu	- pointer to the root menu to add the item
 *	item	- the text to appear in the menu
 *	action	- the string to possibly execute
 *
 * ckh - need to add boolean to say whether or not to expand for pixmaps,
 *       so built in window list can handle windows w/ * and % in title.
 *
 ***********************************************************************/
void AddToMenu(MenuRoot *menu, char *item, char *action, Bool fPixmapsOk,
	       Bool fNoPlus)
{
  MenuItem *tmp;
  char *start;
  char *end;
  char *token = NULL;
  char *token2 = NULL;
  char *option = NULL;
  int i;
  short current_mini_icon = 0;

  if ((item == NULL || *item == 0) && (action == NULL || *action == 0) &&
      fNoPlus)
    return;
  /* empty items screw up our menu when painted, so we replace them with a
   * separator */
  if (item == NULL)
    item = "";

  /***************************************************************
   * Handle dynamic actions
   ***************************************************************/

  if (StrEquals(item, "DynamicPopupAction"))
  {
    if (MR_DYNAMIC(menu).popup_action)
      free(MR_DYNAMIC(menu).popup_action);
    if (!action || *action == 0)
      MR_DYNAMIC(menu).popup_action = NULL;
    else
      MR_DYNAMIC(menu).popup_action = stripcpy(action);
    return;
  }
  else if (StrEquals(item, "DynamicPopdownAction"))
  {
    if (MR_DYNAMIC(menu).popdown_action)
      free(MR_DYNAMIC(menu).popdown_action);
    if (!action || *action == 0)
      MR_DYNAMIC(menu).popdown_action = NULL;
    else
      MR_DYNAMIC(menu).popdown_action = stripcpy(action);
    return;
  }

  /***************************************************************
   * Parse the action
   ***************************************************************/

  if (action == NULL || *action == 0)
    action = "Nop";
  GetNextToken(GetNextToken(action, &token), &option);
  tmp = (MenuItem *)safemalloc(sizeof(MenuItem));
  memset(tmp, 0, sizeof(MenuItem));
  if (MR_FIRST_ITEM(menu) == NULL)
  {
    MR_FIRST_ITEM(menu) = tmp;
    MR_LAST_ITEM(menu) = tmp;
    tmp->prev = NULL;
  }
  else if (StrEquals(token, "title") && option && StrEquals(option, "top"))
  {
    if (MR_FIRST_ITEM(menu)->action)
    {
      GetNextToken(MR_FIRST_ITEM(menu)->action, &token2);
    }
    if (StrEquals(token2, "title"))
    {
      tmp->next = MR_FIRST_ITEM(menu)->next;
      FreeMenuItem(MR_FIRST_ITEM(menu));
    }
    else
    {
      tmp->next = MR_FIRST_ITEM(menu);
    }
    if (token2)
      free(token2);
    tmp->prev = NULL;
    if (MR_FIRST_ITEM(menu) == NULL)
      MR_LAST_ITEM(menu) = tmp;
    MR_FIRST_ITEM(menu) = tmp;
  }
  else
  {
    MR_LAST_ITEM(menu)->next = tmp;
    tmp->prev = MR_LAST_ITEM(menu);
    MR_LAST_ITEM(menu) = tmp;
  }
  if (token)
    free(token);
  if (option)
    free(option);

  tmp->action = stripcpy(action);

  /***************************************************************
   * Parse the labels
   ***************************************************************/

  start = item;
  end = item;
  for (i = 0; i < MAX_ITEM_LABELS; i++, start = end)
  {
    /* Read label up to next tab. */
    if (*end)
    {
      while (*end && *end != '\t')
	/* seek next tab or end of string */
	end++;
      /* Copy the label. */
      tmp->label[i] = safemalloc(end - start + 1);
      strncpy(tmp->label[i], start, end - start);
      (tmp->label[i])[end - start] = 0;
      if (*end == '\t')
      {
	/* skip the tab */
	end++;
	if (i == MAX_ITEM_LABELS - 1)
	{
	  char *s = tmp->label[i];

	  /* Last label, remove all tabs. */
	  while (*s)
	  {
	    if (*s == '\t')
	      *s = ' ';
	  }
	}
      }
    }
    else
    {
      tmp->label[i] = NULL;
    }

    /* Parse the label. */
    if (tmp->label[i] != NULL)
    {
      if (fPixmapsOk)
      {
        if(!tmp->picture)
	{
	  if (scanForPixmap(tmp->label[i], &tmp->picture, '*'))
	    MI_HAS_PICTURE(tmp) = True;
	}
	while (current_mini_icon < MAX_MINI_ICONS)
	{
	  if (scanForPixmap(
	    tmp->label[i], &(MI_MINI_ICON(tmp)[current_mini_icon]), '%'))
	  {
	    current_mini_icon++;
	    MI_HAS_PICTURE(tmp) = True;
	  }
	  else
	    break;
	}
      }
      if (tmp->hotkey == 0)
      {
	/* pete@tecc.co.uk */
        char ch = scanForHotkeys(tmp, i);

	if (ch != '\0')
	  tmp->chHotkey = ch;
      }
      if (*(tmp->label[i]))
      {
	MI_HAS_TEXT(tmp) = True;
      }
      else
      {
	free(tmp->label[i]);
	tmp->label[i] = NULL;
      }
    }
    tmp->label_strlen[i] = (tmp->label[i]) ? strlen(tmp->label[i]) : 0;
  } /* for */

  /***************************************************************
   * Set the type flags
   ***************************************************************/

  find_func_type(tmp->action, &(tmp->func_type), NULL);
  switch (tmp->func_type)
  {
  case F_POPUP:
    MI_IS_POPUP(tmp) = True;
  case F_WINDOWLIST:
  case F_STAYSUP:
    MI_IS_MENU(tmp) = True;
    break;
  case F_TITLE:
    MI_IS_TITLE(tmp) = True;
    break;
  default:
    break;
  }
  MI_IS_SEPARATOR(tmp) = (!MI_HAS_TEXT(tmp) && !MI_HAS_PICTURE(tmp));
  if (MI_IS_SEPARATOR(tmp))
  {
    /* An empty title is handled like a separator. */
    MI_IS_TITLE(tmp) = False;
  }
  MI_IS_SELECTABLE(tmp) =
    ((MI_HAS_TEXT(tmp) || MI_HAS_PICTURE(tmp)) && !MI_IS_TITLE(tmp));


  /***************************************************************
   * misc stuff
   ***************************************************************/

  MR_ITEMS(menu)++;
}

/***********************************************************************
 *
 *  Procedure:
 *	NewMenuRoot - create a new menu root
 *
 *  Returned Value:
 *	(MenuRoot *)
 *
 *  Inputs:
 *	name	- the name of the menu root
 *
 ***********************************************************************/
MenuRoot *NewMenuRoot(char *name)
{
  MenuRoot *tmp;
  Bool flag;

  tmp = (MenuRoot *)safemalloc(sizeof(MenuRoot));
  tmp->s = (MenuRootStatic *)safemalloc(sizeof(MenuRootStatic));
  tmp->d = (MenuRootDynamic *)safemalloc(sizeof(MenuRootDynamic));

  memset(tmp->s, 0, sizeof(MenuRootStatic));
  memset(tmp->d, 0, sizeof(MenuRootDynamic));
  MR_NEXT_MENU(tmp) = Menus.all;
  MR_NAME(tmp) = stripcpy(name);
  MR_WINDOW(tmp) = None;
  scanForPixmap(MR_NAME(tmp), &MR_SIDEPIC(tmp), '@');
  scanForColor(MR_NAME(tmp), &MR_SIDECOLOR(tmp), &flag,'^');
  MR_HAS_SIDECOLOR(tmp) = flag;
  MR_STYLE(tmp) = Menus.DefaultStyle;
  MR_ORIGINAL_MENU(tmp) = tmp;
  MR_COPIES(tmp) = 1;

  Menus.all = tmp;
  return tmp;
}

/***********************************************************************
 *
 *  Procedure:
 *	NewMenuRoot - creates a new instance of an existing menu
 *
 *  Returned Value:
 *	(MenuRoot *)
 *
 *  Inputs:
 *	menu	- the MenuRoot structure of the existing menu
 *
 ***********************************************************************/
static MenuRoot *copy_menu_root(MenuRoot *menu)
{
  MenuRoot *tmp;

  if (!menu || MR_COPIES(menu) >= MAX_MENU_COPIES)
    return NULL;
  tmp = (MenuRoot *)safemalloc(sizeof(MenuRoot));
  tmp->d = (MenuRootDynamic *)safemalloc(sizeof(MenuRootDynamic));
  memset(tmp->d, 0, sizeof(MenuRootDynamic));
  tmp->s = menu->s;

  MR_COPIES(menu)++;
  MR_ORIGINAL_MENU(tmp) = MR_ORIGINAL_MENU(menu);
  MR_CONTINUATION_MENU(tmp) = copy_menu_root(MR_CONTINUATION_MENU(menu));
  MR_NEXT_MENU(tmp) = MR_NEXT_MENU(menu);
  MR_NEXT_MENU(menu) = tmp;
  MR_WINDOW(tmp) = None;
  memset(&(MR_DYNAMIC_FLAGS(menu)), 0, sizeof(MR_DYNAMIC_FLAGS(menu)));

  return tmp;
}


void SetMenuCursor(Cursor cursor)
{
  MenuRoot *mr;

  mr = Menus.all;
  for (mr = Menus.all; mr; mr = MR_NEXT_MENU(mr))
    if (MR_WINDOW(mr))
      XDefineCursor(dpy, MR_WINDOW(mr), cursor);
}

/*
 * ------------------------ Menu and Popup commands --------------------------
 */

static void menu_func(F_CMD_ARGS, Bool fStaysUp)
{
  MenuRoot *menu;
  char *ret_action = NULL;
  MenuOptions mops;
  char *menu_name = NULL;
  XEvent *teventp;
  MenuParameters mp;
  FvwmWindow *fw;
  int tc;
  extern FvwmWindow *Tmp_win;
  extern FvwmWindow *ButtonWindow;
  extern int Context;

  memset(&(mops.flags), 0, sizeof(mops.flags));
  action = GetNextToken(action,&menu_name);
  action = GetMenuOptions(action, w, tmp_win, NULL, NULL, &mops);
  while (action && *action && isspace(*action))
    action++;
  if (action && *action == 0)
    action = NULL;
  menu = FindPopup(menu_name);
  if(menu == NULL)
  {
    if(menu_name)
    {
      fvwm_msg(ERR,"menu_func","No such menu %s",menu_name);
      free(menu_name);
    }
    return;
  }
  if (menu_name &&
      set_repeat_data(menu_name, (fStaysUp) ? REPEAT_MENU : REPEAT_POPUP,NULL))
    free(menu_name);

  if (!action && eventp && eventp->type == KeyPress)
    teventp = (XEvent *)1;
  else
    teventp = eventp;

  mp.menu = menu;
  mp.parent_menu = NULL;
  mp.parent_item = NULL;
  fw = Tmp_win;
  mp.pTmp_win = &fw;
  mp.button_window = ButtonWindow;
  tc = Context;
  mp.pcontext = &tc;
  mp.flags.is_menu_from_frame_or_window_or_titlebar = False;
  mp.flags.is_sticky = fStaysUp;
  mp.flags.is_submenu = False;
  mp.flags.is_already_mapped = False;
  mp.eventp = teventp;
  mp.pops = &mops;
  mp.ret_paction = &ret_action;

  if (do_menu(&mp) == MENU_DOUBLE_CLICKED && action)
  {
    ExecuteFunction(action,tmp_win,eventp,context,*Module,EXPAND_COMMAND);
  }
}

/* the function for the "Popup" command */
void popup_func(F_CMD_ARGS)
{
  menu_func(eventp, w, tmp_win, context, action, Module, False);
}

/* the function for the "Menu" command */
void staysup_func(F_CMD_ARGS)
{
  menu_func(eventp, w, tmp_win, context, action, Module, True);
}

/*
 * -------------------------- Menu style commands ----------------------------
 */

static void FreeMenuFace(Display *dpy, MenuFace *mf)
{
  switch (mf->type)
  {
#ifdef GRADIENT_BUTTONS
  case HGradMenu:
  case VGradMenu:
  case DGradMenu:
    /* - should we check visual is not TrueColor before doing this?
     *
     *            XFreeColors(dpy, PictureCMap,
     *                        ms->u.grad.pixels, ms->u.grad.npixels,
     *                        AllPlanes); */
    free(mf->u.grad.pixels);
    mf->u.grad.pixels = NULL;
    break;
#endif

#ifdef PIXMAP_BUTTONS
  case PixmapMenu:
  case TiledPixmapMenu:
    if (mf->u.p)
      DestroyPicture(dpy, mf->u.p);
    mf->u.p = NULL;
    break;
#endif
  case SolidMenu:
    FreeColors(&mf->u.back, 1);
  default:
    break;
  }
  mf->type = SimpleMenu;
}

/*****************************************************************************
 *
 * Reads a menu face line into a structure (veliaa@rpi.edu)
 *
 ****************************************************************************/
static Boolean ReadMenuFace(char *s, MenuFace *mf, int verbose)
{
  char *style;
  char *token;
  char *action = s;

  s = GetNextToken(s, &style);
  if (style && strncasecmp(style, "--", 2) == 0)
  {
    free(style);
    return True;
  }

  FreeMenuFace(dpy, mf);
  mf->type = SimpleMenu;

  /* determine menu style */
  if (!style)
    return True;
  else if (StrEquals(style,"Solid"))
  {
    s = GetNextToken(s, &token);
    if (token)
    {
      mf->type = SolidMenu;
      mf->u.back = GetColor(token);
      free(token);
    }
    else
    {
      if(verbose)
	fvwm_msg(ERR, "ReadMenuFace", "no color given for Solid face type: %s",
		 action);
      free(style);
      return False;
    }
  }

#ifdef GRADIENT_BUTTONS
  else if (StrEquals(style,"HGradient") || StrEquals(style, "VGradient") ||
	   StrEquals(style,"DGradient") || StrEquals(style, "BGradient"))
  {
    char *item, **s_colors;
    int npixels, nsegs, i, sum, perc[MAX_GRADIENT_SEGMENTS];
    Pixel *pixels;
    char gtype = style[0];

    s = GetNextToken(s, &item);
    if (!item)
    {
      if(verbose)
	fvwm_msg(ERR, "ReadMenuFace",
		 "expected number of colors to allocate in gradient");
      free(style);
      return False;
    }
    npixels = atoi(item);
    free(item);

    s = GetNextToken(s, &item);
    if (!item)
    {
      if(verbose)
	fvwm_msg(ERR, "ReadMenuFace", "incomplete gradient style");
      free(style);
      return False;
    }

    if (!(isdigit(*item)))
    {
      s_colors = (char **)safemalloc(sizeof(char *) * 2);
      nsegs = 1;
      s_colors[0] = item;
      s = GetNextToken(s, &s_colors[1]);
      if (!s_colors[1])
      {
	if(verbose)
	  fvwm_msg(ERR, "ReadMenuFace", "incomplete gradient style");
	free(s_colors);
	free(item);
	free(style);
	return False;
      }
      perc[0] = 100;
    }
    else
    {
      nsegs = atoi(item);
      free(item);
      if (nsegs < 1)
	nsegs = 1;
      if (nsegs > MAX_GRADIENT_SEGMENTS)
	nsegs = MAX_GRADIENT_SEGMENTS;
      s_colors = (char **)safemalloc(sizeof(char *) * (nsegs + 1));
      for (i = 0; i <= nsegs; ++i)
      {
	s = GetNextToken(s, &s_colors[i]);
	if (i < nsegs)
	{
	  s = GetNextToken(s, &item);
	  perc[i] = (item) ? atoi(item) : 0;
	  if (item)
	    free(item);
	}
      }
    }

    for (i = 0, sum = 0; i < nsegs; ++i)
      sum += perc[i];

    if (sum != 100)
    {
      if(verbose)
	fvwm_msg(ERR,"ReadMenuFace", "multi gradient lenghts must sum to 100");
      for (i = 0; i <= nsegs; ++i)
	if (s_colors[i])
	  free(s_colors[i]);
      free(style);
      free(s_colors);
      return False;
    }

    if (npixels < 2)
      npixels = 2;
    if (npixels > MAX_GRADIENT_SEGMENTS)
      npixels = MAX_GRADIENT_SEGMENTS;

    pixels = AllocNonlinearGradient(s_colors, perc, nsegs, npixels);
    for (i = 0; i <= nsegs; ++i)
      if (s_colors[i])
	free(s_colors[i]);
    free(s_colors);

    if (!pixels)
    {
      if(verbose)
	fvwm_msg(ERR, "ReadMenuFace", "couldn't create gradient");
      free(style);
      return False;
    }

    mf->u.grad.pixels = pixels;
    mf->u.grad.npixels = npixels;

    switch (gtype)
    {
    case 'h':
    case 'H':
      mf->type = HGradMenu;
      break;
    case 'v':
    case 'V':
      mf->type = VGradMenu;
      break;
    case 'd':
    case 'D':
      mf->type = DGradMenu;
      break;
    default:
      mf->type = BGradMenu;
      break;
    }
  }
#endif /* GRADIENT_BUTTONS */

#ifdef PIXMAP_BUTTONS
  else if (StrEquals(style,"Pixmap") || StrEquals(style,"TiledPixmap"))
  {
    s = GetNextToken(s, &token);
    if (token)
    {
      mf->u.p = CachePicture(dpy, Scr.NoFocusWin, NULL,
			     token, Scr.ColorLimit);
      if (mf->u.p == NULL)
      {
	if(verbose)
	  fvwm_msg(ERR, "ReadMenuFace", "couldn't load pixmap %s", token);
	free(token);
	free(style);
	return False;
      }
      free(token);
      mf->type = (StrEquals(style,"TiledPixmap")) ?
	TiledPixmapMenu : PixmapMenu;
    }
    else
    {
      if(verbose)
	fvwm_msg(ERR, "ReadMenuFace", "missing pixmap name for style %s",
		 style);
	free(style);
	return False;
    }
  }
#endif /* PIXMAP_BUTTONS */

  else
  {
    if(verbose)
      fvwm_msg(ERR, "ReadMenuFace", "unknown style %s: %s", style, action);
    free(style);
    return False;
  }

  free(style);
  return True;
}

static MenuStyle *FindMenuStyle(char *name)
{
  MenuStyle *ms = Menus.DefaultStyle;

  while(ms)
  {
     if(strcasecmp(ms->name,name)==0)
       return ms;
     ms = ms->next;
  }
  return NULL;
}

static void FreeMenuStyle(MenuStyle *ms)
{
  MenuRoot *mr;
  MenuStyle *before = Menus.DefaultStyle;

  if (!ms)
    return;
  mr = Menus.all;
  while(mr)
  {
    if(MR_STYLE(mr) == ms)
      MR_STYLE(mr) = Menus.DefaultStyle;
    mr = MR_NEXT_MENU(mr);
  }
  if(ms->look.MenuGC)
    XFreeGC(dpy, ms->look.MenuGC);
  if(ms->look.MenuActiveGC)
    XFreeGC(dpy, ms->look.MenuActiveGC);
  if(ms->look.MenuActiveBackGC)
    XFreeGC(dpy, ms->look.MenuActiveBackGC);
  if(ms->look.MenuReliefGC)
    XFreeGC(dpy, ms->look.MenuReliefGC);
  if(ms->look.MenuStippleGC)
    XFreeGC(dpy, ms->look.MenuStippleGC);
  if(ms->look.MenuShadowGC)
    XFreeGC(dpy, ms->look.MenuShadowGC);
  if (ms->look.sidePic)
    DestroyPicture(dpy, ms->look.sidePic);
  if (ms->look.flags.has_side_color == 1)
    FreeColors(&ms->look.sideColor, 1);

  while(before->next != ms)
    /* Not too many checks, may segfaults in race conditions */
    before = before->next;

  before->next = ms->next;
  free(ms->name);
  free(ms);
}

void DestroyMenuStyle(F_CMD_ARGS)
{
  MenuStyle *ms = NULL;
  char *name = NULL;
  MenuRoot *mr;

  action = GetNextToken(action,&name);
  if (name == NULL)
  {
    fvwm_msg(ERR,"DestroyMenuStyle", "needs one parameter");
    return;
  }

  ms = FindMenuStyle(name);
  if(ms == NULL)
    fvwm_msg(ERR,"DestroyMenuStyle", "cannot find style %s", name);
  else if (ms == Menus.DefaultStyle)
    fvwm_msg(ERR,"DestroyMenuStyle", "cannot destroy Default Menu Face");
  else
  {
    FreeMenuFace(dpy, &ms->look.face);
    FreeMenuStyle(ms);
  }
  free(name);
  for (mr = Menus.all; mr; mr = MR_NEXT_MENU(mr))
  {
    if (MR_STYLE(mr) == ms)
      MR_STYLE(mr) = Menus.DefaultStyle;
  }
  if (!fFvwmInStartup)
  {
    MakeMenus();
  }
}

static void UpdateMenuStyle(MenuStyle *ms)
{
  XGCValues gcv;
  unsigned long gcm;

  if (ms->look.pStdFont && ms->look.pStdFont != &Scr.StdFont)
    {
      ms->look.pStdFont->y = ms->look.pStdFont->font->ascent;
      ms->look.pStdFont->height =
	ms->look.pStdFont->font->ascent + ms->look.pStdFont->font->descent;
    }

  /* calculate colors based on foreground */
  if (!ms->look.flags.has_active_fore)
    ms->look.MenuActiveColors.fore=ms->look.MenuColors.fore;

  /* calculate colors based on background */
  if (!ms->look.flags.has_active_back)
    ms->look.MenuActiveColors.back = ms->look.MenuColors.back;
  if (!ms->look.flags.has_stipple_fore)
    ms->look.MenuStippleColors.fore = ms->look.MenuColors.back;
  if(Scr.depth > 2)
  {                 /* if not black and white */
    ms->look.MenuRelief.back = GetShadow(ms->look.MenuColors.back);
    ms->look.MenuRelief.fore = GetHilite(ms->look.MenuColors.back);
  }
  else
  {                              /* black and white */
    ms->look.MenuRelief.back = GetColor("black");
    ms->look.MenuRelief.fore = GetColor("white");
  }
  ms->look.MenuStippleColors.back = ms->look.MenuColors.back;

  /* make GC's */
  gcm = GCFunction|GCFont|GCLineWidth|GCForeground|GCBackground;
  gcv.font = ms->look.pStdFont->font->fid;
  gcv.function = GXcopy;
  gcv.line_width = 0;

  gcv.foreground = ms->look.MenuRelief.fore;
  gcv.background = ms->look.MenuRelief.back;
  if(ms->look.MenuReliefGC)
    XChangeGC(dpy, ms->look.MenuReliefGC, gcm, &gcv);
  else
    ms->look.MenuReliefGC = XCreateGC(dpy, Scr.NoFocusWin, gcm, &gcv);

  gcv.foreground = ms->look.MenuRelief.back;
  gcv.background = ms->look.MenuRelief.fore;
  if(ms->look.MenuShadowGC)
    XChangeGC(dpy, ms->look.MenuShadowGC, gcm, &gcv);
  else
    ms->look.MenuShadowGC = XCreateGC(dpy, Scr.NoFocusWin, gcm, &gcv);

  gcv.foreground = (ms->look.flags.has_active_back) ?
    ms->look.MenuActiveColors.back : ms->look.MenuRelief.back;
  gcv.background = (ms->look.flags.has_active_fore) ?
    ms->look.MenuActiveColors.fore : ms->look.MenuRelief.fore;
  if(ms->look.MenuActiveBackGC)
    XChangeGC(dpy, ms->look.MenuActiveBackGC, gcm, &gcv);
  else
    ms->look.MenuActiveBackGC = XCreateGC(dpy, Scr.NoFocusWin, gcm, &gcv);

  gcv.foreground = ms->look.MenuColors.fore;
  gcv.background = ms->look.MenuColors.back;
  if(ms->look.MenuGC)
    XChangeGC(dpy, ms->look.MenuGC, gcm, &gcv);
  else
    ms->look.MenuGC = XCreateGC(dpy, Scr.NoFocusWin, gcm, &gcv);

  gcv.foreground = ms->look.MenuActiveColors.fore;
  gcv.background = ms->look.MenuActiveColors.back;
  if(ms->look.MenuActiveGC)
    XChangeGC(dpy, ms->look.MenuActiveGC, gcm, &gcv);
  else
    ms->look.MenuActiveGC = XCreateGC(dpy, Scr.NoFocusWin, gcm, &gcv);

  if(Scr.depth < 2)
  {
    gcv.fill_style = FillStippled;
    gcv.stipple = Scr.gray_bitmap;
    gcm|=GCStipple|GCFillStyle; /* no need to reset fg/bg, FillStipple wins */
  }
  else
  {
    gcv.foreground = ms->look.MenuStippleColors.fore;
    gcv.background = ms->look.MenuStippleColors.back;
  }
  if(ms->look.MenuStippleGC)
    XChangeGC(dpy, ms->look.MenuStippleGC, gcm, &gcv);
  else
    ms->look.MenuStippleGC = XCreateGC(dpy, Scr.NoFocusWin, gcm, &gcv);
}

void UpdateAllMenuStyles(void)
{
  MenuStyle *ms;

  for (ms = Menus.DefaultStyle; ms; ms = ms->next)
  {
    UpdateMenuStyle(ms);
  }
  if (!fFvwmInStartup)
  {
    MakeMenus();
  }
}


static int GetMenuStyleIndex(char *option)
{
  char *optlist[] = {
    "fvwm", "mwm", "win",
    "Foreground", "Background", "Greyed",
    "HilightBack", "HilightBackOff",
    "ActiveFore", "ActiveForeOff",
    "Hilight3DThick", "Hilight3DThin", "Hilight3DOff",
    "Animation", "AnimationOff",
    "Font",
    "MenuFace",
    "PopupDelay", "PopupOffset",
    "TitleWarp", "TitleWarpOff",
    "TitleUnderlines0", "TitleUnderlines1", "TitleUnderlines2",
    "SeparatorsLong", "SeparatorsShort",
    "TrianglesSolid", "TrianglesRelief",
    "PopupImmediately", "PopupDelayed",
    "DoubleClickTime",
    "SidePic", "SideColor",
    "PopupAsRootmenu", "PopupAsSubmenu",
    "RemoveSubmenus", "HoldSubmenus",
    "SubmenusRight", "SubmenusLeft",
    "BorderWidth",
    "Hilight3DThickness",
    "ItemFormat",
    NULL
  };
  return GetTokenIndex(option, optlist, 0, NULL);
}

static void NewMenuStyle(F_CMD_ARGS)
{
  char *name;
  char *option = NULL;
  char *optstring = NULL;
  char *nextarg;
  char *args = NULL;
  char *arg1;
  MenuStyle *ms;
  MenuStyle *tmpms;
  Bool is_initialised = True;
  Bool gc_changed = False;
  Bool is_default_style = False;
  int val[2];
  int n;
  XFontStruct *xfs = NULL;
  int i;

  action = GetNextToken(action, &name);
  if (!name)
  {
    fvwm_msg(ERR,"NewMenuStyle", "error in %s style specification",action);
    return;
  }

  tmpms = (MenuStyle *)safemalloc(sizeof(MenuStyle));
  memset(tmpms, 0, sizeof(MenuStyle));
  ms = FindMenuStyle(name);
  if (ms)
  {
    /* copy the structure over our temporary menu face. */
    memcpy(tmpms, ms, sizeof(MenuStyle));
    if (ms == Menus.DefaultStyle)
      is_default_style = True;
    free(name);
  }
  else
  {
    tmpms->name = name;
    is_initialised = False;
  }

  /* Parse the options. */
  while (action && *action)
  {
    if (is_initialised == False)
    {
      /* some default configuration goes here for the new menu style */
      tmpms->look.MenuColors.back = GetColor("white");
      tmpms->look.MenuColors.fore = GetColor("black");
      tmpms->look.pStdFont = &Scr.StdFont;
      tmpms->look.face.type = SimpleMenu;
      tmpms->look.flags.has_active_fore = 0;
      tmpms->look.flags.has_active_back = 0;
      tmpms->feel.flags.do_popup_as_root_menu = 0;
      gc_changed = True;
      option = "fvwm";
    }
    else
    {
      /* Read next option specification (delimited by a comma or \0). */
      args = action;
      action = GetQuotedString(action, &optstring, ",", NULL, NULL, NULL);
      if (!optstring)
	break;

      args = GetNextToken(optstring, &option);
      if (!option)
      {
	free(optstring);
	break;
      }
      nextarg = GetNextToken(args, &arg1);
    }

    switch((i = GetMenuStyleIndex(option)))
    {
    case 0: /* fvwm */
    case 1: /* mwm */
    case 2: /* win */
      if (i == 0)
      {
	tmpms->feel.PopupOffsetPercent = 67;
	tmpms->feel.PopupOffsetAdd = 0;
	tmpms->feel.flags.do_popup_immediately = 0;
	tmpms->feel.flags.do_title_warp = 1;
	tmpms->feel.flags.do_unmap_submenu_on_popdown = 0;
	tmpms->look.ReliefThickness = 1;
	tmpms->look.TitleUnderlines = 1;
	tmpms->look.flags.has_long_separators = 0;
	tmpms->look.flags.has_triangle_relief = 1;
	tmpms->look.flags.do_hilight = 0;
      }
      else if (i == 1)
      {
	tmpms->feel.PopupOffsetPercent = 100;
	tmpms->feel.PopupOffsetAdd = -DEFAULT_MENU_BORDER_WIDTH - 1;
	tmpms->feel.flags.do_popup_immediately = 1;
	tmpms->feel.flags.do_title_warp = 0;
	tmpms->feel.flags.do_unmap_submenu_on_popdown = 0;
	tmpms->look.ReliefThickness = 2;
	tmpms->look.TitleUnderlines = 2;
	tmpms->look.flags.has_long_separators = 1;
	tmpms->look.flags.has_triangle_relief = 1;
	tmpms->look.flags.do_hilight = 0;
      }
      else /* i == 2 */
      {
	tmpms->feel.PopupOffsetPercent = 100;
	tmpms->feel.PopupOffsetAdd = -DEFAULT_MENU_BORDER_WIDTH - 3;
	tmpms->feel.flags.do_popup_immediately = 1;
	tmpms->feel.flags.do_title_warp = 0;
	tmpms->feel.flags.do_unmap_submenu_on_popdown = 1;
	tmpms->look.ReliefThickness = 0;
	tmpms->look.TitleUnderlines = 1;
	tmpms->look.flags.has_long_separators = 0;
	tmpms->look.flags.has_triangle_relief = 0;
	tmpms->look.flags.do_hilight = 1;
      }

      /* common settings */
      tmpms->look.BorderWidth = DEFAULT_MENU_BORDER_WIDTH;
      tmpms->feel.flags.use_left_submenus = 0;
      tmpms->feel.flags.is_animated = 0;
      FreeMenuFace(dpy, &tmpms->look.face);
      tmpms->look.face.type = SimpleMenu;
      if (tmpms->look.pStdFont && tmpms->look.pStdFont != &Scr.StdFont)
      {
	XFreeFont(dpy, tmpms->look.pStdFont->font);
	free(tmpms->look.pStdFont);
      }
      tmpms->look.pStdFont = &Scr.StdFont;
      gc_changed = True;
      if (tmpms->look.flags.has_side_color == 1)
      {
	FreeColors(&tmpms->look.sideColor, 1);
	tmpms->look.flags.has_side_color = 0;
      }
      tmpms->look.flags.has_side_color = 0;
      if (tmpms->look.sidePic)
      {
	DestroyPicture(dpy, tmpms->look.sidePic);
	tmpms->look.sidePic = NULL;
      }

      if (is_initialised == False)
      {
	/* now begin the real work */
	is_initialised = True;
	continue;
      }
      break;

    case 3: /* Foreground */
      FreeColors(&tmpms->look.MenuColors.fore, 1);
      if (arg1)
	tmpms->look.MenuColors.fore = GetColor(arg1);
      else
	tmpms->look.MenuColors.fore = GetColor("black");
      gc_changed = True;
      break;

    case 4: /* Background */
      FreeColors(&tmpms->look.MenuColors.back, 1);
      if (arg1)
	tmpms->look.MenuColors.back = GetColor(arg1);
      else
	tmpms->look.MenuColors.back = GetColor("grey");
      gc_changed = True;
      break;

    case 5: /* Greyed */
      if (tmpms->look.flags.has_stipple_fore)
	FreeColors(&tmpms->look.MenuStippleColors.fore, 1);
      if (arg1 == NULL)
      {
	tmpms->look.flags.has_stipple_fore = 0;
      }
      else
      {
	tmpms->look.MenuStippleColors.fore = GetColor(arg1);
	tmpms->look.flags.has_stipple_fore = 1;
      }
      gc_changed = True;
      break;

    case 6: /* HilightBack */
      if (tmpms->look.flags.has_active_back)
	FreeColors(&tmpms->look.MenuActiveColors.back, 1);
      if (arg1 == NULL)
      {
	tmpms->look.flags.has_active_back = 0;
      }
      else
      {
	tmpms->look.MenuActiveColors.back = GetColor(arg1);
	tmpms->look.flags.has_active_back = 1;
      }
      tmpms->look.flags.do_hilight = 1;
      gc_changed = True;
      break;

    case 7: /* HilightBackOff */
      tmpms->look.flags.do_hilight = 0;
      gc_changed = True;
      break;

    case 8: /* ActiveFore */
      if (tmpms->look.flags.has_active_fore)
	FreeColors(&tmpms->look.MenuActiveColors.fore, 1);
      if (arg1 == NULL)
      {
	tmpms->look.flags.has_active_fore = 0;
      }
      else
      {
	tmpms->look.MenuActiveColors.fore = GetColor(arg1);
	tmpms->look.flags.has_active_fore = 1;
      }
      gc_changed = True;
      break;

    case 9: /* ActiveForeOff */
      tmpms->look.flags.has_active_fore = 0;
      gc_changed = True;
      break;

    case 10: /* Hilight3DThick */
      tmpms->look.ReliefThickness = 2;
      break;

    case 11: /* Hilight3DThin */
      tmpms->look.ReliefThickness = 1;
      break;

    case 12: /* Hilight3DOff */
      tmpms->look.ReliefThickness = 0;
      break;

    case 13: /* Animation */
      tmpms->feel.flags.is_animated = 1;
      break;

    case 14: /* AnimationOff */
      tmpms->feel.flags.is_animated = 0;
      break;

    case 15: /* Font */
      if (arg1 && (xfs = GetFontOrFixed(dpy, arg1)) == NULL)
      {
	fvwm_msg(ERR,"NewMenuStyle",
		 "Couldn't load font '%s' or 'fixed'\n", arg1);
	break;
      }
      if (tmpms->look.pStdFont && tmpms->look.pStdFont != &Scr.StdFont)
      {
	if (tmpms->look.pStdFont->font)
	  XFreeFont(dpy, tmpms->look.pStdFont->font);
	free(tmpms->look.pStdFont);
      }
      if (arg1 == NULL)
      {
	/* reset to screen font */
	tmpms->look.pStdFont = &Scr.StdFont;
      }
      else
      {
	tmpms->look.pStdFont = (MyFont *)safemalloc(sizeof(MyFont));
	tmpms->look.pStdFont->font = xfs;
      }
      gc_changed = True;
      break;

    case 16: /* MenuFace */
      while (args && *args != '\0' && isspace(*args))
	args++;
      ReadMenuFace(args, &tmpms->look.face, True);
      break;

    case 17: /* PopupDelay */
      if (GetIntegerArguments(args, NULL, val, 1) == 0 || *val < 0)
	Menus.PopupDelay10ms = DEFAULT_POPUP_DELAY;
      else
	Menus.PopupDelay10ms = (*val+9)/10;
      if (!is_default_style)
      {
	fvwm_msg(WARN, "NewMenuStyle",
		 "PopupDelay applied to style '%s' will affect all menus",
		 tmpms->name);
      }
      break;

    case 18: /* PopupOffset */
      if ((n = GetIntegerArguments(args, NULL, val, 2)) == 0)
      {
	fvwm_msg(ERR,"NewMenuStyle",
		 "PopupOffset requires one or two arguments");
      }
      else
      {
	tmpms->feel.PopupOffsetAdd = val[0];
	if (n == 2 && val[1] <= 100 && val[1] >= 0)
	  tmpms->feel.PopupOffsetPercent = val[1];
	else
	  tmpms->feel.PopupOffsetPercent = 100;
      }
      break;

    case 19: /* TitleWarp */
      tmpms->feel.flags.do_title_warp = 1;
      break;

    case 20: /* TitleWarpOff */
      tmpms->feel.flags.do_title_warp = 0;
      break;

    case 21: /* TitleUnderlines0 */
      tmpms->look.TitleUnderlines = 0;
      break;

    case 22: /* TitleUnderlines1 */
      tmpms->look.TitleUnderlines = 1;
      break;

    case 23: /* TitleUnderlines2 */
      tmpms->look.TitleUnderlines = 2;
      break;

    case 24: /* SeparatorsLong */
      tmpms->look.flags.has_long_separators = 1;
      break;

    case 25: /* SeparatorsShort */
      tmpms->look.flags.has_long_separators = 0;
      break;

    case 26: /* TrianglesSolid */
      tmpms->look.flags.has_triangle_relief = 0;
      break;

    case 27: /* TrianglesRelief */
      tmpms->look.flags.has_triangle_relief = 1;
      break;

    case 28: /* PopupImmediately */
      tmpms->feel.flags.do_popup_immediately = 1;
      break;

    case 29: /* PopupDelayed */
      tmpms->feel.flags.do_popup_immediately = 0;
      break;

    case 30: /* DoubleClickTime */
      if (GetIntegerArguments(args, NULL, val, 1) == 0 || *val < 0)
	Menus.DoubleClickTime = DEFAULT_MENU_CLICKTIME;
      else
	Menus.DoubleClickTime = *val;
      if (!is_default_style)
      {
	fvwm_msg(WARN, "NewMenuStyle",
		 "DoubleClickTime for style '%s' will affect all menus",
		 tmpms->name);
      }
      break;

    case 31: /* SidePic */
      if (tmpms->look.sidePic)
      {
	DestroyPicture(dpy, tmpms->look.sidePic);
	tmpms->look.sidePic = NULL;
      }
      if (arg1)
      {
	tmpms->look.sidePic = CachePicture(dpy, Scr.NoFocusWin, NULL,
					   arg1, Scr.ColorLimit);
	if (!tmpms->look.sidePic)
	  fvwm_msg(WARN, "NewMenuStyle", "Couldn't find pixmap %s", arg1);
      }
      break;

    case 32: /* SideColor */
      if (tmpms->look.flags.has_side_color == 1)
      {
	FreeColors(&tmpms->look.sideColor, 1);
	tmpms->look.flags.has_side_color = 0;
      }
      if (arg1)
      {
	tmpms->look.sideColor = GetColor(arg1);
	tmpms->look.flags.has_side_color = 1;
      }
      break;

    case 33: /* PopupAsRootmenu */
      tmpms->feel.flags.do_popup_as_root_menu = 1;
      break;

    case 34: /* PopupAsSubmenu */
      tmpms->feel.flags.do_popup_as_root_menu = 0;
      break;

    case 35: /* RemoveSubmenus */
      tmpms->feel.flags.do_unmap_submenu_on_popdown = 1;
      break;

    case 36: /* HoldSubmenus */
      tmpms->feel.flags.do_unmap_submenu_on_popdown = 0;
      break;

    case 37: /* SubmenusRight */
      tmpms->feel.flags.use_left_submenus = 0;
      break;

    case 38: /* SubmenusLeft */
      tmpms->feel.flags.use_left_submenus = 1;
      break;

    case 39: /* BorderWidth */
      if (GetIntegerArguments(args, NULL, val, 1) == 0 ||
	  *val < 0 || *val > MAX_MENU_BORDER_WIDTH)
	tmpms->look.BorderWidth = DEFAULT_MENU_BORDER_WIDTH;
      else
	tmpms->look.BorderWidth = *val;
      break;

    case 40: /* Hilight3DThickness */
      if (GetIntegerArguments(args, NULL, val, 1) > 0 &&
	  *val >= 0 && *val <= MAX_ITEM_RELIEF_THICKNESS)
	tmpms->look.ReliefThickness = *val;
      break;

    case 41: /* ItemFormat */
      if (tmpms->feel.item_format)
      {
	free(tmpms->feel.item_format);
	tmpms->feel.item_format = NULL;
      }
      if (arg1)
      {
	tmpms->feel.item_format = strdup(arg1);
      }
      break;

#if 0
    case 99: /* PositionHints */
      break;
#endif

    default:
      fvwm_msg(ERR,"NewMenuStyle", "unknown option '%s'", option);
      break;
    } /* switch */

    if (option)
    {
      free(option);
      option = NULL;
    }
    free(optstring);
    optstring = NULL;
    if (arg1)
    {
      free(arg1);
      arg1 = NULL;
    }
  } /* while */

  if (gc_changed)
  {
    UpdateMenuStyle(tmpms);
  } /* if (gc_changed) */

  if(Menus.DefaultStyle == NULL)
  {
    /* First MenuStyle MUST be the default style */
    Menus.DefaultStyle = tmpms;
    tmpms->next = NULL;
  }
  else if (ms)
  {
    /* copy our new menu face over the old one */
    memcpy(ms, tmpms, sizeof(MenuStyle));
    free(tmpms);
  }
  else
  {
    MenuStyle *before = Menus.DefaultStyle;

    /* add a new menu face to list */
    tmpms->next = NULL;
    while(before->next)
      before = before->next;
    before->next = tmpms;
  }

  if (!fFvwmInStartup)
  {
    MakeMenus();
  }
  return;
}

static void OldMenuStyle(F_CMD_ARGS)
{
  char *buffer, *rest;
  char *fore, *back, *stipple, *font, *style, *animated;

  rest = GetNextToken(action,&fore);
  rest = GetNextToken(rest,&back);
  rest = GetNextToken(rest,&stipple);
  rest = GetNextToken(rest,&font);
  rest = GetNextToken(rest,&style);
  rest = GetNextToken(rest,&animated);

  if(!fore || !back || !stipple || !font || !style)
  {
    fvwm_msg(ERR,"OldMenuStyle", "error in %s style specification", action);
  }
  else
  {
    buffer = (char *)safemalloc(strlen(action) + 100);
    sprintf(buffer,
	    "* %s, Foreground %s, Background %s, Greyed %s, Font %s, %s",
	    style, fore, back, stipple, font,
	    (animated && StrEquals(animated, "anim")) ?
	    "Animation" : "AnimationOff");
    NewMenuStyle(eventp, w, tmp_win, context, buffer, Module);
    free(buffer);
  }

  if(fore)
    free(fore);
  if(back)
    free(back);
  if(stipple)
    free(stipple);
  if(font)
    free(font);
  if(style)
    free(style);
  if(animated)
    free(animated);
}


void SetMenuStyle(F_CMD_ARGS)
{
  char *option;

  GetNextSimpleOption(SkipNTokens(action, 1), &option);
  if (option == NULL || GetMenuStyleIndex(option) != -1)
    NewMenuStyle(eventp, w, tmp_win, context, action, Module);
  else
    OldMenuStyle(eventp, w, tmp_win, context, action, Module);
  if (option)
    free(option);
  return;
}


void ChangeMenuStyle(F_CMD_ARGS)
{
  char *name = NULL, *menuname = NULL;
  MenuStyle *ms = NULL;
  MenuRoot *mr = NULL;


  action = GetNextToken(action,&name);
  if (name == NULL)
  {
    fvwm_msg(ERR,"ChangeMenuStyle", "needs at least two parameters");
    return;
  }

  ms = FindMenuStyle(name);
  if(ms == NULL)
  {
    fvwm_msg(ERR,"ChangeMenuStyle", "cannot find style %s", name);
    free(name);
    return;
  }
  free(name);

  action = GetNextToken(action,&menuname);
  while(menuname && *menuname)
  {
    mr = FindPopup(menuname);
    if(mr == NULL)
    {
      fvwm_msg(ERR,"ChangeMenuStyle", "cannot find menu %s", menuname);
      free(menuname);
      break;
    }
    MR_STYLE(mr) = ms;
    if (!fFvwmInStartup)
    {
      MakeMenu(mr);
    }
    free(menuname);
    action = GetNextToken(action,&menuname);
  }
}

void add_item_to_menu(F_CMD_ARGS)
{
  MenuRoot *mr;
  MenuRoot *mrPrior;
  char *token, *rest,*item;

  rest = GetNextToken(action,&token);
  if (!token)
    return;
  mr = FollowMenuContinuations(FindPopup(token),&mrPrior);
  if(mr == NULL)
    mr = NewMenuRoot(token);

  /* Set + state to last menu */
  set_last_added_item(ADDED_MENU, mr);

  rest = GetNextToken(rest,&item);
  AddToMenu(mr, item,rest,True /* pixmap scan */, True);
  if (item)
    free(item);
  /* These lines are correct! We must not release token if the string is
     empty.  WHY??
   * It cannot be NULL! GetNextToken never returns an empty string! */
  if (*token)
    free(token);

  if (!fFvwmInStartup)
  {
    MakeMenu(mr);
  }
  return;
}

void add_another_menu_item(char *action)
{
  MenuRoot *mr;
  MenuRoot *mrPrior;
  char *rest,*item;

  mr = FollowMenuContinuations(Scr.last_added_item.item ,&mrPrior);
  if(mr == NULL)
    return;
  rest = GetNextToken(action,&item);
  AddToMenu(mr, item,rest,True /* pixmap scan */, False);
  if (item)
    free(item);
  if (!fFvwmInStartup)
  {
    MakeMenu(mr);
  }
}


static int float_to_int_with_tolerance(float f)
{
  int low;

  if (f < 0)
    low = (int)(f - ROUNDING_ERROR_TOLERANCE);
  else
    low = (int)(f + ROUNDING_ERROR_TOLERANCE);
  if ((int)f != low)
    return low;
  else
    return (int)(f);
}

static void get_xy_from_position_hints(
  MenuPosHints *ph, int width, int height, int context_width,
  Bool do_reverse_x, int *ret_x, int *ret_y)
{
  float x_add;
  float y_add;

  *ret_x = ph->x;
  *ret_y = ph->y;
  if (ph->is_menu_relative)
  {
    if (do_reverse_x)
    {
      *ret_x -= ph->x_offset;
      x_add =
	width * (-1.0 - ph->x_factor) +
	ph->menu_width * (1.0 - ph->context_x_factor);
    }
    else
    {
      *ret_x += ph->x_offset;
      x_add =
	width * ph->x_factor +
	ph->menu_width * ph->context_x_factor;
    }
    y_add = height * ph->y_factor;
  }
  else
  {
    x_add = width * ph->x_factor;
    y_add = height * ph->y_factor;
  }
  *ret_x += float_to_int_with_tolerance(x_add);
  *ret_y += float_to_int_with_tolerance(y_add);
}

/*****************************************************************************
 * Used by GetMenuOptions
 *
 * The vars are named for the x-direction, but this is used for both x and y
 *****************************************************************************/
static char *get_one_menu_position_argument(
  char *action, int x, int w, int *pFinalX, int *x_offset, float *width_factor,
  float *context_width_factor, Bool *is_menu_relative)
{
  char *token, *orgtoken, *naction;
  char c;
  int val;
  int chars;
  float fval;
  float factor = (float)w/100;
  float x_add = 0;

  naction = GetNextToken(action, &token);
  if (token == NULL)
    return action;
  orgtoken = token;
  *pFinalX = x;
  *x_offset = 0;
  *width_factor = 0.0;
  *context_width_factor = 0.0;
  if (sscanf(token,"o%d%n", &val, &chars) >= 1)
  {
    fval = val;
    token += chars;
    x_add += fval*factor;
    *width_factor -= fval / 100.0;
    *context_width_factor += fval / 100.0;
  }
  else if (token[0] == 'c')
  {
    token++;
    x_add += ((float)w) / 2.0;
    *width_factor -= 0.5;
    *context_width_factor += 0.5;
  }
  while (*token != 0)
  {
    if (sscanf(token,"%d%n", &val, &chars) >= 1)
    {
      fval = val;
      token += chars;
      if (sscanf(token,"%c", &c) == 1)
      {
	switch (c)
	{
	case 'm':
	  token++;
	  *width_factor += fval / 100.0;
	  *is_menu_relative = True;
	  break;
	case 'p':
	  token++;
	  x_add += val;
	  *x_offset += val;
	  break;
	default:
	  x_add += fval * factor;
	  *context_width_factor += fval / 100.0;
	  break;
	}
      }
      else
      {
	x_add += fval * factor;
	*context_width_factor += fval / 100.0;
      }
    }
    else
    {
      naction = action;
      break;
    }
  }
  *pFinalX += float_to_int_with_tolerance(x_add);
  free(orgtoken);
  return naction;
}

/*****************************************************************************
 * GetMenuOptions is used for Menu, Popup and WindowList
 * It parses strings matching
 *
 *   [ [context-rectangle] x y ] [special-options] [other arguments]
 *
 * and returns a pointer to the first part of the input string that doesn't
 * match this syntax.
 *
 * See documentation for a detailed description.
 ****************************************************************************/
char *GetMenuOptions(char *action, Window w, FvwmWindow *tmp_win,
		     MenuRoot *mr, MenuItem *mi, MenuOptions *pops)
{
  char *tok = NULL;
  char *naction = action;
  char *taction;
  int x;
  int y;
  int button;
  int gflags;
  unsigned int width;
  unsigned int height;
  int dummy_int;
  float dummy_float;
  Bool dummy_flag;
  Window context_window = 0;
  Bool fHasContext, fUseItemOffset;
  Bool fValidPosHints =
    last_saved_pos_hints.flags.is_last_menu_pos_hints_valid;
  /* If this is set we may want to reverse the position hints, so don't sum up
   * the totals right now. This is useful for the SubmenusLeft style. */

  last_saved_pos_hints.flags.is_last_menu_pos_hints_valid = False;
  if (pops == NULL)
  {
    fvwm_msg(ERR,"GetMenuOptions","no MenuOptions pointer passed");
    return action;
  }

  taction = action;
  memset(&(pops->flags), 0, sizeof(pops->flags));
  pops->flags.has_poshints = 0;
  while (action != NULL)
  {
    /* ^ just to be able to jump to end of loop without 'goto' */
    gflags = NoValue;
    pops->pos_hints.is_relative = False;
    pops->pos_hints.menu_width = 0;
    pops->pos_hints.is_menu_relative = False;
    /* parse context argument (if present) */
    naction = GetNextToken(taction, &tok);
    if (!tok)
    {
      /* no context string */
      fHasContext = False;
      break;
    }

    pops->pos_hints.is_relative = True; /* set to False for absolute hints! */
    fUseItemOffset = False;
    fHasContext = True;
    if (StrEquals(tok, "context"))
    {
      if (mi && mr)
	context_window = MR_WINDOW(mr);
      else if (tmp_win)
      {
	if (IS_ICONIFIED(tmp_win))
	  context_window=tmp_win->icon_pixmap_w;
	else
	  context_window = tmp_win->frame;
      }
      else
	context_window = w;
    }
    else if (StrEquals(tok,"menu"))
    {
      if (mr)
      {
	context_window = MR_WINDOW(mr);
	pops->pos_hints.is_menu_relative = True;
	pops->pos_hints.menu_width = MR_WIDTH(mr);
      }
    }
    else if (StrEquals(tok,"item"))
    {
      if (mi && mr)
      {
	context_window = MR_WINDOW(mr);
	fUseItemOffset = True;
	pops->pos_hints.is_menu_relative = True;
	pops->pos_hints.menu_width = MR_WIDTH(mr);
      }
    }
    else if (StrEquals(tok,"icon"))
    {
      if (tmp_win)
	context_window = tmp_win->icon_pixmap_w;
    }
    else if (StrEquals(tok,"window"))
    {
      if (tmp_win)
	context_window = tmp_win->frame;
    }
    else if (StrEquals(tok,"interior"))
    {
      if (tmp_win)
	context_window = tmp_win->w;
    }
    else if (StrEquals(tok,"title"))
    {
      if (tmp_win)
      {
	if (IS_ICONIFIED(tmp_win))
	  context_window = tmp_win->icon_w;
	else
	  context_window = tmp_win->title_w;
      }
    }
    else if (strncasecmp(tok,"button",6) == 0)
    {
      if (sscanf(&(tok[6]),"%d",&button) != 1 ||
		 tok[6] == '+' || tok[6] == '-' || button < 0 || button > 9)
      {
	fHasContext = False;
      }
      else if (tmp_win)
      {
	if (button == 0)
	  button = 10;
	if (button & 0x01)
	  context_window = tmp_win->left_w[button/2];
	else
	  context_window = tmp_win->right_w[button/2-1];
      }
    }
    else if (StrEquals(tok,"root"))
    {
      context_window = Scr.Root;
      pops->pos_hints.is_relative = False;
    }
    else if (StrEquals(tok,"mouse"))
    {
      context_window = 0;
    }
    else if (StrEquals(tok,"rectangle"))
    {
      int flags;
      /* parse the rectangle */
      free(tok);
      naction = GetNextToken(taction, &tok);
      if (tok == NULL)
      {
	fvwm_msg(ERR,"GetMenuOptions","missing rectangle geometry");
	return action;
      }
      flags = XParseGeometry(tok, &x, &y, &width, &height);
      if ((flags & AllValues) != AllValues)
      {
	free(tok);
	fvwm_msg(ERR,"GetMenuOptions","invalid rectangle geometry");
	return action;
      }
      if (flags & XNegative)
	x = Scr.MyDisplayWidth - x - width;
      if (flags & YNegative)
	y = Scr.MyDisplayHeight - y - height;
      pops->pos_hints.is_relative = False;
    }
    else if (StrEquals(tok,"this"))
    {
      MenuRoot *dummy_mr;

      context_window = w;
      if (XFindContext(dpy, w, MenuContext, (caddr_t *)&dummy_mr) != XCNOENT)
      {
	pops->pos_hints.is_menu_relative = True;
	pops->pos_hints.menu_width = MR_WIDTH(mr);
      }
    }
    else
    {
      /* no context string */
      fHasContext = False;
    }

    if (tok)
      free(tok);
    if (fHasContext)
      taction = naction;
    else
      naction = action;

    if (!context_window || !fHasContext ||
	!XGetGeometry(dpy, context_window, &JunkRoot, &JunkX, &JunkY,
		      &width, &height, &JunkBW, &JunkDepth) ||
	!XTranslateCoordinates(
	  dpy, context_window, Scr.Root, 0, 0, &x, &y, &JunkChild))
    {
      /* now window or could not get geometry */
      XQueryPointer(dpy,Scr.Root, &JunkRoot, &JunkChild,&x,&y, &JunkX, &JunkY,
		    &JunkMask);
      width = height = 1;
    }
    else if (fUseItemOffset)
    {
      y += MI_Y_OFFSET(mi);
      height = MI_HEIGHT(mi);
    }

    /* parse position arguments */
    taction = get_one_menu_position_argument(
      naction, x, width, &(pops->pos_hints.x), &(pops->pos_hints.x_offset),
      &(pops->pos_hints.x_factor), &(pops->pos_hints.context_x_factor),
      &pops->pos_hints.is_menu_relative);
    if (pops->pos_hints.is_menu_relative)
    {
      pops->pos_hints.x = x;
      if (pops->pos_hints.menu_width == 0 && mr)
      {
	pops->pos_hints.menu_width = MR_WIDTH(mr);
      }
    }
    naction = get_one_menu_position_argument(
      taction, y, height, &(pops->pos_hints.y), &dummy_int,
      &(pops->pos_hints.y_factor), &dummy_float, &dummy_flag);
    if (naction == taction)
    {
      /* argument is missing or invalid */
      if (fHasContext)
	fvwm_msg(ERR,"GetMenuOptions","invalid position arguments");
      naction = action;
      taction = action;
      break;
    }
    taction = naction;
    pops->flags.has_poshints = 1;
    if (fValidPosHints == True && pops->pos_hints.is_relative == True)
    {
      pops->pos_hints = last_saved_pos_hints.pos_hints;
    }
    /* we want to do this only once */
    break;
  } /* while (1) */

  if (!pops->flags.has_poshints && fValidPosHints)
  {
    DBUG("GetMenuOptions","recycling position hints");
    pops->flags.has_poshints = 1;
    pops->pos_hints = last_saved_pos_hints.pos_hints;
    pops->pos_hints.is_relative = False;
  }

  action = naction;
  /* to keep Purify silent */
  pops->flags.select_in_place = 0;
  /* parse additional options */
  while (naction && *naction)
  {
    naction = GetNextToken(action, &tok);
    if (!tok)
      break;
    if (StrEquals(tok, "WarpTitle"))
    {
      pops->flags.warp_title = 1;
      pops->flags.no_warp = 0;
    }
    else if (StrEquals(tok, "NoWarp"))
    {
      pops->flags.warp_title = 0;
      pops->flags.no_warp = 1;
    }
    else if (StrEquals(tok, "Fixed"))
    {
      pops->flags.fixed = 1;
    }
    else if (StrEquals(tok, "SelectInPlace"))
    {
      pops->flags.select_in_place = 1;
    }
    else if (StrEquals(tok, "SelectWarp"))
    {
      pops->flags.select_warp = 1;
    }
    else
    {
      free (tok);
      break;
    }
    action = naction;
    free (tok);
  }
  if (!pops->flags.select_in_place)
  {
    pops->flags.select_warp = 0;
  }

  return action;
}
