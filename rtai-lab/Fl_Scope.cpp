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

#include <Fl_Scope.h>

void Fl_Scope::show_trace(int n, int value)
{
	if (n < num_of_traces) {
		value ? Trace_Visible[n] = true : Trace_Visible[n] = false;
	}
}

void Fl_Scope::pause(int value)
{
	value ? Pause_Flag = true : Pause_Flag = false;
}

int Fl_Scope::pause()
{
	return Pause_Flag;
}

void Fl_Scope::oneshot(int value)
{
	value ? OneShot_Flag = true : OneShot_Flag = false;
}

int Fl_Scope::oneshot()
{
	return OneShot_Flag;
}

void Fl_Scope::grid_visible(int value)
{
	value ? Grid_Visible = true : Grid_Visible = false;
}

void Fl_Scope::grid_color(float r, float g, float b)
{
	Grid_rgb[0] = r;
	Grid_rgb[1] = g;
	Grid_rgb[2] = b;
}

Fl_Color Fl_Scope::grid_color()
{
	return Grid_Color;
}

void Fl_Scope::grid_free_color()
{
	Grid_Color = FL_FREE_COLOR;
}

float Fl_Scope::grid_r()
{
	return Grid_rgb[0];
}

float Fl_Scope::grid_g()
{
	return Grid_rgb[1];
}

float Fl_Scope::grid_b()
{
	return Grid_rgb[2];
}

void Fl_Scope::bg_color(float r, float g, float b)
{
	Bg_rgb[0] = r;
	Bg_rgb[1] = g;
	Bg_rgb[2] = b;
}

Fl_Color Fl_Scope::bg_color()
{
	return Bg_Color;
}

void Fl_Scope::bg_free_color()
{
	Bg_Color = FL_FREE_COLOR;
}

float Fl_Scope::bg_r()
{
	return Bg_rgb[0];
}

float Fl_Scope::bg_g()
{
	return Bg_rgb[1];
}

float Fl_Scope::bg_b()
{
	return Bg_rgb[2];
}

void Fl_Scope::trace_color(int n, float r, float g, float b)
{
	if (n < num_of_traces) {
		Trace_rgb[n][0] = r;
		Trace_rgb[n][1] = g;
		Trace_rgb[n][2] = b;
	}
}

Fl_Color Fl_Scope::trace_color(int n)
{
	if (n < num_of_traces) {
		return Trace_Color[n];
	} else return 0;
}

void Fl_Scope::trace_free_color(int n)
{
	if (n < num_of_traces) {
		Trace_Color[n] = FL_FREE_COLOR;
	}
}

float Fl_Scope::tr_r(int n)
{
	return Trace_rgb[n][0];
}

float Fl_Scope::tr_g(int n)
{
	return Trace_rgb[n][1];
}

float Fl_Scope::tr_b(int n)
{
	return Trace_rgb[n][2];
}

void Fl_Scope::sampling_frequency(float freq)
{
	Sampling_Frequency = freq;
}

float Fl_Scope::sampling_frequency()
{
	return Sampling_Frequency;
}

void Fl_Scope::trace_offset(int n, float value)
{
	if (n < num_of_traces) {
		Trace_Offset_Value[n] = value;
		Trace_Offset[n] = (h()/2.)*value;
	}
}

void Fl_Scope::time_range(float range)
{
	Time_Range = range;
}

float Fl_Scope::time_range()
{
	return Time_Range;
}

void Fl_Scope::y_range_inf(int n, float range)
{
	if (n < num_of_traces) {
		Y_Range_Inf[n] = range;
	}
}

void Fl_Scope::y_range_sup(int n, float range)
{
	if (n < num_of_traces) {
		Y_Range_Sup[n] = range;
	}
}

void Fl_Scope::setdx()
{
	dx = w()/(Sampling_Frequency*Time_Range);
}

float Fl_Scope::getdx()
{
	return dx;
}

void Fl_Scope::trace_length(int width)
{
	Trace_Len = (int)(width/dx);
}

int Fl_Scope::trace_length()
{
	return Trace_Len;
}

void Fl_Scope::trace_pointer(int pos)
{
	Trace_Ptr = pos;
}

int Fl_Scope::trace_pointer()
{
	return Trace_Ptr;
}

int Fl_Scope::increment_trace_pointer()
{
	return Trace_Ptr++;
}

void Fl_Scope::add_to_trace(int pos, int n, float val)
{
	Trace[n][pos] = val;
}

void Fl_Scope::add_to_trace(int n, float val)
{
	memmove(Trace[n] + 1, Trace[n], (Trace_Len - 1)*sizeof(float));
	Trace[n][0] = val;
}

void Fl_Scope::initgl()
{
	glViewport(0, 0, w(), h());
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, w(), 0, h(), -1, 1);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glEnable(GL_DEPTH_TEST);
	gl_font(FL_HELVETICA_BOLD, 12);
	glClearColor(Bg_rgb[0], Bg_rgb[1], Bg_rgb[2], 0.0);
	glClear(GL_COLOR_BUFFER_BIT);	
}

void Fl_Scope::drawticks()
{
	float lx = (w()/NDIV_GRID_X)/10., ly = (h()/NDIV_GRID_Y)/10.;
	float x = 0., y = 0.;

	glLineWidth(0.1);
	glColor3f(Grid_rgb[0], Grid_rgb[1], Grid_rgb[2]);
	glBegin(GL_LINES);
	for (;;) {
		y += dyGrid;
		if (y >= h()) break;
		glVertex2f(0., y);
		glVertex2f(lx, y);
		glVertex2f(w()-lx, y);
		glVertex2f(w(), y);
	}
	for (;;) {
		x += dxGrid;
		if (x >= w()) break;
		glVertex2f(x, 0.);
		glVertex2f(x, ly);
		glVertex2f(x, h()-ly);
		glVertex2f(x, h());
	}
	glEnd();
}

void Fl_Scope::drawgrid()
{
	float x = 0., y = 0.;

	glLineWidth(0.1);
	glLineStipple(1, 0xAAAA);
	glEnable(GL_LINE_STIPPLE);
	glColor3f(Grid_rgb[0], Grid_rgb[1], Grid_rgb[2]);
	glBegin(GL_LINES);
	for (;;) {
		y += dyGrid;
		if (y >= h()) break;
		glVertex2f(0., y);
		glVertex2f(w(), y);
	}
	for (;;) {
		x += dxGrid;
		if (x >= w()) break;
		glVertex2f(x, 0.);
		glVertex2f(x, h());
	}
	glEnd();
	glDisable(GL_LINE_STIPPLE);
}

void Fl_Scope::draw()
{
//	char secdiv[20];
	if (!valid()) {
		initgl();
		dxGrid = w()/(float)((int)(w()/(w()/((float)NDIV_GRID_X))));
		dyGrid = (h()/2.)/(float)((int)((h()/2.)/(h()/((float)NDIV_GRID_Y))));
		for (int n = 0; n < num_of_traces; n++) {
			Trace_Offset[n] = (h()/2.)*Trace_Offset_Value[n];
		}
	}
	dx = w()/(Sampling_Frequency*Time_Range);
	Trace_Len = (int)(w()/dx);
	glClearColor(Bg_rgb[0], Bg_rgb[1], Bg_rgb[2], 0.0);
	glClear(GL_COLOR_BUFFER_BIT);
	drawticks();
	if (Grid_Visible) {
		drawgrid();
	}
	glLineWidth(0.1);
	for (int nn = 0; nn < num_of_traces; nn++) {
		glColor3f(Trace_rgb[nn][0], Trace_rgb[nn][1], Trace_rgb[nn][2]);
		if (Trace_Visible[nn]) {
			glBegin(GL_LINE_STRIP);
			rtPoint p;
			p.x = p.y = 0;
			for (int n = Trace_Len - 1; n >= 0; n--) {
				p.y = Trace[nn][n]*(((float)(h()))/(float)(Y_Range_Sup[nn]-Y_Range_Inf[nn])) + Trace_Offset[nn];
				glVertex2f(p.x, p.y);
				p.x += dx;
			}
			glEnd();
		}
	}
/*
	gl_color(FL_GRAY);
	glDisable(GL_DEPTH_TEST);
	sprintf(secdiv, "S/Div %1.5f", Time_Range/NDIV_GRID_X);
	gl_font(FL_TIMES, 10);
	gl_draw(secdiv, 5.0f, 5.0f);
	glEnable(GL_DEPTH_TEST);
*/
}

Fl_Scope::Fl_Scope(int x, int y, int w, int h, int ntr, const char *title):Fl_Gl_Window(x,y,w,h,title)
{
	int i;

	num_of_traces = ntr;
	dxGrid = 0.;
	dyGrid = 0.;
	Time_Range = 1.0;
	Trace_Visible = new int[num_of_traces];
	Y_Div = new float[num_of_traces];
	Y_Range_Inf = new float[num_of_traces];
	Y_Range_Sup = new float[num_of_traces];
	Trace_rgb = new float*[num_of_traces];
	Trace_Color = new Fl_Color[num_of_traces];
	Trace_Offset = new float[num_of_traces];
	Trace_Offset_Value = new float[num_of_traces];

	Trace = new float*[num_of_traces];

	for (i = 0; i < num_of_traces; i++) {
		Trace_Visible[i] = true;
		Y_Div[i] = 2.5;
		Y_Range_Inf[i] = -Y_Div[i]*((float)NDIV_GRID_Y/2.);
		Y_Range_Sup[i] = Y_Div[i]*((float)NDIV_GRID_Y/2.);
		Trace_rgb[i] = new float[3];
		Trace_Offset[i] = h/2;
		Trace_Offset_Value[i] = 1.0;
		Trace[i] = new float[MAX_TRACE_LENGTH];
	}
	Pause_Flag = false;
	OneShot_Flag = false;
	Grid_Visible = true;
	Grid_rgb[0] = 0.650;
	Grid_rgb[1] = 0.650;
	Grid_rgb[2] = 0.650;
	Grid_Color = FL_GRAY;
	fl_set_color(FL_FREE_COLOR, fl_rgb((unsigned char)(Grid_rgb[0]*255.), (unsigned char)(Grid_rgb[1]*255.), (unsigned char)(Grid_rgb[2]*255.)));
	Grid_Color = FL_FREE_COLOR;
	Bg_rgb[0] = 0.388;
	Bg_rgb[1] = 0.451;
	Bg_rgb[2] = 0.604;
	Bg_Color = FL_GRAY;
	fl_set_color(FL_FREE_COLOR, fl_rgb((unsigned char)(Bg_rgb[0]*255.), (unsigned char)(Bg_rgb[1]*255.), (unsigned char)(Bg_rgb[2]*255.)));
	Bg_Color = FL_FREE_COLOR;
	for (i = 0; i < num_of_traces; i++) {
		Trace_rgb[i][0] = 1.0;
		Trace_rgb[i][1] = 1.0;
		Trace_rgb[i][2] = 1.0;
		Trace_Color[i] = FL_GRAY;
		fl_set_color(FL_FREE_COLOR, fl_rgb((unsigned char)(Trace_rgb[i][0]*255.), (unsigned char)(Trace_rgb[i][1]*255.), (unsigned char)(Trace_rgb[i][2]*255.)));
	}
}
