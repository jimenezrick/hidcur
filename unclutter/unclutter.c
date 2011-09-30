/*
 * unclutter: remove idle cursor image from screen so that it doesnt
 * obstruct the area you are looking at.
 * doesn't do it if cursor is in root window or a button is down.
 * polls mouse to see if is stationary, or waits for a keyup event on the
 * screen.  These will only arrive in windows of applications that dont
 * wait for keyup themselves.  We could only do better by using the XTest
 * extensions and so getting all keystroke events.
 * Tries to cope with jitter if you have a mouse that twitches.
 * Unfortunately, clients like emacs set different text cursor
 * shapes depending on whether they have pointer focus or not.
 * Try to kid them with a synthetic EnterNotify event.
 * Whereas version 1 did a grab cursor, version 2 creates a small subwindow.
 * This may work better with some window managers.
 * Some servers return a Visibility event when the subwindow is mapped.
 * Sometimes this is Unobscured, or even FullyObscured. Ignore these and
 * rely on LeaveNotify events. (An InputOnly window is not supposed to get
 * visibility events.)
 * Mark M Martin. cetia feb 1994  mmm@cetia.fr
 * keystroke code from Bill Trost trost@cloud.rain.com
 */
#include <X11/Xos.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xproto.h>
#include <stdio.h>
#include "vroot.h"

char *progname;
pexit(str)char *str;{
    fprintf(stderr,"%s: %s\n",progname,str);
    exit(1);
}
usage(){
    pexit("usage:\n\
	-display <display>\n\
	-idle <seconds>		time between polls to detect idleness.\n\
	-keystroke		wait for keystroke before idling.\n\
	-jitter <pixels>	pixels mouse can twitch without moving\n\
	-grab			use grabpointer method not createwindow\n\
	-reset			reset the timer whenever cursor becomes\n\
					visible even if it hasn't moved\n\
 	-root	       		apply to cursor on root window too\n\
	-onescreen		apply only to given screen of display\n\
 	-visible       		ignore visibility events\n\
 	-noevents      		dont send pseudo events\n\
	-not names...		dont apply to windows whose wm-name begins.\n\
				(must be last argument)");
}

#define ALMOSTEQUAL(a,b) (abs(a-b)<=jitter)
#define ANYBUTTON (Button1Mask|Button2Mask|Button3Mask|Button4Mask|Button5Mask)

/* Since the small window we create is a child of the window the pointer is
 * in, it can be destroyed by its adoptive parent.  Hence our destroywindow()
 * can return an error, saying it no longer exists.  Similarly, the parent
 * window can disappear while we are trying to create the child. Trap and
 * ignore these errors.
 */
int (*defaulthandler)();
int errorhandler(display,error)
Display *display;
XErrorEvent *error;
{
    if(error->error_code!=BadWindow)
	(*defaulthandler)(display,error);
}

char **names;	/* -> argv list of names to avoid */

/*
 * return true if window has a wm_name and the start of it matches
 * one of the given names to avoid
 */
nameinlist(display,window)
Display *display;
Window window;
{
    char **cpp;
    char *name;

    if(names==0)return 0;
    if(XFetchName (display, window, &name)){
	for(cpp = names;*cpp!=0;cpp++){
	    if(strncmp(*cpp,name,strlen(*cpp))==0)
		break;
	}
	XFree(name);
	return(*cpp!=0);
    }
    return 0;
}	
/*
 * create a small 1x1 curssor with all pixels masked out on the given screen.
 */
createnullcursor(display,root)
Display *display;
Window root;
{
    Pixmap cursormask;
    XGCValues xgc;
    GC gc;
    XColor dummycolour;
    Cursor cursor;

    cursormask = XCreatePixmap(display, root, 1, 1, 1/*depth*/);
    xgc.function = GXclear;
    gc =  XCreateGC(display, cursormask, GCFunction, &xgc);
    XFillRectangle(display, cursormask, gc, 0, 0, 1, 1);
    dummycolour.pixel = 0;
    dummycolour.red = 0;
    dummycolour.flags = 04;
    cursor = XCreatePixmapCursor(display, cursormask, cursormask,
	      &dummycolour,&dummycolour, 0,0);
    XFreePixmap(display,cursormask);
    XFreeGC(display,gc);
    return cursor;
}

main(argc,argv)char **argv;{
    Display *display;
    int screen,oldx = -99,oldy = -99,numscreens;
    int doroot = 0, jitter = 0, idletime = 5, usegrabmethod = 0, waitagain = 0,
	dovisible = 1, doevents = 1, onescreen = 0;
    Cursor *cursor;
    Window *realroot;
    Window root;
    char *displayname = 0;
    
    progname = *argv;
    argc--;
    while(argv++,argc-->0){
	if(strcmp(*argv,"-idle")==0){
	    argc--,argv++;
	    if(argc<0)usage();
	    idletime = atoi(*argv);
	}else if(strcmp(*argv,"-keystroke")==0){
	    idletime = -1;
	}else if(strcmp(*argv,"-jitter")==0){
	    argc--,argv++;
	    if(argc<0)usage();
	    jitter = atoi(*argv);
	}else if(strcmp(*argv,"-noevents")==0){
	    doevents = 0;
	}else if(strcmp(*argv,"-root")==0){
	    doroot = 1;
	}else if(strcmp(*argv,"-grab")==0){
	    usegrabmethod = 1;
	}else if(strcmp(*argv,"-reset")==0){
	    waitagain = 1;
	}else if(strcmp(*argv,"-onescreen")==0){
	    onescreen = 1;
	}else if(strcmp(*argv,"-visible")==0){
	    dovisible = 0;
	}else if(strcmp(*argv,"-not")==0){
	    /* take rest of srg list */
	    names = ++argv;
	    if(*names==0)names = 0;	/* no args follow */
	    argc = 0;
	}else if(strcmp(*argv,"-display")==0 || strcmp(*argv,"-d")==0){
	    argc--,argv++;
	    if(argc<0)usage();
	    displayname = *argv;
	}else usage();
    }
    display = XOpenDisplay(displayname);
    if(display==0)pexit("could not open display");
    numscreens = ScreenCount(display);
    cursor = (Cursor*) malloc(numscreens*sizeof(Cursor));
    realroot = (Window*) malloc(numscreens*sizeof(Window));

    /* each screen needs its own empty cursor.
     * note each real root id so can find which screen we are on
     */
    for(screen = 0;screen<numscreens;screen++)
	if(onescreen && screen!=DefaultScreen(display)){
	    realroot[screen] = -1;
	    cursor[screen] = -1;
	}else{
	    realroot[screen] = XRootWindow(display,screen);
	    cursor[screen] = createnullcursor(display,realroot[screen]);
	    if(idletime<0)
		XSelectInput(display,realroot[screen],KeyReleaseMask);
	}
    screen = DefaultScreen(display);
    root = VirtualRootWindow(display,screen);

    if(!usegrabmethod)
	defaulthandler = XSetErrorHandler(errorhandler);
    /*
     * create a small unmapped window on a screen just so xdm can use
     * it as a handle on which to killclient() us.
     */
    XCreateWindow(display, realroot[screen], 0,0,1,1, 0, CopyFromParent,
		 InputOutput, CopyFromParent, 0, (XSetWindowAttributes*)0);

    while(1){
	Window dummywin,windowin,newroot;
	int rootx,rooty,winx,winy;
	unsigned int modifs;
	Window lastwindowavoided = None;
	
	/*
	 * wait for pointer to not move and no buttons down
	 * or if triggered by keystroke check no buttons down
	 */
	while(1){
	    if(idletime<0){		/* wait for keystroke trigger */
		XEvent event;
		do{
		    XNextEvent(display,&event);
		}while(event.type != KeyRelease ||
		       (event.xkey.state & ANYBUTTON));
		oldx = event.xkey.x_root;
		oldy = event.xkey.y_root;
	    }
	    if(!XQueryPointer(display, root, &newroot, &windowin,
			 &rootx, &rooty, &winx, &winy, &modifs)){
		/* window manager with virtual root may have restarted
		 * or we have changed screens */
		if(!onescreen){
		    for(screen = 0;screen<numscreens;screen++)
			if(newroot==realroot[screen])break;
		    if(screen>=numscreens)
			pexit("not on a known screen");
		}
		root = VirtualRootWindow(display,screen);
	    }else if((!doroot && windowin==None) || (modifs & ANYBUTTON) ||
		     !(ALMOSTEQUAL(rootx,oldx) && ALMOSTEQUAL(rooty,oldy))){
		oldx = rootx, oldy = rooty;
	    }else if(windowin==None){
		windowin = root;
		break;
	    }else if(windowin!=lastwindowavoided){
		/* descend tree of windows under cursor to bottommost */
		Window childin;
		int toavoid = xFalse;
		lastwindowavoided = childin = windowin;
		do{
		    windowin = childin;
		    if(nameinlist (display, windowin)){
			toavoid = xTrue;
			break;
		    }
		}while(XQueryPointer(display, windowin, &dummywin,
		     &childin, &rootx, &rooty, &winx, &winy, &modifs)
		       && childin!=None);
		if(!toavoid){
		    lastwindowavoided = None;
		    break;
		}
	    }
	    if(idletime>=0)
		sleep(idletime);
	}
	/* wait again next time */
	if(waitagain)
	    oldx = -1-jitter;
	if(usegrabmethod){
	    if(XGrabPointer(display, root, 0,
		    PointerMotionMask|ButtonPressMask|ButtonReleaseMask,
		    GrabModeAsync, GrabModeAsync, None, cursor[screen],
		    CurrentTime)==GrabSuccess){
		/* wait for a button event or large cursor motion */
		XEvent event;
		do{
		    XNextEvent(display,&event);
		}while(event.type==KeyRelease ||
		       (event.type==MotionNotify &&
			ALMOSTEQUAL(rootx,event.xmotion.x) &&
			ALMOSTEQUAL(rooty,event.xmotion.y)));
		XUngrabPointer(display, CurrentTime);
	    }
	}else{
	    XSetWindowAttributes attributes;
	    XEvent event;
	    Window cursorwindow;
	    
	    /* create small input-only window under cursor
	     * as a sub window of the window currently under the cursor
	     */
	    attributes.event_mask = LeaveWindowMask |
			EnterWindowMask |
			StructureNotifyMask |
			FocusChangeMask;
	    if(dovisible)
		attributes.event_mask |= VisibilityChangeMask;
	    attributes.override_redirect = True;
	    attributes.cursor = cursor[screen];
	    cursorwindow = XCreateWindow
		(display, windowin,
		 winx-jitter, winy-jitter,
		 jitter*2+1, jitter*2+1, 0, CopyFromParent,
		 InputOnly, CopyFromParent, 
		 CWOverrideRedirect | CWEventMask | CWCursor,
		 &attributes);
	    /* discard old events for previously created windows */
	    XSync(display,True);
	    XMapWindow(display,cursorwindow);
	    /*
	     * Dont wait for expose/map cos override and inputonly(?).
	     * Check that created window captured the pointer by looking
	     * for inevitable EnterNotify event that must follow MapNotify.
	     * [Bug fix thanks to Charles Hannum <mycroft@ai.mit.edu>]
	     */
	    XSync(display,False);
	    if(!XCheckTypedWindowEvent(display, cursorwindow, EnterNotify,
				      &event))
		oldx = -1-jitter;	/* slow down retry */
	    else{
		if(doevents){
		    /*
		     * send a pseudo EnterNotify event to the parent window
		     * to try to convince application that we didnt really leave it
		     */
		    event.xcrossing.type = EnterNotify;
		    event.xcrossing.display = display;
		    event.xcrossing.window = windowin;
		    event.xcrossing.root = root;
		    event.xcrossing.subwindow = None;
		    event.xcrossing.time = CurrentTime;
		    event.xcrossing.x = winx;
		    event.xcrossing.y = winy;
		    event.xcrossing.x_root = rootx;
		    event.xcrossing.y_root = rooty;
		    event.xcrossing.mode = NotifyNormal;
		    event.xcrossing.same_screen = True;
		    event.xcrossing.focus = True;
		    event.xcrossing.state = modifs;
		    (void)XSendEvent(display,windowin,
				     True/*propagate*/,EnterWindowMask,&event);
		}
		/* wait till pointer leaves window */
		do{
		    XNextEvent(display,&event);
		}while(event.type!=LeaveNotify &&
		       event.type!=FocusOut &&
		       event.type!=UnmapNotify &&
		       event.type!=ConfigureNotify &&
		       event.type!=CirculateNotify &&
		       event.type!=ReparentNotify &&
		       event.type!=DestroyNotify &&
		       (event.type!=VisibilityNotify ||
			event.xvisibility.state==VisibilityUnobscured)
		       );
		/* check if a second unclutter is running cos they thrash */
		if(event.type==LeaveNotify &&
		   event.xcrossing.window==cursorwindow &&
		   event.xcrossing.detail==NotifyInferior)
		    pexit("someone created a sub-window to my sub-window! giving up");
	    }
	    XDestroyWindow(display, cursorwindow);
	}
    }
}
