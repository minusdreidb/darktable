/*
    This file is part of darktable,
    copyright (c) 2011--2012 henrik andersson.
    copyright (c) 2012 ulrich pegelow.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    darktable is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with darktable.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef DT_LIGHTROOM_H
#define DT_LIGHTROOM_H

#include "develop/imageop.h"

/* Import some lightroom develop options
   When called from lightable : dev == NULL, in this case only the tags are imported
   When called from darkroom  : dev != NULL, in this case only develop data are imported
*/
void dt_lightroom_import (int imgid, dt_develop_t *dev, gboolean iauto);

/* returns NULL if not found, or g_strdup'ed pathname, the caller should g_free it. */
char *dt_get_lightroom_xmp (int imgid);

#endif

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-space on;
