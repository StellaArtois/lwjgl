/* 
 * Copyright (c) 2002 Light Weight Java Game Library Project
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are 
 * met:
 * 
 * * Redistributions of source code must retain the above copyright 
 *   notice, this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 * * Neither the name of 'Light Weight Java Game Library' nor the names of 
 *   its contributors may be used to endorse or promote products derived 
 *   from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR 
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, 
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, 
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR 
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING 
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
 
/**
 * $Id$
 *
 * Linux keyboard handling.
 *
 * @author elias_naur <elias_naur@users.sourceforge.net>
 * @version $Revision$
 */

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <string.h>
#include <assert.h>
#include "org_lwjgl_input_Keyboard.h"

#define KEYBOARD_BUFFER_SIZE 50
#define KEYBOARD_SIZE 256
#define KEY_EVENT_BACKLOG 20

unsigned char readBuffer[KEYBOARD_BUFFER_SIZE * 2];
jfieldID fid_readBuffer;
unsigned char key_buf[KEYBOARD_SIZE];
unsigned char key_map[KEYBOARD_SIZE];

XKeyEvent saved_key_events[KEY_EVENT_BACKLOG];
int list_start = 0;
int list_end = 0;

bool keyboard_grabbed;
bool buffer_enabled;
bool translation_enabled;

extern Display *disp;
extern Window win;
extern int current_fullscreen;

extern int isFocused(void);

/*
 * Class:     org_lwjgl_input_Keyboard
 * Method:    initIDs
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_org_lwjgl_input_Keyboard_initIDs
  (JNIEnv * env, jclass clazz)
{
	// Get a global class instance, just to be sure
	static jobject globalClassLock = NULL;

	if (globalClassLock == NULL) {
		globalClassLock = env->NewGlobalRef(clazz);
	}

	fid_readBuffer = env->GetStaticFieldID(clazz, "readBuffer", "Ljava/nio/ByteBuffer;");
}

int grabKeyboard(void) {
	int result = XGrabKeyboard(disp, win, False, GrabModeAsync, GrabModeAsync, CurrentTime);
	if (result == GrabSuccess)
		keyboard_grabbed = true;
	return result;
}

void ungrabKeyboard(void) {
	keyboard_grabbed = false;
	XUngrabKeyboard(disp, CurrentTime);
}

int updateKeyboardGrab(void) {
	if (current_fullscreen) {
		if (!keyboard_grabbed)
			return grabKeyboard();
	} else {
		if (isFocused()) {
			if (!keyboard_grabbed)
				return grabKeyboard();
		} else {
			if (keyboard_grabbed)
				ungrabKeyboard();
		}
	}
	return GrabSuccess;
}

/*
 * Class:     org_lwjgl_input_Keyboard
 * Method:    nCreate
 * Signature: ()Z
 */
JNIEXPORT jboolean JNICALL Java_org_lwjgl_input_Keyboard_nCreate
  (JNIEnv * env, jclass clazz)
{
	keyboard_grabbed = false;
	translation_enabled = false;
	buffer_enabled = false;

	if (updateKeyboardGrab() != GrabSuccess) {
#ifdef _DEBUG
		printf("Could not grab keyboard\n");
#endif
		return JNI_FALSE;
	}
	for (int i =  0; i < KEYBOARD_SIZE; i++)
		key_map[i] = i;
	key_map[0x6b] = 0xdb; // Left doze key
	key_map[0x6c] = 0xdc; // Right doze key
	key_map[0x6d] = 0xdd; // Apps key
	key_map[0x5a] = 0xc8; // Up arrow
	key_map[0x5c] = 0xcb; // Left arrow
	key_map[0x5e] = 0xcd; // Right arrow
	key_map[0x60] = 0xd0; // Down arrow
	key_map[0x59] = 0xc7; // Home
	key_map[0x62] = 0xd2; // Insert
	key_map[0x63] = 0xd3; // Delete
	key_map[0x5f] = 0xcf; // End
	key_map[0x5b] = 0xc9; // Page up
	key_map[0x61] = 0xd1; // Page down
	key_map[0x67] = 0xb7; // SysRQ
	key_map[0x66] = 0xc5; // Pause
	key_map[0x64] = 0x9c; // Numpad enter
	key_map[0x65] = 0x9d; // Right control
	key_map[0x68] = 0xb5; // Numpad divide
	
	memset(key_buf, 0, KEYBOARD_SIZE*sizeof(unsigned char));
	return JNI_TRUE;
}

/*
 * Class:     org_lwjgl_input_Keyboard
 * Method:    nDestroy
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_org_lwjgl_input_Keyboard_nDestroy
  (JNIEnv * env, jclass clazz)
{
	if (keyboard_grabbed)
		ungrabKeyboard();
}

XKeyEvent *nextEventElement(void) {
	if (list_start == list_end)
		return NULL;
	XKeyEvent *result = &(saved_key_events[list_start]);
	list_start = (list_start + 1)%KEY_EVENT_BACKLOG;
	return result;
}

void putEventElement(XKeyEvent *event) {
	int next_index = (list_end + 1)%KEY_EVENT_BACKLOG;
	if (next_index == list_start)
		return;
	saved_key_events[list_end] = *event;
	list_end = next_index;
}

unsigned char getKeycode(XKeyEvent *event) {
	unsigned char keycode = (unsigned char)((event->keycode - 8) & 0xff);
	keycode = key_map[keycode];
	return keycode;
}

int translateEvent(int *position, XKeyEvent *event) {
	static char temp_translation_buffer[KEYBOARD_BUFFER_SIZE];
	static XComposeStatus status;
        int num_chars, i;

	if (*position >= KEYBOARD_BUFFER_SIZE * 2)
		return 0;
       	if (event->type == KeyRelease) {
		readBuffer[(*position)++] = 0;
                readBuffer[(*position)++] = 0;
		return 0;
	}
	num_chars = XLookupString(event, temp_translation_buffer, KEYBOARD_BUFFER_SIZE, NULL, &status);
	if (num_chars > 0) {
		num_chars--;
		/* Assume little endian byte order */
		readBuffer[(*position)++] = temp_translation_buffer[0];
		readBuffer[(*position)++] = 0;
		for (i = 0; i < num_chars; i++) {
			readBuffer[(*position)++] = 0;
			readBuffer[(*position)++] = 0;
	                readBuffer[(*position)++] = temp_translation_buffer[i + 1];
			readBuffer[(*position)++] = 0;
		}
	} else {
		readBuffer[(*position)++] = 0;
                readBuffer[(*position)++] = 0;
	}
	return num_chars;
}

unsigned char eventState(XKeyEvent *event) {
	if (event->type == KeyPress) {
		return 1;
	} else if (event->type == KeyRelease) {
		return 0;
	} else
		assert(0);
}

/*
 * Class:     org_lwjgl_input_Keyboard
 * Method:    nPoll
 * Signature: (I)V
 */
JNIEXPORT void JNICALL Java_org_lwjgl_input_Keyboard_nPoll
  (JNIEnv * env, jclass clazz, jint buf)
{
	XEvent event;
	unsigned char state;
	
	updateKeyboardGrab();

	while (XCheckMaskEvent(disp, KeyPressMask | KeyReleaseMask, &event)) {
		unsigned char keycode = getKeycode(&(event.xkey));
		state = eventState(&(event.xkey));
		key_buf[keycode] = state;
		if (buffer_enabled)
			putEventElement(&(event.xkey));
	}
	memcpy((unsigned char*)buf, key_buf, KEYBOARD_SIZE*sizeof(unsigned char));
}

/*
 * Class:     org_lwjgl_input_Keyboard
 * Method:    nRead
 * Signature: (I)V
 */
JNIEXPORT int JNICALL Java_org_lwjgl_input_Keyboard_nRead
  (JNIEnv * env, jclass clazz)
{
	XEvent event;
	XKeyEvent *key_event;
	int buf_count = 0;
	int state;
	int num_events = 0;

	updateKeyboardGrab();

	while (buf_count < KEYBOARD_BUFFER_SIZE * 2 && (key_event = nextEventElement()) != NULL) {
		num_events++;
		unsigned char keycode = getKeycode(key_event);
		unsigned char state = eventState(key_event);
//		printf("Reading a key %d %d count %d\n", (int)keycode, (int)state, num_events);
		readBuffer[buf_count++] = keycode;
		readBuffer[buf_count++] = state;
		if (translation_enabled)
			num_events += translateEvent(&buf_count, key_event);
	}

	while (buf_count < KEYBOARD_BUFFER_SIZE * 2 && XCheckMaskEvent(disp, KeyPressMask | KeyReleaseMask, &event)) {
		num_events++;
		unsigned char keycode = getKeycode(&(event.xkey));
		unsigned char state = eventState(&(event.xkey));
//		printf("Reading a key %d %d count %d\n", (int)keycode, (int)state, num_events);
		key_buf[keycode] = state;
		readBuffer[buf_count++] = keycode;
		readBuffer[buf_count++] = state;
		if (translation_enabled)
			num_events += translateEvent(&buf_count, &(event.xkey));
	}
	return num_events;
}

/*
 * Class:     org_lwjgl_input_Keyboard
 * Method:    nEnableTranslation
 * Signature: ()I
 */
JNIEXPORT void JNICALL Java_org_lwjgl_input_Keyboard_nEnableTranslation
  (JNIEnv *env, jclass clazz)
{
	translation_enabled = true;
}

/*
 * Class:     org_lwjgl_input_Keyboard
 * Method:    nEnableBuffer
 * Signature: ()I
 */
JNIEXPORT jint JNICALL Java_org_lwjgl_input_Keyboard_nEnableBuffer
  (JNIEnv * env, jclass clazz)
{
	jobject newBuffer = env->NewDirectByteBuffer(&readBuffer, KEYBOARD_BUFFER_SIZE * 2);
	env->SetStaticObjectField(clazz, fid_readBuffer, newBuffer);
	buffer_enabled = true;
	return KEYBOARD_BUFFER_SIZE;
}
