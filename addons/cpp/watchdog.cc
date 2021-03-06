/*
 * Project: rtai_cpp - RTAI C++ Framework 
 *
 * File: $Id: watchdog.cc,v 1.4 2013/10/22 14:54:15 ando Exp $
 *
 * Copyright: (C) 2001,2002 Erwin Rol <erwin@muffin.org>
 *
 * Licence:
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 * 
 */

#include "watchdog.h"

namespace RTAI {

namespace Watchdog {
	
int set_grace(int new_value)
{
	return rt_wdset_grace( new_value );
}

int set_gracediv(int new_value)
{
	return rt_wdset_gracediv( new_value );
}

wd_policy set_policy(wd_policy new_value)
{
	return rt_wdset_policy( new_value );
}

int set_slip(int new_value)
{
	return rt_wdset_slip( new_value );
}

int set_stretch(int new_value)
{
	return rt_wdset_stretch( new_value );	
}

int set_limit(int new_value)
{
	return rt_wdset_limit( new_value );	
}

int set_safety(int new_value)
{
	return rt_wdset_safety( new_value );
}

}; // namespace Watchdog

}; // namespace RTAI
