/*
 * unclutter: 
 * level 0: initial release
 * level 1: -grab option to use old method, new method creates small input
 *	    only sub-window. (distributed by mail only, not posted)
 * level 2: use Andreas Stolcke's vroot.h for tvtwm and similar winmans.
 * level 3: -not option takes list of windows to avoid and -visible ignores
 *	    visibility events for some servers.
 * level 4: create an unused window so that xdm can find us to kill.
 * level 5: send an EnterNotify pseudo event to client to pretend did not leave
 *	    window.  Useful for emacs.  -noevents option will cancel.
 * level 6: replace XQueryPointer by looking for EnterNotify event, else
 *	    get confused during button grabs and never remove cursor again.
 *	    [Bug found and fixed thanks to Charles Hannum <mycroft@ai.mit.edu>]
 * level 7: manage all screens on display.
 * level 8: -keystroke option from Bill Trost trost@cloud.rain.com.
 */
#define PATCHLEVEL 8
