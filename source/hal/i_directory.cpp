// Emacs style mode select   -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// Copyright(C) 2013 David Hill et al.
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
// Additional terms and conditions compatible with the GPLv3 apply. See the
// file COPYING-EE for details.
//
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//    Directory Manipulation
//
//-----------------------------------------------------------------------------

#if _MSC_VER >= 1914
#include <locale>
#include <filesystem>
#include <string>
#endif

#include "../z_zone.h"

#include "i_directory.h"

#include "i_platform.h"
#include "../m_qstr.h"

//
// All this for PATH_MAX
//
#if EE_CURRENT_PLATFORM == EE_PLATFORM_LINUX \
 || EE_CURRENT_PLATFORM == EE_PLATFORM_MACOSX \
 || EE_CURRENT_PLATFORM == EE_PLATFORM_FREEBSD \
 || EE_CURRENT_PLATFORM == EE_PLATFORM_SWITCH
#include <limits.h>
#elif EE_CURRENT_PLATFORM == EE_PLATFORM_WINDOWS
#include <windows.h>
#endif

//=============================================================================
//
// Global Functions
//

//
// I_CreateDirectory
//
bool I_CreateDirectory(const qstring &path)
{
#if (EE_CURRENT_PLATFORM == EE_PLATFORM_LINUX) || (EE_CURRENT_PLATFORM == EE_PLATFORM_SWITCH)
   if(!mkdir(path.constPtr(), S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH))
      return true;
#endif

   return false;
}

//
// I_PlatformInstallDirectory
//
const char *I_PlatformInstallDirectory()
{
#if EE_CURRENT_PLATFORM == EE_PLATFORM_LINUX
   struct stat sbuf;

   // Prefer /usr/local, but fall back to just /usr.
   if(!stat("/usr/local/share/eternity/base", &sbuf) && S_ISDIR(sbuf.st_mode))
      return "/usr/local/share/eternity/base";
   else
      return "/usr/share/eternity/base";
#elif EE_CURRENT_PLATFORM == EE_PLATFORM_SWITCH
   return "/switch/eternity/base"; // this is our "home dir", the default
#endif

   return nullptr;
}

//
// Clears all symbolic links from a path (which may be relative) and returns the
// real path in "real"
//
void I_GetRealPath(const char *path, qstring &real)
{
#if EE_CURRENT_PLATFORM == EE_PLATFORM_WINDOWS
#if _MSC_VER >= 1914
   std::filesystem::path pathobj(path);
   pathobj = std::filesystem::canonical(pathobj);

   // Has to be converted since fs::value_type is wchar_t on Windows
   std::wstring wpath(pathobj.c_str());
   char *ret = ecalloc(char *, wpath.length() + 1, 1);
   WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, ret,
                       static_cast<int>(wpath.length()), NULL, NULL);
   real = ret;
   efree(ret);

   // wstring_convert became deprecated and didn't get replaced
   //std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
   //std::string spath(convertor.to_bytes(wpath));
   //real = spath.c_str();
#else
   // MaxW: I cannot be assed to make this work without std::filesystem
   real = path;
#endif


#elif EE_CURRENT_PLATFORM == EE_PLATFORM_LINUX \
   || EE_CURRENT_PLATFORM == EE_PLATFORM_MACOSX \
   || EE_CURRENT_PLATFORM == EE_PLATFORM_FREEBSD

   char result[PATH_MAX + 1];
   if(realpath(path, result))
      real = result;
   else
      real = path;   // failure

#else
#warning Unknown platform; this will merely copy "path" to "real"
   real = path;
#endif
}

// EOF

