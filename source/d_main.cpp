// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// Copyright (C) 2013 James Haley et al.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/
//
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//  DOOM main program (D_DoomMain) and game loop, plus functions to
//  determine game mode (shareware, registered), parse command line
//  parameters, configure game parameters (turbo), and call the startup
//  functions.
//
//-----------------------------------------------------------------------------

// haleyjd 10/28/04: Win32-specific repair for D_DoomExeDir
// haleyjd 08/20/07: POSIX opendir needed for autoload functionality
#ifdef _MSC_VER
#include "Win32/i_fnames.h"
#endif

#include "z_zone.h"

#include "acs_intr.h"
#include "aeon_system.h"
#include "am_map.h"
#include "c_io.h"
#include "c_net.h"
#include "c_runcmd.h"
#include "d_deh.h"      // Ty 04/08/98 - Externalizations
#include "d_dehtbl.h"
#include "d_event.h"
#include "d_files.h"
#include "d_gi.h"
#include "d_io.h"
#include "d_iwad.h"
#include "d_net.h"
#include "doomstat.h"
#include "dstrings.h"
#include "e_edf.h"
#include "e_fonts.h"
#include "e_player.h"
#include "f_finale.h"
#include "f_wipe.h"
#include "g_bind.h"
#include "g_demolog.h"
#include "g_dmflag.h"
#include "g_game.h"
#include "g_gfs.h"
#include "hal/i_timer.h"
#include "hu_stuff.h"
#include "i_sound.h"
#include "i_system.h"
#include "i_video.h"
#include "in_lude.h"
#include "m_argv.h"
#include "m_compare.h"
#include "m_misc.h"
#include "m_syscfg.h"
#include "m_qstr.h"
#include "m_utils.h"
#include "mn_engin.h"
#include "p_chase.h"
#include "p_setup.h"
#include "r_draw.h"
#include "r_main.h"
#include "r_patch.h"
#include "s_sound.h"
#include "st_stuff.h"
#include "v_block.h"
#include "v_font.h"
#include "v_misc.h"
#include "v_patchfmt.h"
#include "v_video.h"
#include "version.h"
#include "w_wad.h"
#include "xl_scripts.h"

// killough 10/98: preloaded files
#define MAXLOADFILES 2
char *wad_files[MAXLOADFILES], *deh_files[MAXLOADFILES];
// haleyjd: allow two auto-loaded console scripts
char *csc_files[MAXLOADFILES];

int textmode_startup = 0;  // sf: textmode_startup for old-fashioned people
int use_startmap = -1;     // default to -1 for asking in menu
bool devparm;              // started game with -devparm

// jff 1/24/98 add new versions of these variables to remember command line
bool clnomonsters;   // checkparm of -nomonsters
bool clrespawnparm;  // checkparm of -respawn
bool clfastparm;     // checkparm of -fast
// jff 1/24/98 end definition of command line version of play mode switches

int r_blockmap = false;       // -blockmap command line

bool nomonsters;     // working -nomonsters
bool respawnparm;    // working -respawn
bool fastparm;       // working -fast

bool singletics = false; // debug flag to cancel adaptiveness

//jff 1/22/98 parms for disabling music and sound
bool nosfxparm;
bool nomusicparm;

//jff 4/18/98
extern bool inhelpscreens;

skill_t startskill;
int     startepisode;
int     startmap;
char    *startlevel;
bool autostart;

bool advancedemo;

extern bool timingdemo, singledemo, demoplayback, fastdemo; // killough

char    *basedefault;             // default file
char    *basesavegame;            // killough 2/16/98: savegame directory

char    *basepath;                // haleyjd 11/23/06: path of "base" directory
char    *basegamepath;            // haleyjd 11/23/06: path of base/game directory

char    *userpath;                // haleyjd 02/05/12: path of "user" directory
char    *usergamepath;            // haleyjd 02/05/12: path of user/game directory

void D_CheckNetGame(void);
void D_ProcessEvents(void);
void G_BuildTiccmd(ticcmd_t* cmd);
void D_DoAdvanceDemo(void);

void usermsg(const char *s, ...)
{
   static char msg[1024];
   va_list v;
   
   va_start(v,s);
   pvsnprintf(msg, sizeof(msg), s, v); // print message in buffer
   va_end(v);
   
   if(in_textmode)
   {
      puts(msg);
   }
   else
   {
      C_Puts(msg);
      C_Update();
   }
}

//sf:
void startupmsg(const char *func, const char *desc)
{
   // add colours in console mode
   usermsg(in_textmode ? "%s: %s" : FC_HI "%s: " FC_NORMAL "%s",
           func, desc);
}

//=============================================================================
//
// EVENT HANDLING
//
// Events are asynchronous inputs generally generated by the game user.
// Events can be discarded if no responder claims them
//

#define MAXEVENTS 64

static event_t events[MAXEVENTS];
static int eventhead, eventtail;

//
// D_PostEvent
// Called by the I/O functions when input is detected
//
void D_PostEvent(event_t *ev)
{
   events[eventhead++] = *ev;
   eventhead &= MAXEVENTS-1;
}

//
// D_ProcessEvents
// Send all the events of the given timestamp down the responder chain
//
void D_ProcessEvents()
{
   // IF STORE DEMO, DO NOT ACCEPT INPUT
   // sf: I don't think SMMU is going to be played in any store any
   //     time soon =)
   // if (gamemode != commercial || W_CheckNumForName("map01") >= 0)

   for(; eventtail != eventhead; eventtail = (eventtail+1) & (MAXEVENTS-1))
   {
      event_t *evt = events + eventtail;

      if(!MN_Responder(evt))
         if(!C_Responder(evt))
            G_Responder(evt);
   }
}

//=============================================================================
//
//  DEMO LOOP
//

static int demosequence;         // killough 5/2/98: made static
static int pagetic;
static const char *pagename;

//
// D_AdvanceDemo
// Called after each demo or intro demosequence finishes
//
void D_AdvanceDemo(void)
{
   advancedemo = true;
}

//
// D_PageTicker
// Handles timing for warped projection
//
void D_PageTicker(void)
{
   // killough 12/98: don't advance internal demos if a single one is
   // being played. The only time this matters is when using -loadgame with
   // -fastdemo, -playdemo, or -timedemo, and a consistency error occurs.

   if (/*!singledemo &&*/ --pagetic < 0)
      D_AdvanceDemo();
}

//
// D_PageDrawer
//
// killough 11/98: add credits screen
//
static void D_PageDrawer()
{
   int l;

   if(pagename && (l = W_CheckNumForName(pagename)) != -1)
   {
      // haleyjd 08/15/02: handle Heretic pages
      V_DrawFSBackground(&subscreen43, l);

      if(GameModeInfo->flags & GIF_HASADVISORY && demosequence == 1)
      {
         V_DrawPatch(4, 160, &subscreen43, 
                     PatchLoader::CacheName(wGlobalDir, "ADVISOR", PU_CACHE));
      }
   }
   else
      MN_DrawCredits();
}

// killough 11/98: functions to perform demo sequences

static void D_SetPageName(const char *name)
{
   pagename = name;
}

static void D_DrawTitle(const char *name)
{
   S_StartMusic(GameModeInfo->titleMusNum);
   pagetic = GameModeInfo->titleTics;

   if(GameModeInfo->missionInfo->flags & MI_CONBACKTITLE)
      D_SetPageName(GameModeInfo->consoleBack);
   else
      D_SetPageName(name);
}

static void D_DrawTitleA(const char *name)
{
   pagetic = GameModeInfo->advisorTics;
   D_SetPageName(name);
}

// killough 11/98: tabulate demo sequences

const demostate_t demostates_doom[] =
{
   { D_DrawTitle,       "TITLEPIC" }, // shareware, registered
   { G_DeferedPlayDemo, "DEMO1"    },
   { D_SetPageName,     NULL       },
   { G_DeferedPlayDemo, "DEMO2"    },
   { D_SetPageName,     "HELP2"    },
   { G_DeferedPlayDemo, "DEMO3"    },
   { NULL }
};

const demostate_t demostates_doom2[] =
{
   { D_DrawTitle,       "TITLEPIC" }, // commercial
   { G_DeferedPlayDemo, "DEMO1"    },
   { D_SetPageName,     NULL       },
   { G_DeferedPlayDemo, "DEMO2"    },
   { D_SetPageName,     "CREDIT"   },
   { G_DeferedPlayDemo, "DEMO3"    },
   { NULL }
};

const demostate_t demostates_udoom[] =
{
   { D_DrawTitle,       "TITLEPIC" }, // retail
   { G_DeferedPlayDemo, "DEMO1"    },
   { D_SetPageName,     NULL       },
   { G_DeferedPlayDemo, "DEMO2"    },
   { D_SetPageName,     "CREDIT"   },
   { G_DeferedPlayDemo, "DEMO3"    },
   { G_DeferedPlayDemo, "DEMO4"    },
   { NULL }
};

const demostate_t demostates_hsw[] =
{
   { D_DrawTitle,       "TITLE" }, // heretic shareware
   { D_DrawTitleA,      "TITLE" },
   { G_DeferedPlayDemo, "DEMO1" },
   { D_SetPageName,     "ORDER" },
   { G_DeferedPlayDemo, "DEMO2" },
   { D_SetPageName,     NULL    },
   { G_DeferedPlayDemo, "DEMO3" },
   { NULL }
};

const demostate_t demostates_hreg[] =
{
   { D_DrawTitle,       "TITLE"  }, // heretic registered/sosr
   { D_DrawTitleA,      "TITLE"  },
   { G_DeferedPlayDemo, "DEMO1"  },
   { D_SetPageName,     "CREDIT" },
   { G_DeferedPlayDemo, "DEMO2"  },
   { D_SetPageName,     NULL     },
   { G_DeferedPlayDemo, "DEMO3"  },
   { NULL }
};

const demostate_t demostates_unknown[] =
{
   { D_SetPageName, NULL }, // indetermined - haleyjd 04/01/08
   { NULL }
};

//
// D_DoAdvanceDemo
//
// This cycles through the demo sequences.
// killough 11/98: made table-driven
//
void D_DoAdvanceDemo()
{
   const demostate_t *demostates = GameModeInfo->demoStates;
   const demostate_t *state;

   players[consoleplayer].playerstate = PST_LIVE;  // not reborn
   advancedemo = usergame = false;
   paused = 0;
   gameaction = ga_nothing;

   pagetic = GameModeInfo->pageTics;
   gamestate = GS_DEMOSCREEN;

   // haleyjd 10/08/06: changed to allow DEH/BEX replacement of
   // demo state resource names
   state = &(demostates[++demosequence]);

   if(!state->func) // time to wrap?
   {
      demosequence = 0;
      state = &(demostates[0]);
   }

   state->func(DEH_String(state->name));

   C_InstaPopup();       // make console go away
}

//
// D_StartTitle
//
void D_StartTitle()
{
   gameaction = ga_nothing;
   demosequence = -1;
   D_AdvanceDemo();
}

//=============================================================================
//
// Display
//
// All drawing starts here.
//

// wipegamestate can be set to -1 to force a wipe on the next draw

gamestate_t oldgamestate  = GS_NOSTATE;  // sf: globaled
gamestate_t wipegamestate = GS_DEMOSCREEN;
camera_t    *camera;
extern bool setsizeneeded;
int         wipewait;        // haleyjd 10/09/07

bool        d_drawfps;       // haleyjd 09/07/10: show drawn fps

//
// D_showFPS
//
static void D_showDrawnFPS()
{
   static unsigned int lastms, accms, frames;
   unsigned int curms;
   static int lastfps;
   vfont_t *font;
   char msg[64];
   
   accms += (curms = i_haltimer.GetTicks()) - lastms;
   lastms = curms;
   ++frames;

   if(accms >= 1000)
   {
      lastfps = frames * 1000 / accms;
      frames = 0;
      accms -= 1000;
   }

   font = E_FontForName("ee_smallfont");
   psnprintf(msg, 64, "DFPS: %d", lastfps);
   V_FontWriteText(font, msg, 5, 20);
}

#ifdef INSTRUMENTED
struct cachelevelprint_t
{
   int cachelevel;
   const char *name;
};
static cachelevelprint_t cachelevels[] =
{
   { PU_STATIC,   "static: " },
   { PU_VALLOC,   " video: " },
   { PU_RENDERER, "render: " },
   { PU_LEVEL,    " level: " },
   { PU_CACHE,    " cache: " },
   { PU_MAX,      " total: " }
};
#define NUMCACHELEVELSTOPRINT earrlen(cachelevels)

static void D_showMemStats()
{
   vfont_t *font;
   size_t total_memory = 0;
   double s;
   char buffer[1024];
   size_t i;

   for(i = 0; i < earrlen(cachelevels) - 1; i++)
      total_memory += memorybytag[cachelevels[i].cachelevel];
   s = 100.0 / total_memory;

   font = E_FontForName("ee_consolefont");   
   // draw the labels
   for(i = 0; i < earrlen(cachelevels); i++)
   {
      int tag = cachelevels[i].cachelevel;
      if(tag != PU_MAX)
      {
         psnprintf(buffer, sizeof(buffer), "%s%9lu %7.02f%%", 
                   cachelevels[i].name,
                   memorybytag[tag], memorybytag[tag] * s);
         V_FontWriteText(font, buffer, 1, static_cast<int>(1 + i*font->cy));
      }
      else
      {
         psnprintf(buffer, sizeof(buffer), "%s%9lu %7.02f%%",
                   cachelevels[i].name, total_memory, 100.0f);
         V_FontWriteText(font, buffer, 1, static_cast<int>(1 + i*font->cy));
      }
   }
}
#endif

//
// D_DrawPillars
//
// Will draw pillars for pillarboxing the 4:3 subscreen.
//
void D_DrawPillars()
{
   int wingwidth;
   
   if(vbscreen.getVirtualAspectRatio() <= 4 * FRACUNIT / 3)
      return;
   
   wingwidth = (vbscreen.width - (vbscreen.height * 4 / 3)) / 2;
   if(wingwidth <= 0)
         return;

   V_ColorBlock(&vbscreen, GameModeInfo->blackIndex, 0, 0, wingwidth, vbscreen.height);
   V_ColorBlock(&vbscreen, GameModeInfo->blackIndex, vbscreen.width - wingwidth,
                0, wingwidth, vbscreen.height);
}

//
// D_DrawWings
//
// haleyjd: Draw pillarboxing during non-play gamestates, or the wings of the 
// status bar while it is visible. This is necessary when drawing patches at
// 4:3 aspect ratio over widescreen video modes.
//
void D_DrawWings()
{
   int wingwidth;

   if(vbscreen.getVirtualAspectRatio() <= 4 * FRACUNIT / 3)
      return;

   wingwidth = (vbscreen.width - (vbscreen.height * 4 / 3)) / 2;

   // safety check
   if(wingwidth <= 0)
      return;

   if(gamestate == GS_LEVEL && !MN_CheckFullScreen())
   {
      if(scaledwindow.height != SCREENHEIGHT || automapactive)
      {
         unsigned int bottom   = SCREENHEIGHT - 1;
         unsigned int statbarh = static_cast<unsigned int>(GameModeInfo->StatusBar->height);
         
         int ycoord      = vbscreen.y1lookup[bottom - statbarh];
         int blockheight = vbscreen.y2lookup[bottom] - ycoord + 1;

         R_VideoEraseScaled(0, ycoord, wingwidth, blockheight);
         R_VideoEraseScaled(vbscreen.width - wingwidth, ycoord, wingwidth, blockheight);
      }
   }
   else
   {
      V_ColorBlock(&vbscreen, GameModeInfo->blackIndex, 0, 0, wingwidth, vbscreen.height);
      V_ColorBlock(&vbscreen, GameModeInfo->blackIndex, vbscreen.width - wingwidth,
                   0, wingwidth, vbscreen.height);
   }
}

//
// D_Display
//  draw current display, possibly wiping it from the previous
//
static void D_Display()
{
   if(nodrawers)                // for comparative timing / profiling
      return;

   i_haltimer.StartDisplay();

   if(setsizeneeded)            // change the view size if needed
   {
      R_ExecuteSetViewSize();
      R_FillBackScreen(scaledwindow);       // redraw backscreen
   }

   // save the current screen if about to wipe
   // no melting consoles
   if(gamestate != wipegamestate &&
      !(wipegamestate == GS_CONSOLE && gamestate != GS_LEVEL))
      Wipe_StartScreen();

   // haleyjd 07/15/2012: draw "wings" (or pillars) to fill in missing bits
   // created by drawing patches 4:3 in higher aspect ratios.
   D_DrawWings();

   // haleyjd: optimization for fullscreen menu drawing -- no
   // need to do all this if the menus are going to cover it up :)
   if(!MN_CheckFullScreen())
   {
      switch(gamestate)                // do buffered drawing
      {
      case GS_LEVEL:
         // see if the border needs to be initially drawn
         if(oldgamestate != GS_LEVEL)
            R_FillBackScreen(scaledwindow); // draw the pattern into the back screen
         
         if(automapactive)
         {
            AM_Drawer();
         }
         else
         {
            R_DrawViewBorder();    // redraw border
            R_RenderPlayerView(&players[displayplayer], camera);
         }
         
         ST_Drawer(scaledwindow.height == SCREENHEIGHT);  // killough 11/98
         HU_Drawer();
         break;
      case GS_INTERMISSION:
         IN_Drawer();
         break;
      case GS_FINALE:
         F_Drawer();
         break;
      case GS_DEMOSCREEN:
         D_PageDrawer();
         break;
      case GS_CONSOLE:
         break;
      default:
         break;
      }
         
      // clean up border stuff
      if(gamestate != oldgamestate && gamestate != GS_LEVEL)
         I_SetPalette((byte *)(wGlobalDir.cacheLumpName("PLAYPAL", PU_CACHE)));
      
      oldgamestate = wipegamestate = gamestate;
         
      // draw pause pic
      if(paused && !walkcam_active) // sf: not if walkcam active for
      {                             // frads taking screenshots
         const char *lumpname = GameModeInfo->pausePatch; 
         
         // haleyjd 03/12/03: changed to work
         // in heretic, and with user pause patches
         patch_t *patch = PatchLoader::CacheName(wGlobalDir, lumpname, PU_CACHE);
         int width = patch->width;
         int x = (SCREENWIDTH - width) / 2 + patch->leftoffset;
         // SoM 2-4-04: ANYRES
         int y = 4 + (automapactive ? 0 : scaledwindow.y);
         
         V_DrawPatch(x, y, &subscreen43, patch);
      }

      if(inwipe)
      {
         bool wait = (wipewait == 1 || (wipewait == 2 && demoplayback));
         
         // about to start wiping; if wipewait is enabled, save everything 
         // that was just drawn
         if(wait)
         {
            Wipe_SaveEndScreen();
            
            do
            {
               int starttime = i_haltimer.GetTime();
               int tics = 0;
               
               Wipe_Drawer();
               
               do
               {
                  tics = i_haltimer.GetTime() - starttime;

                  // haleyjd 06/16/09: sleep to avoid hogging 100% CPU
                  i_haltimer.Sleep(1);
               }
               while(!tics);
               
               Wipe_Ticker();
               
               C_Drawer();
               MN_Drawer();
               NetUpdate();
               if(v_ticker)
                  V_FPSDrawer();
               I_FinishUpdate();
               
               if(inwipe)
                  Wipe_BlitEndScreen();
            }
            while(inwipe);
         }
         else
            Wipe_Drawer();
      }

      C_Drawer();

   } // if(!MN_CheckFullScreen())

   // menus go directly to the screen
   MN_Drawer();         // menu is drawn even on top of everything
   NetUpdate();         // send out any new accumulation
   
   //sf : now system independent
   if(v_ticker)
      V_FPSDrawer();

   if(d_drawfps)
      D_showDrawnFPS();

#ifdef INSTRUMENTED
   if(printstats)
      D_showMemStats();
#endif
   
   I_FinishUpdate();              // page flip or blit buffer

   i_haltimer.EndDisplay();
}

//=============================================================================
//
// EXE Path / Name Functions
//

//
// D_DoomExeDir
//
// Return the path where the executable lies -- Lee Killough
//
// FIXME: call an I_DoomExeDir function to get rid of #ifndef
//
char *D_DoomExeDir()
{
   static char *base = NULL;

   if(!base) // cache multiple requests
   {
#ifndef _MSC_VER

      size_t len = strlen(myargv[0]) + 1;

      base = emalloc(char *, len);

      // haleyjd 03/09/03: generalized
      M_GetFilePath(myargv[0], base, len);
#else
      // haleyjd 10/28/04: the above is not sufficient for all versions
      // of Windows. There is an API function which takes care of this,
      // however.  See i_fnames.c in the Win32 subdirectory.
      base = emalloc(char *, PATH_MAX + 1);

      WIN_GetExeDir(base, PATH_MAX + 1);
#endif
   }

   return base;
}

//=============================================================================

static const char *game_name; // description of iwad

//
// D_SetGameName
//
// Sets the game_name variable for displaying what version of the game is being
// played at startup. "iwad" may be NULL. GameModeInfo must be initialized prior
// to calling this.
//
void D_SetGameName(const char *iwad)
{
   // get appropriate name for the gamemode/mission
   game_name = GameModeInfo->versionName;

   // special hacks for localized DOOM II variants:
   if(iwad && GameModeInfo->id == commercial)
   {
      // joel 10/16/98 Final DOOM fix
      if(GameModeInfo->missionInfo->id == doom2)
      {
         int i = static_cast<int>(strlen(iwad));
         if(i >= 10 && !strncasecmp(iwad+i-10, "doom2f.wad", 10))
         {
            language = french;
            game_name = "DOOM II version, French language";
         }
         else if(!haswolflevels)
            game_name = "DOOM II version, German edition, no Wolf levels";
      }
      // joel 10/16/98 end Final DOOM fix
   }

   // haleyjd 03/07/10: Special FreeDoom overrides :)
   if(freedoom && GameModeInfo->freeVerName)
      game_name = GameModeInfo->freeVerName;

   // haleyjd 11/03/12: BFG Edition overrides
   if(bfgedition && GameModeInfo->bfgEditionName)
      game_name = GameModeInfo->bfgEditionName;

   puts(game_name);
}

//
// D_InitPaths
//
// Initializes important file paths, including the game path, config
// path, and save path.
//
void D_InitPaths()
{
   int i;

   // haleyjd 11/23/06: set game path if -game wasn't used
   if(!gamepathset)
      D_SetGamePath();

   // haleyjd 11/23/06: set basedefault here, and use basegamepath.
   // get config file from same directory as executable
   // killough 10/98
   if(GameModeInfo->type == Game_DOOM && use_doom_config)
   {
      qstring tmp(userpath);
      tmp.pathConcatenate("/doom/eternity.cfg");
      basedefault = tmp.duplicate(PU_STATIC);
   }
   else
   {
      qstring tmp(usergamepath);
      tmp.pathConcatenate("/eternity.cfg");
      basedefault = tmp.duplicate(PU_STATIC);
   }

   // haleyjd 11/23/06: set basesavegame here, and use usergamepath
   // set save path to -save parm or current dir
   basesavegame = estrdup(usergamepath);

   if((i = M_CheckParm("-save")) && i < myargc-1) //jff 3/24/98 if -save present
   {
      struct stat sbuf; //jff 3/24/98 used to test save path for existence

      if(!stat(myargv[i+1],&sbuf) && S_ISDIR(sbuf.st_mode)) // and is a dir
      {
         if(basesavegame)
            efree(basesavegame);
         basesavegame = estrdup(myargv[i+1]); //jff 3/24/98 use that for savegame
      }
      else
         puts("Error: -save path does not exist, using game path");  // killough 8/8/98
   }
}

//=============================================================================
//
// Response File Parsing
//

// MAXARGVS: a reasonable(?) limit on response file arguments

#define MAXARGVS 100

//
// FindResponseFile
//
// Find a Response File, identified by an "@" argument.
//
// haleyjd 04/17/03: copied, slightly modified prboom's code to
// allow quoted LFNs in response files.
//
static void FindResponseFile()
{
   int i;

   for(i = 1; i < myargc; ++i)
   {
      if(myargv[i][0] == '@')
      {
         int size, index, indexinfile;
         byte *f;
         char *file = NULL, *firstargv;
         char **moreargs = ecalloc(char **, myargc, sizeof(char *));
         char **newargv;
         qstring fname;
         
         fname = &myargv[i][1];
         fname.addDefaultExtension(".rsp");

         // read the response file into memory
         if((size = M_ReadFile(fname.constPtr(), &f)) < 0)
            I_Error("No such response file: %s\n", fname.constPtr());

         file = (char *)f;

         printf("Found response file %s\n", fname.constPtr());

         // proff 04/05/2000: Added check for empty rsp file
         if(!size)
         {
            int k;
            printf("\nResponse file empty!\n");

            newargv = ecalloc(char **, sizeof(char *), MAXARGVS);
            newargv[0] = myargv[0];
            for(k = 1, index = 1; k < myargc; k++)
            {
               if(i != k)
                  newargv[index++] = myargv[k];
            }
            myargc = index; myargv = newargv;
            return;
         }

         // keep all cmdline args following @responsefile arg
         memcpy((void *)moreargs, &myargv[i+1],
                (index = myargc - i - 1) * sizeof(myargv[0]));

         firstargv = myargv[0];
         newargv = ecalloc(char **, sizeof(char *), MAXARGVS);
         newargv[0] = firstargv;

         {
            char *infile = file;
            indexinfile = 0;
            indexinfile++;  // skip past argv[0] (keep it)
            do
            {
               while(size > 0 && ectype::isSpace(*infile))
               {
                  infile++;
                  size--;
               }

               if(size > 0)
               {
                  char *s = emalloc(char *, size+1);
                  char *p = s;
                  int quoted = 0;

                  while (size > 0)
                  {
                     // Whitespace terminates the token unless quoted
                     if(!quoted && ectype::isSpace(*infile))
                        break;
                     if(*infile == '\"')
                     {
                        // Quotes are removed but remembered
                        infile++; size--; quoted ^= 1;
                     }
                     else
                     {
                        *p++ = *infile++; size--;
                     }
                  }
                  if(quoted)
                     I_Error("Runaway quoted string in response file\n");

                  // Terminate string, realloc and add to argv
                  *p = 0;
                  newargv[indexinfile++] = erealloc(char *, s, strlen(s)+1);
               }
            } 
            while(size > 0);
         }
         efree(file);

         memcpy((void *)&newargv[indexinfile],moreargs,index*sizeof(moreargs[0]));
         efree((void *)moreargs);

         myargc = indexinfile+index; myargv = newargv;

         // display args
         printf("%d command-line args:\n", myargc);

         for(index = 1; index < myargc; index++)
            printf("%s\n", myargv[index]);

         break;
      }
   }
}

//=============================================================================
//
// Misc. File Stuff
// * DEHACKED Loading
// * MBF-Style Autoloads
//

// killough 10/98: moved code to separate function

static void D_ProcessDehCommandLine(void)
{
   // ty 03/09/98 do dehacked stuff
   // Note: do this before any other since it is expected by
   // the deh patch author that this is actually part of the EXE itself
   // Using -deh in BOOM, others use -dehacked.
   // Ty 03/18/98 also allow .bex extension.  .bex overrides if both exist.
   // killough 11/98: also allow -bex

   int p = M_CheckParm("-deh");
   if(p || (p = M_CheckParm("-bex")))
   {
      // the parms after p are deh/bex file names,
      // until end of parms or another - preceded parm
      // Ty 04/11/98 - Allow multiple -deh files in a row
      // killough 11/98: allow multiple -deh parameters

      bool deh = true;
      while(++p < myargc)
      {
         if(*myargv[p] == '-')
            deh = !strcasecmp(myargv[p],"-deh") || !strcasecmp(myargv[p],"-bex");
         else
         {
            if(deh)
            {
               qstring file; // killough
                  
               file = myargv[p];
               file.addDefaultExtension(".bex");
               if(access(file.constPtr(), F_OK))  // nope
               {
                  file = myargv[p];
                  file.addDefaultExtension(".deh");
                  if(access(file.constPtr(), F_OK))  // still nope
                     I_Error("Cannot find .deh or .bex file named '%s'\n", myargv[p]);
               }
               // during the beta we have debug output to dehout.txt
               // (apparently, this was never removed after Boom beta-killough)
               //ProcessDehFile(file, D_dehout(), 0);  // killough 10/98
               // haleyjd: queue the file, process it later
               D_QueueDEH(file.constPtr(), 0);
            }
         }
      }
   }
   // ty 03/09/98 end of do dehacked stuff
}

// killough 10/98: support preloaded wads

static void D_ProcessWadPreincludes()
{
   // haleyjd 09/30/08: don't do in shareware
   if(!M_CheckParm("-noload") && !(GameModeInfo->flags & GIF_SHAREWARE))
   {
      for(char *s : wad_files)
         if(s)
         {
            while(ectype::isSpace(*s))
               s++;
            if(*s)
            {
               qstring file;

               file = s;
               file.addDefaultExtension(".wad");
               if(!access(file.constPtr(), R_OK))
                  D_AddFile(file.constPtr(), lumpinfo_t::ns_global, nullptr, 0, DAF_NONE);
               else
                  printf("\nWarning: could not open '%s'\n", file.constPtr());
            }
         }
   }
}

// killough 10/98: support preloaded deh/bex files

static void D_ProcessDehPreincludes(void)
{
   if(!M_CheckParm ("-noload"))
   {
      for(char *s : deh_files)
      {
         if(s)
         {
            while(ectype::isSpace(*s))
               s++;
            if(*s)
            {
               qstring file;

               file = s;
               file.addDefaultExtension(".bex");
               if(!access(file.constPtr(), R_OK))
                  D_QueueDEH(file.constPtr(), 0); // haleyjd: queue it
               else
               {
                  file = s;
                  file.addDefaultExtension(".deh");
                  if(!access(file.constPtr(), R_OK))
                     D_QueueDEH(file.constPtr(), 0); // haleyjd: queue it
                  else
                     printf("\nWarning: could not open '%s' .deh or .bex\n", s);
               }
            } // end if(*s)
         } // end if((s = deh_files[i]))
      } // end for
   } // end if
}

//
// D_AutoExecScripts
//
// haleyjd: auto-executed console scripts
//
static void D_AutoExecScripts()
{
   // haleyjd 05/31/06: run command-line scripts first
   C_RunCmdLineScripts();

   if(!M_CheckParm("-nocscload")) // separate param from above
   {
      for(char *s : csc_files)
      {
         if(s)
         {
            while(ectype::isSpace(*s))
               s++;
            if(*s)
            {
               qstring file;

               file = s;
               file.addDefaultExtension(".csc");
               if(!access(file.constPtr(), R_OK))
                  C_RunScriptFromFile(file.constPtr());
               else
                  usermsg("\nWarning: could not open console script %s\n", s);
            }
         }
      }
   }
}

// killough 10/98: support .deh from wads
//
// A lump named DEHACKED is treated as plaintext of a .deh file embedded in
// a wad (more portable than reading/writing info.c data directly in a wad).
//
// If there are multiple instances of "DEHACKED", we process each, in first
// to last order (we must reverse the order since they will be stored in
// last to first order in the chain). Passing NULL as first argument to
// ProcessDehFile() indicates that the data comes from the lump number
// indicated by the third argument, instead of from a file.

// haleyjd 10/20/03: restored to MBF semantics in order to support
// queueing of dehacked lumps without trouble because of reordering
// of the wad directory by W_InitMultipleFiles.

static void D_ProcessDehInWad(int i)
{
   if(i >= 0)
   {
      lumpinfo_t **lumpinfo = wGlobalDir.getLumpInfo();
      D_ProcessDehInWad(lumpinfo[i]->namehash.next);
      if(!strncasecmp(lumpinfo[i]->name, "DEHACKED", 8) &&
         lumpinfo[i]->li_namespace == lumpinfo_t::ns_global)
         D_QueueDEH(NULL, i); // haleyjd: queue it
   }
}

static void D_ProcessDehInWads()
{
   // haleyjd: start at the top of the hash chain
   lumpinfo_t *root = wGlobalDir.getLumpNameChain("DEHACKED");

   D_ProcessDehInWad(root->namehash.index);
}

//=============================================================================
//
// Primary Initialization Routines
//

//
// D_LoadSysConfig
//
// Load the system config. Prerequisites: base path must be determined.
//
static void D_LoadSysConfig(void)
{
   qstring filename;

   filename = userpath;
   filename.pathConcatenate("system.cfg");

   M_LoadSysConfig(filename.constPtr());
}

//
// D_SetGraphicsMode
//
// sf: this is really part of D_DoomMain but I made it into
// a seperate function
// this stuff needs to be kept together
//
static void D_SetGraphicsMode()
{
   // set graphics mode
   I_InitGraphics();

   // set up the console to display startup messages
   gamestate = GS_CONSOLE;
   Console.current_height = SCREENHEIGHT;
   Console.showprompt = false;

   C_Puts(game_name);    // display description of gamemode
   D_ListWads();         // list wads to the console
   C_Printf("\n");       // leave a gap
}

#ifdef GAMEBAR
// print title for every printed line
static char title[128];
#endif

extern int levelTimeLimit;
extern int levelFragLimit;

//
// D_StartupMessage
//
// A little reminder for Certain People (Ichaelmay Ardyhay)
//
static void D_StartupMessage()
{
   puts("The Eternity Engine\n"
        "Copyright 2017 James Haley, Stephen McGranahan, et al.\n"
        "http://www.doomworld.com/eternity\n"
        "\n"
        "This program is free software distributed under the terms of\n"
        "the GNU General Public License. See the file \"COPYING\" for\n"
        "full details. Commercial sale or distribution of this product\n"
        "without its license, source code, and copyright notices is an\n"
        "infringement of US and international copyright laws.\n");
}

//
// D_DoomInit
//
// Broke D_DoomMain into two functions in order to keep
// initialization stuff off the main line of execution.
//
static void D_DoomInit()
{
   int p, slot;
   int dmtype = 0;          // haleyjd 04/14/03
   bool haveGFS = false;    // haleyjd 03/10/03
   gfs_t *gfs = NULL;

   gamestate = GS_STARTUP; // haleyjd 01/01/10

   D_StartupMessage();

   startupmsg("Z_Init", "Init zone memory allocation daemon.");
   Z_Init();
   atexit(I_Quit);

   FindResponseFile(); // Append response file arguments to command-line

   // haleyjd 08/18/07: set base path and user path
   D_SetBasePath();
   D_SetUserPath();

   // haleyjd 08/19/07: check for -game parameter first
   D_CheckGamePathParam();

   // haleyjd 03/05/09: load system config as early as possible
   D_LoadSysConfig();

   // haleyjd 03/10/03: GFS support
   // haleyjd 11/22/03: support loose GFS on the command line too
   if((p = M_CheckParm("-gfs")) && p < myargc - 1)
   {
      qstring fn;
         
      // haleyjd 01/19/05: corrected use of AddDefaultExtension
      fn = myargv[p + 1];
      fn.addDefaultExtension(".gfs");
      if(access(fn.constPtr(), F_OK))
         I_Error("GFS file '%s' not found\n", fn.constPtr());

      printf("Parsing GFS file '%s'\n", fn.constPtr());

      gfs = G_LoadGFS(fn.constPtr());
      haveGFS = true;
   }
   else if((gfs = D_LooseGFS())) // look for a loose GFS for drag-and-drop support
   {
      haveGFS = true;
   }
   else if(gamepathset) // haleyjd 08/19/07: look for default.gfs in specified game path
   {
      qstring fn;
      
      fn = basegamepath;
      fn.pathConcatenate("default.gfs");
      if(!access(fn.constPtr(), R_OK))
      {
         gfs = G_LoadGFS(fn.constPtr());
         haveGFS = true;
      }
   }

   // haleyjd: init the dehacked queue (only necessary the first time)
   D_DEHQueueInit();

   // haleyjd 11/22/03: look for loose DEH files (drag and drop)
   D_LooseDehs();

   // killough 10/98: process all command-line DEH's first
   // haleyjd  09/03: this just queues them now
   D_ProcessDehCommandLine();

   // haleyjd 09/11/03: queue GFS DEH's
   if(haveGFS)
      D_ProcessGFSDeh(gfs);

   // killough 10/98: set default savename based on executable's name
   // haleyjd 08/28/03: must be done BEFORE bex hash chain init!
   savegamename = estrdup("etersav");

   devparm = !!M_CheckParm("-devparm");         //sf: move up here

   D_IdentifyVersion();
   printf("\n"); // gap

   modifiedgame = false;

   // jff 1/24/98 set both working and command line value of play parms
   // sf: make bool for console
   nomonsters  = clnomonsters  = !!M_CheckParm("-nomonsters");
   respawnparm = clrespawnparm = !!M_CheckParm("-respawn");
   fastparm    = clfastparm    = !!M_CheckParm("-fast");
   // jff 1/24/98 end of set to both working and command line value

   DefaultGameType = gt_single;

   if(M_CheckParm("-deathmatch"))
   {
      DefaultGameType = gt_dm;
      dmtype = 1;
   }
   if(M_CheckParm("-altdeath"))
   {
      DefaultGameType = gt_dm;
      dmtype = 2;
   }
   if(M_CheckParm("-trideath"))  // deathmatch 3.0!
   {
      DefaultGameType = gt_dm;
      dmtype = 3;
   }

   GameType = DefaultGameType;
   G_SetDefaultDMFlags(dmtype, true);

#ifdef GAMEBAR
   psnprintf(title, sizeof(title), "%s", GameModeInfo->startupBanner);
   printf("%s\n", title);
   printf("%s\nBuilt on %s at %s\n", title, version_date,
          version_time);    // killough 2/1/98
#else
   // haleyjd: always provide version date/time
   printf("Built on %s at %s\n", version_date, version_time);
#endif /* GAMEBAR */

   if(devparm)
   {
      printf(D_DEVSTR);
      v_ticker = 1;  // turn on the fps ticker
   }

   // haleyjd 03/10/03: Load GFS Wads
   // 08/08/03: moved first, so that command line overrides
   if(haveGFS)
      D_ProcessGFSWads(gfs);

   // haleyjd 11/22/03: look for loose wads (drag and drop)
   D_LooseWads();

   // add any files specified on the command line with -file wadfile
   // to the wad list

   // killough 1/31/98, 5/2/98: reload hack removed, -wart same as -warp now.

   if((p = M_CheckParm("-file")))
   {
      // the parms after p are wadfile/lump names,
      // until end of parms or another - preceded parm
      // killough 11/98: allow multiple -file parameters

      bool file = modifiedgame = true; // homebrew levels
      while(++p < myargc)
      {
         if(*myargv[p] == '-')
         {
            file = !strcasecmp(myargv[p], "-file");
         }
         else
         {
            if(file)
               D_AddFile(myargv[p], lumpinfo_t::ns_global, NULL, 0, DAF_NONE);
         }
      }
   }

   // ioanch 20160313: demo testing
   if((p = M_CheckParm("-demolog")) && p < myargc - 1)
      G_DemoLogInit(myargv[p + 1]);

   // haleyjd 01/17/11: allow -play also
   const char *playdemoparms[] = { "-playdemo", "-play", NULL };

   if(!(p = M_CheckMultiParm(playdemoparms, 1)) || p >= myargc-1)   // killough
   {
      if((p = M_CheckParm("-fastdemo")) && p < myargc-1)  // killough
         fastdemo = true;            // run at fastest speed possible
      else
         p = M_CheckParm("-timedemo");
   }

   // haleyjd 02/29/2012: support a loose demo on the command line
   const char *loosedemo = NULL;
   if(!p)
      loosedemo = D_LooseDemo();

   if((p && p < myargc - 1) || loosedemo)
   {
      const char *demosource = loosedemo ? loosedemo : myargv[p + 1];
      qstring file;
      
      file = demosource;
      file.addDefaultExtension(".lmp"); // killough

      D_AddFile(file.constPtr(), lumpinfo_t::ns_demos, NULL, 0, DAF_DEMO);
      usermsg("Playing demo '%s'\n", file.constPtr());
   }

   // get skill / episode / map from parms

   // jff 3/24/98 was sk_medium, just note not picked
   startskill = sk_none;
   startepisode = 1;
   startmap = 1;
   autostart = false;

   if((p = M_CheckParm("-skill")) && p < myargc - 1)
   {
      startskill = (skill_t)(myargv[p+1][0]-'1');
      autostart = true;
   }

   if((p = M_CheckParm("-episode")) && p < myargc - 1)
   {
      startepisode = myargv[p+1][0]-'0';
      startmap = 1;
      autostart = true;
   }

   // haleyjd: deatchmatch-only options
   if(GameType == gt_dm)
   {
      if((p = M_CheckParm("-timer")) && p < myargc-1)
      {
         int time = atoi(myargv[p+1]);

         usermsg("Levels will end after %d minute%s.\n",
            time, time > 1 ? "s" : "");
         levelTimeLimit = time;
      }

      // sf: moved from p_spec.c
      // See if -frags has been used
      if((p = M_CheckParm("-frags")) && p < myargc-1)
      {
         int frags = atoi(myargv[p+1]);

         if(frags <= 0)
            frags = 10;  // default 10 if no count provided
         levelFragLimit = frags;
      }

      if((p = M_CheckParm("-avg")) && p < myargc-1)
      {
         levelTimeLimit = 20 * 60 * TICRATE;
         usermsg("Austin Virtual Gaming: Levels will end after 20 minutes");
      }
   }

   if(((p = M_CheckParm("-warp")) ||      // killough 5/2/98
       (p = M_CheckParm("-wart"))) && p < myargc - 1)
   {
      // 1/25/98 killough: fix -warp xxx from crashing Doom 1 / UD
      if(GameModeInfo->flags & GIF_MAPXY)
      {
         startmap = atoi(myargv[p + 1]);
         autostart = true;
      }
      else if(p < myargc - 2)
      {
         startepisode = atoi(myargv[++p]);
         startmap = atoi(myargv[p + 1]);
         autostart = true;
      }
   }

   //jff 1/22/98 add command line parms to disable sound and music
   {
      bool nosound = !!M_CheckParm("-nosound");
      nomusicparm  = nosound || M_CheckParm("-nomusic");
      nosfxparm    = nosound || M_CheckParm("-nosfx");
      s_randmusic  = !!M_CheckParm("-randmusic");
   }
   //jff end of sound/music command line parms

   // killough 3/2/98: allow -nodraw -noblit generally
   nodrawers = !!M_CheckParm("-nodraw");
   noblit    = !!M_CheckParm("-noblit");

   // haleyjd: need to do this before M_LoadDefaults
   C_InitPlayerName();

   startupmsg("M_LoadDefaults", "Load system defaults.");
   M_LoadDefaults();              // load before initing other systems

   bodyquesize = default_bodyquesize; // killough 10/98

   G_ReloadDefaults();    // killough 3/4/98: set defaults just loaded.
   // jff 3/24/98 this sets startskill if it was -1

   // 1/18/98 killough: Z_Init call moved to i_main.c

   D_ProcessWadPreincludes(); // killough 10/98: add preincluded wads at the end

   // haleyjd 08/20/07: also, enumerate and load wads from base/game/autoload
   D_EnumerateAutoloadDir();
   D_GameAutoloadWads();

   startupmsg("W_Init", "Init WADfiles.");
   wGlobalDir.initMultipleFiles(wadfiles);
   usermsg("");  // gap

   // Check for -file in shareware
   //
   // haleyjd 03/22/03: there's no point in trying to detect fake IWADs,
   // especially after user wads have already been linked in, so I've removed
   // that kludge
   if(modifiedgame && (GameModeInfo->flags & GIF_SHAREWARE))
      I_Error("\nYou cannot -file with the shareware version. Register!\n");

   // haleyjd 08/03/13: load any deferred mission metadata
   D_DoDeferredMissionMetaData();

   // haleyjd 11/12/09: Initialize post-W_InitMultipleFiles GameModeInfo
   // overrides and adjustments here.
   D_InitGMIPostWads();

   // haleyjd 10/20/03: use D_ProcessDehInWads again
   D_ProcessDehInWads();

   // killough 10/98: process preincluded .deh files
   // haleyjd  09/03: this just queues them now
   D_ProcessDehPreincludes();

   // haleyjd 08/20/07: queue autoload dir dehs
   D_GameAutoloadDEH();

   // jff 4/24/98 load color translation lumps
   // haleyjd 09/06/12: need to do this before EDF
   V_InitColorTranslation(); 

   // haleyjd 08/28/13: init console command list
   C_AddCommands();

   // haleyjd 09/11/03: All EDF and DeHackEd processing is now
   // centralized here, in order to allow EDF to load from wads.
   // As noted in comments, the other DEH functions above now add
   // their files or lumps to the queue in d_dehtbl.c -- the queue
   // is processed here to parse all files/lumps at once.

   // Init bex hash chaining before EDF
   D_BuildBEXHashChains();

   // Init Aeon before EDF
   AeonScriptManager::Init();

   // Identify root EDF file and process EDF
   D_LoadEDF(gfs);

   // haleyjd 03/27/11: process Hexen scripts
   XL_ParseHexenScripts();

   // Build BEX tables (some are EDF-dependent)
   D_BuildBEXTables();

   // Process the DeHackEd queue, then free it
   D_ProcessDEHQueue();
   
   // haleyjd: moved down turbo to here for player class support
   if((p = M_CheckParm("-turbo")))
   {
      extern int turbo_scale;

      if(p < myargc - 1)
         turbo_scale = atoi(myargv[p + 1]);
      if(turbo_scale < 10)
         turbo_scale = 10;
      if(turbo_scale > 400)
         turbo_scale = 400;
      printf("turbo scale: %i%%\n",turbo_scale);
      E_ApplyTurbo(turbo_scale);
   }

   // killough 2/22/98: copyright / "modified game" / SPA banners removed

   // Ty 04/08/98 - Add 5 lines of misc. data, only if nonblank
   // The expectation is that these will be set in a .bex file

   // haleyjd: in order for these to play the appropriate role, they
   //  should appear in the console if not in text mode startup

   if(textmode_startup)
   {
      if(DEH_StringChanged("STARTUP1"))
         puts(DEH_String("STARTUP1"));
      if(DEH_StringChanged("STARTUP2"))
         puts(DEH_String("STARTUP2"));
      if(DEH_StringChanged("STARTUP3"))
         puts(DEH_String("STARTUP3"));
      if(DEH_StringChanged("STARTUP4"))
         puts(DEH_String("STARTUP4"));
      if(DEH_StringChanged("STARTUP5"))
         puts(DEH_String("STARTUP5"));
   }
   // End new startup strings

   startupmsg("V_InitMisc","Init miscellaneous video patches.");
   V_InitMisc();

   startupmsg("C_Init", "Init console.");
   C_Init();

   startupmsg("I_Init","Setting up machine state.");
   I_Init();

   // devparm override of early set graphics mode
   if(!textmode_startup && !devparm)
   {
      startupmsg("D_SetGraphicsMode", "Set graphics mode");
      D_SetGraphicsMode();
   }

   startupmsg("R_Init", "Init DOOM refresh daemon");
   R_Init();

   startupmsg("P_Init", "Init Playloop state.");
   P_Init();

   startupmsg("HU_Init", "Setting up heads up display.");
   HU_Init();

   startupmsg("ST_Init", "Init status bar.");
   ST_Init();

   startupmsg("MN_Init", "Init menu.");
   MN_Init();

   startupmsg("F_Init", "Init finale.");
   F_Init();

   startupmsg("S_Init", "Setting up sound.");
   S_Init(snd_SfxVolume, snd_MusicVolume);

   //
   // NETCODE_FIXME: Netgame check.
   //

   startupmsg("D_CheckNetGame", "Check netgame status.");
   D_CheckNetGame();

   // haleyjd 04/10/03: set coop gametype
   // haleyjd 04/01/10: support -solo-net parameter
   if((netgame || M_CheckParm("-solo-net")) && GameType == gt_single)
   {
      GameType = DefaultGameType = gt_coop;
      G_SetDefaultDMFlags(0, true);
   }

   // check for command-line override of dmflags
   if((p = M_CheckParm("-dmflags")) && p < myargc-1)
   {
      dmflags = default_dmflags = (unsigned int)atoi(myargv[p+1]);
   }

   // haleyjd: this SHOULD be late enough...
   startupmsg("G_LoadDefaults", "Init keybindings.");
   G_LoadDefaults();

   //
   // CONSOLE_FIXME: This may not be the best time for scripts.
   // Reconsider this as is appropriate.
   //

   // haleyjd: AFTER keybindings for overrides
   startupmsg("D_AutoExecScripts", "Executing console scripts.");
   D_AutoExecScripts();

   // haleyjd 08/20/07: autoload dir csc's
   D_GameAutoloadCSC();

   // haleyjd 03/10/03: GFS csc's
   if(haveGFS)
   {
      D_ProcessGFSCsc(gfs);

      // this is the last GFS action, so free the gfs now
      G_FreeGFS(gfs);
   }

   // haleyjd: GFS is no longer valid from here!

   // haleyjd 08/20/07: done with base/game/autoload directory
   D_CloseAutoloadDir();

   if(devparm) // we wait if in devparm so the user can see the messages
   {
      printf("devparm: press a key..\n");
      getchar();
   }

   ///////////////////////////////////////////////////////////////////
   //
   // Must be in Graphics mode by now!
   //

   // check

   if(in_textmode)
      D_SetGraphicsMode();

   // Initialize ACS
   ACS_Init();

   // haleyjd: updated for eternity
   C_Printf("\n");
   C_Separator();
   C_Printf("\n"
            FC_HI "The Eternity Engine\n"
            FC_NORMAL "By James Haley and Stephen McGranahan\n"
            "http://doomworld.com/eternity/ \n"
            "Version %i.%02i.%02i '%s' \n\n",
            version/100, version%100, subversion, version_name);

#if defined(TOKE_MEMORIAL)
   // haleyjd 08/30/06: for v3.33.50 Phoenix: RIP Toke
   C_Printf(FC_GREEN "Dedicated to the memory of our friend\n"
            "Dylan 'Toke' McIntosh  Jan 14 1983 - Aug 19 2006\n");
#elif defined(ASSY_MEMORIAL)
   // haleyjd 08/29/08
   C_Printf(FC_GREEN "Dedicated to the memory of our friend\n"
            "Jason 'Amaster' Masihdas 12 Oct 1981 - 14 Jun 2007\n");
#elif defined(KATE_MEMORIAL)
   // MaxW: 2017/12/27
   // Note: FC_CUSTOM1 is temporarily pink/purple
   C_Printf(FC_CUSTOM1 "Dedicated to team member\n"
            "and our dear friend:\n"
            "Kaitlyn Anne Fox\n"
            "(withheld) - 19 Dec 2017\n"
            "May her spirit and memory\n"
            "live on into Eternity\n");
#endif

   // haleyjd: if we didn't do textmode startup, these didn't show up
   //  earlier, so now is a cool time to show them :)
   // haleyjd: altered to prevent printf attacks
   if(!textmode_startup)
   {
      if(DEH_StringChanged("STARTUP1"))
         C_Printf("%s", DEH_String("STARTUP1"));
      if(DEH_StringChanged("STARTUP2"))
         C_Printf("%s", DEH_String("STARTUP2"));
      if(DEH_StringChanged("STARTUP3"))
         C_Printf("%s", DEH_String("STARTUP3"));
      if(DEH_StringChanged("STARTUP4"))
         C_Printf("%s", DEH_String("STARTUP4"));
      if(DEH_StringChanged("STARTUP5"))
         C_Printf("%s", DEH_String("STARTUP5"));
   }

   if(!textmode_startup && !devparm)
      C_Update();

   idmusnum = -1; //jff 3/17/98 insure idmus number is blank

#if 0
   // check for a driver that wants intermission stats
   if((p = M_CheckParm("-statcopy")) && p < myargc-1)
   {
      // for statistics driver
      extern void *statcopy;

      // killough 5/2/98: this takes a memory
      // address as an integer on the command line!

      statcopy = (void *)atoi(myargv[p+1]);
      usermsg("External statistics registered.");
   }
#endif

   // sf: -blockmap option as a variable now
   if(M_CheckParm("-blockmap")) r_blockmap = true;

   // start the appropriate game based on parms

   // killough 12/98:
   // Support -loadgame with -record and reimplement -recordfrom.
   if((slot = M_CheckParm("-recordfrom")) && (p = slot+2) < myargc)
      G_RecordDemo(myargv[p]);
   else if((p = M_CheckParm("-recordfromto")) && p < myargc - 2)
   {
      autostart = true;
      G_RecordDemoContinue(myargv[p + 1], myargv[p + 2]);
   }
   else
   {
      // haleyjd 01/17/11: allow -recorddemo as well
      const char *recordparms[] = { "-record", "-recorddemo", NULL };

      slot = M_CheckParm("-loadgame");
 
      if((p = M_CheckMultiParm(recordparms, 1)) && ++p < myargc)
      {
         autostart = true;
         G_RecordDemo(myargv[p]);
      }
   }

   if((p = M_CheckParm("-fastdemo")) && ++p < myargc)
   {                                 // killough
      fastdemo = true;                // run at fastest speed possible
      timingdemo = true;              // show stats after quit
      G_DeferedPlayDemo(myargv[p]);
      singledemo = true;              // quit after one demo
   }
   else if((p = M_CheckParm("-timedemo")) && ++p < myargc)
   {
      // haleyjd 10/16/08: restored to MBF status
      singletics = true;
      timingdemo = true;            // show stats after quit
      G_DeferedPlayDemo(myargv[p]);
      singledemo = true;            // quit after one demo
   }
   else if((p = M_CheckMultiParm(playdemoparms, 1)) && ++p < myargc)
   {
      G_DeferedPlayDemo(myargv[p]);
      singledemo = true;          // quit after one demo
   }
   else if(loosedemo)
   {
      G_DeferedPlayDemo(loosedemo);
      singledemo = true;
   }

   startlevel = estrdup(G_GetNameForMap(startepisode, startmap));

   if(slot && ++slot < myargc)
   {
      char *file = NULL;
      size_t len = M_StringAlloca(&file, 2, 26, basesavegame, savegamename);
      slot = atoi(myargv[slot]);        // killough 3/16/98: add slot info
      G_SaveGameName(file, len, slot); // killough 3/22/98
      G_LoadGame(file, slot, true);     // killough 5/15/98: add command flag
   }
   else if(!singledemo)                    // killough 12/98
   {
      if(autostart || netgame)
      {
         // haleyjd 01/16/11: old demo options must be set BEFORE G_InitNew
         if(M_CheckParm("-vanilla") > 0)
            G_SetOldDemoOptions();

         G_InitNewNum(startskill, startepisode, startmap);
         if(demorecording)
            G_BeginRecording();
      }
      else
         D_StartTitle();                 // start up intro loop

      /*
      if(netgame)
      {
         //
         // NETCODE_FIXME: C_SendNetData.
         //
         C_SendNetData();

         if(demorecording)
            G_BeginRecording();
      }
      */
   }

   // a lot of alloca calls are made during startup; kill them all now.
   Z_FreeAlloca();
}

//=============================================================================
//
// Main Routine
//

//
// D_DoomMain
//
void D_DoomMain()
{
   D_DoomInit();

   oldgamestate = wipegamestate = gamestate;

   // haleyjd 02/23/04: fix problems with -warp
   if(autostart)
      oldgamestate = GS_NOSTATE;

   // killough 12/98: inlined D_DoomLoop
   while(1)
   {
      // frame synchronous IO operations
      I_StartFrame();

      TryRunTics();

      // killough 3/16/98: change consoleplayer to displayplayer
      S_UpdateSounds(players[displayplayer].mo); // move positional sounds

      // Update display, next frame, with current state.
      D_Display();

      // Sound mixing for the buffer is synchronous.
      I_UpdateSound();

      // Synchronous sound output is explicitly called.
      // Update sound output.
      I_SubmitSound();

      // haleyjd 12/06/06: garbage-collect all alloca blocks
      Z_FreeAlloca();
   }
}

//============================================================================
// 
// Console Commands
//

VARIABLE_TOGGLE(d_drawfps, NULL, onoff);
CONSOLE_VARIABLE(d_drawfps, d_drawfps, 0) {}

//----------------------------------------------------------------------------
//
// $Log: d_main.c,v $
// Revision 1.47  1998/05/16  09:16:51  killough
// Make loadgame checksum friendlier
//
// Revision 1.46  1998/05/12  10:32:42  jim
// remove LEESFIXES from d_main
//
// Revision 1.45  1998/05/06  15:15:46  jim
// Documented IWAD routines
//
// Revision 1.44  1998/05/03  22:26:31  killough
// beautification, declarations, headers
//
// Revision 1.43  1998/04/24  08:08:13  jim
// Make text translate tables lumps
//
// Revision 1.42  1998/04/21  23:46:01  jim
// Predefined lump dumper option
//
// Revision 1.39  1998/04/20  11:06:42  jim
// Fixed print of IWAD found
//
// Revision 1.37  1998/04/19  01:12:19  killough
// Fix registered check to work with new lump namespaces
//
// Revision 1.36  1998/04/16  18:12:50  jim
// Fixed leak
//
// Revision 1.35  1998/04/14  08:14:18  killough
// Remove obsolete adaptive_gametics code
//
// Revision 1.34  1998/04/12  22:54:41  phares
// Remaining 3 Setup screens
//
// Revision 1.33  1998/04/11  14:49:15  thldrmn
// Allow multiple deh/bex files
//
// Revision 1.32  1998/04/10  06:31:50  killough
// Add adaptive gametic timer
//
// Revision 1.31  1998/04/09  09:18:17  thldrmn
// Added generic startup strings for BEX use
//
// Revision 1.30  1998/04/06  04:52:29  killough
// Allow demo_insurance=2, fix fps regression wrt redrawsbar
//
// Revision 1.29  1998/03/31  01:08:11  phares
// Initial Setup screens and Extended HELP screens
//
// Revision 1.28  1998/03/28  15:49:37  jim
// Fixed merge glitches in d_main.c and g_game.c
//
// Revision 1.27  1998/03/27  21:26:16  jim
// Default save dir offically . now
//
// Revision 1.26  1998/03/25  18:14:21  jim
// Fixed duplicate IWAD search in .
//
// Revision 1.25  1998/03/24  16:16:00  jim
// Fixed looking for wads message
//
// Revision 1.23  1998/03/24  03:16:51  jim
// added -iwad and -save parms to command line
//
// Revision 1.22  1998/03/23  03:07:44  killough
// Use G_SaveGameName, fix some remaining default.cfg's
//
// Revision 1.21  1998/03/18  23:13:54  jim
// Deh text additions
//
// Revision 1.19  1998/03/16  12:27:44  killough
// Remember savegame slot when loading
//
// Revision 1.18  1998/03/10  07:14:58  jim
// Initial DEH support added, minus text
//
// Revision 1.17  1998/03/09  07:07:45  killough
// print newline after wad files
//
// Revision 1.16  1998/03/04  08:12:05  killough
// Correctly set defaults before recording demos
//
// Revision 1.15  1998/03/02  11:24:25  killough
// make -nodraw -noblit work generally, fix ENDOOM
//
// Revision 1.14  1998/02/23  04:13:55  killough
// My own fix for m_misc.c warning, plus lots more (Rand's can wait)
//
// Revision 1.11  1998/02/20  21:56:41  phares
// Preliminarey sprite translucency
//
// Revision 1.10  1998/02/20  00:09:00  killough
// change iwad search path order
//
// Revision 1.9  1998/02/17  06:09:35  killough
// Cache D_DoomExeDir and support basesavegame
//
// Revision 1.8  1998/02/02  13:20:03  killough
// Ultimate Doom, -fastdemo -nodraw -noblit support, default_compatibility
//
// Revision 1.7  1998/01/30  18:48:15  phares
// Changed textspeed and textwait to functions
//
// Revision 1.6  1998/01/30  16:08:59  phares
// Faster end-mission text display
//
// Revision 1.5  1998/01/26  19:23:04  phares
// First rev with no ^Ms
//
// Revision 1.4  1998/01/26  05:40:12  killough
// Fix Doom 1 crashes on -warp with too few args
//
// Revision 1.3  1998/01/24  21:03:04  jim
// Fixed disappearence of nomonsters, respawn, or fast mode after demo play or IDCLEV
//
// Revision 1.1.1.1  1998/01/19  14:02:53  rand
// Lee's Jan 19 sources
//
//----------------------------------------------------------------------------
