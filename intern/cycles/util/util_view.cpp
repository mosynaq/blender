/*
 * Copyright 2011-2013 Blender Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util_opengl.h"
#include "util_time.h"
#include "util_view.h"

#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <GL/glut.h>
#endif

CCL_NAMESPACE_BEGIN

/* structs */

struct View {
	ViewInitFunc initf;
	ViewExitFunc exitf;
	ViewResizeFunc resize;
	ViewDisplayFunc display;
	ViewKeyboardFunc keyboard;

	bool first_display;
	bool redraw;

	int width, height;
} V;

/* public */

static void view_display_text(int x, int y, const char *text)
{
	const char *c;

	glRasterPos3f(x, y, 0);

	for(c = text; *c != '\0'; c++)
		glutBitmapCharacter(GLUT_BITMAP_HELVETICA_10, *c);
}

void view_display_info(const char *info)
{
	const int height = 20;

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glColor4f(0.1f, 0.1f, 0.1f, 0.8f);
	glRectf(0.0f, V.height - height, V.width, V.height);
	glDisable(GL_BLEND);

	glColor3f(0.5f, 0.5f, 0.5f);

	view_display_text(10, 7 + V.height - height, info);

	glColor3f(1.0f, 1.0f, 1.0f);
}

void view_display_help()
{
	const int w = V.width / 1.15;
	const int h = V.height / 1.15;

	const int x1 = (V.width - w) / 2;
	const int x2 = x1 + w;

	const int y1 = (V.height - h) / 2;
	const int y2 = y1 + h;

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glColor4f(0.5f, 0.5f, 0.5f, 0.8f);
	glRectf(x1, y1, x2, y2);
	glDisable(GL_BLEND);

	glColor3f(0.8f, 0.8f, 0.8f);

	view_display_text(x1+20, y2-20, "Cycles Renderer");
	view_display_text(x1+20, y2-40, "(C) 2011-2014 Blender Foundation");
	view_display_text(x1+20, y2-80, "Help:");
	view_display_text(x1+20, y2-100, "h:  Toggle this help message");
	view_display_text(x1+20, y2-120, "r:  Restart the render");
	view_display_text(x1+20, y2-140, "q:  Quit the program");
	view_display_text(x1+20, y2-160, "esc:  Cancel the render");

	glColor3f(1.0f, 1.0f, 1.0f);
}

static void view_display()
{
	if(V.first_display) {
		if(V.initf) V.initf();
		if(V.exitf) atexit(V.exitf);

		V.first_display = false;
	}

	glClearColor(0.05f, 0.05f, 0.05f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluOrtho2D(0, V.width, 0, V.height);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	glRasterPos3f(0, 0, 0);

	if(V.display)
		V.display();

	glutSwapBuffers();
}

static void view_reshape(int width, int height)
{
	if(width <= 0 || height <= 0)
		return;
	
	V.width = width;
	V.height = height;

	glViewport(0, 0, width, height);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	if(V.resize)
		V.resize(width, height);
}

static void view_keyboard(unsigned char key, int x, int y)
{
	if(V.keyboard)
		V.keyboard(key);

	if(key == 'm')
		printf("mouse %d %d\n", x, y);
	if(key == 'q') {
		if(V.exitf) V.exitf();
		exit(0);
	}
}

static void view_idle(void)
{
	if(V.redraw) {
		V.redraw = false;
		glutPostRedisplay();
	}

	time_sleep(0.1f);
}

void view_main_loop(const char *title, int width, int height,
	ViewInitFunc initf, ViewExitFunc exitf,
	ViewResizeFunc resize, ViewDisplayFunc display,
	ViewKeyboardFunc keyboard)
{
	const char *name = "app";
	char *argv = (char*)name;
	int argc = 1;

	memset(&V, 0, sizeof(V));
	V.width = width;
	V.height = height;
	V.first_display = true;
	V.redraw = false;
	V.initf = initf;
	V.exitf = exitf;
	V.resize = resize;
	V.display = display;
	V.keyboard = keyboard;

	glutInit(&argc, &argv);
	glutInitWindowSize(width, height);
	glutInitWindowPosition(0, 0);
	glutInitDisplayMode(GLUT_RGB|GLUT_DOUBLE|GLUT_DEPTH);
	glutCreateWindow(title);

#ifndef __APPLE__
	glewInit();
#endif

	view_reshape(width, height);

	glutDisplayFunc(view_display);
	glutIdleFunc(view_idle);
	glutReshapeFunc(view_reshape);
	glutKeyboardFunc(view_keyboard);

	glutMainLoop();
}

void view_redraw()
{
	V.redraw = true;
}

CCL_NAMESPACE_END

