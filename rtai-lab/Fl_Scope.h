/*
COPYRIGHT (C) 2003  Lorenzo Dozio (dozio@aero.polimi.it)
                    Paolo Mantegazza (mantegazza@aero.polimi.it)

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
*/

#ifndef _FL_SCOPE_H_
#define _FL_SCOPE_H_

#include <efltk/Fl.h>
#include <efltk/Fl_Gl_Window.h>
#include <efltk/Fl_Color.h>
#include <efltk/gl.h>
#include <GL/glu.h>

#define NDIV_GRID_X           10
#define NDIV_GRID_Y           8
#define MAX_TRACE_LENGTH      100000

typedef struct rtPointStruct rtPoint;

struct rtPointStruct {
        float x;
        float y;
};

class Fl_Scope : public Fl_Gl_Window
{
	public:
		Fl_Scope(int x, int y, int w, int h, int ntr, const char *title="rtScope");
		void sampling_frequency(float);
		float sampling_frequency();
		void setdx();
		float getdx();
		void trace_length(int);
		int trace_length();
		void trace_pointer(int pos);
		int trace_pointer();
		int increment_trace_pointer();
		void add_to_trace(int pos, int n, float val);
		void add_to_trace(int n, float val);
		void pause(int);
		int pause();
		void oneshot(int);
		int oneshot();
		void grid_visible(int);
		void grid_color(float r, float g, float b);
		Fl_Color grid_color();
		void grid_free_color();
		float grid_r();
		float grid_g();
		float grid_b();
		void bg_color(float r, float g, float b);
		Fl_Color bg_color();
		void bg_free_color();
		float bg_r();
		float bg_g();
		float bg_b();
		void time_range(float range);
		float time_range();
		void show_trace(int , int);
		void y_range_inf(int, float);
		void y_range_sup(int, float);
		void trace_color(int, float r, float g, float b);
		Fl_Color trace_color(int);
		void trace_free_color(int);
		float tr_r(int);
		float tr_g(int);
		float tr_b(int);
		void trace_offset(int, float);
	private:
		float Sampling_Frequency;
		float dx;
		int Trace_Len, Trace_Ptr;
		float **Trace;
		int Pause_Flag;
		int OneShot_Flag;
		int Grid_Visible;
		float Grid_rgb[3];
		Fl_Color Grid_Color;
		float Bg_rgb[3];
		Fl_Color Bg_Color;
		float Time_Range;
		int num_of_traces;
		int *Trace_Visible;
		float *Y_Div;
		float *Y_Range_Sup;
		float *Y_Range_Inf;
		float **Trace_rgb;
		Fl_Color *Trace_Color;
		float *Trace_Offset;
		float *Trace_Offset_Value;
	protected:
		void initgl();
		void drawticks();
		void drawgrid();
		void draw();
		float dxGrid;
		float dyGrid;
};

#endif
