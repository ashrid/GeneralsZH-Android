// GeneralsX @feature Claude 10/07/2026 Task 12 (D7): mod picker menu.
// Scans GameData/Mods/ for subdirectories, populates a ListBox, and writes the
// selected mod path to mod.txt on activate. Cancel pops without changes.
// Pattern: ExtrasMenu.cpp skeleton + updateNotifyButton dynamic button on MainMenu.

#include "PreRTS.h"

#include <vector>

#ifndef _WIN32
#include <dirent.h>
#include <sys/stat.h>
#endif

// GeneralsX @feature Claude 30/07/2026 Android Mods picker: bridge the dynamic
// "Import Folder" button to GameActivity's SAF importer via SDL3's JNI accessors.
// z_gameengine has no SDL3 include path (only the Main target links SDL3), so we
// forward-declare the two C accessors we use; they resolve at link from libSDL3.
// <jni.h> ships in the NDK sysroot. Not built on non-Android targets.
#if defined(__ANDROID__)
#include <jni.h>
extern "C" {
void *SDL_GetAndroidJNIEnv(void);
void *SDL_GetAndroidActivity(void);
}
#endif

#include "Common/GlobalData.h"
#include "Common/NameKeyGenerator.h"
#include "GameClient/Shell.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Display.h"  // GeneralsX @bugfix: scale picker font to TheDisplay->getWidth()
#include "Common/UnicodeString.h"
#include "Common/AsciiString.h"

static NameKeyType buttonActivateID = NAMEKEY_INVALID;
static NameKeyType buttonCancelID = NAMEKEY_INVALID;
static NameKeyType listBoxModsID = NAMEKEY_INVALID;

// GeneralsX @feature Claude 30/07/2026 Android import button (created in Init,
// matched by pointer in System like the Mods button on MainMenu). ID is a unique
// name key so the window is identifiable alongside the WND-defined controls.
static NameKeyType buttonImportID = NAMEKEY_INVALID;
static GameWindow *importButton = nullptr;

static GameWindow *modPickerListBox = nullptr;

static std::vector<AsciiString> s_modPaths;

static void scanModDirectories()
{
	s_modPaths.clear();

#ifndef _WIN32
	DIR *dir = opendir("Mods");
	if (!dir)
		return;

	struct dirent *entry;
	while ((entry = readdir(dir)) != nullptr)
	{
		if (entry->d_name[0] == '.')
			continue;

		AsciiString fullpath = AsciiString("Mods/");
		fullpath.concat(entry->d_name);
		struct stat st;
		if (stat(fullpath.str(), &st) != 0)
			continue;
		if (!S_ISDIR(st.st_mode))
			continue;

		s_modPaths.push_back(fullpath);
	}
	closedir(dir);
#endif
}

static void writeModTxt(const AsciiString &modPath)
{
	FILE *f = fopen("mod.txt", "w");
	if (f)
	{
		fprintf(f, "%s\n", modPath.str());
		fclose(f);
	}
}

static void populateModListBox()
{
	if (!modPickerListBox)
		return;

	GadgetListBoxReset(modPickerListBox);
	for (const auto &modPath : s_modPaths)
	{
		UnicodeString displayText;
		displayText.translate(modPath.str());
		GadgetListBoxAddEntryText(modPickerListBox, displayText, 0xFFFFFFFF, -1);
	}
}

#if defined(__ANDROID__)
// GeneralsX @feature Claude 30/07/2026 JNI bridge to GameActivity. Mirrors the
// SDL3Main.cpp mod-Intent pattern: get the env/activity from SDL3, look up the
// method, call it, clear any pending exception, release the local class ref.
// The activity ref is owned by SDL and must not be deleted. Any failure returns
// silently without blocking the game.
static void requestModFolderImportJni()
{
	JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
	if (!env)
		return;
	jobject activity = (jobject)SDL_GetAndroidActivity();
	if (!activity)
		return;
	jclass cls = env->GetObjectClass(activity);
	if (!cls)
	{
		if (env->ExceptionCheck())
			env->ExceptionClear();
		return;
	}
	jmethodID mid = env->GetMethodID(cls, "requestModFolderImport", "()V");
	if (!mid || env->ExceptionCheck())
	{
		if (env->ExceptionCheck())
			env->ExceptionClear();
		env->DeleteLocalRef(cls);
		return;
	}
	env->CallVoidMethod(activity, mid);
	if (env->ExceptionCheck())
		env->ExceptionClear();
	env->DeleteLocalRef(cls);
}

static bool consumeModImportedJni()
{
	JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
	if (!env)
		return false;
	jobject activity = (jobject)SDL_GetAndroidActivity();
	if (!activity)
		return false;
	jclass cls = env->GetObjectClass(activity);
	if (!cls)
	{
		if (env->ExceptionCheck())
			env->ExceptionClear();
		return false;
	}
	jmethodID mid = env->GetMethodID(cls, "consumeModImported", "()Z");
	if (!mid || env->ExceptionCheck())
	{
		if (env->ExceptionCheck())
			env->ExceptionClear();
		env->DeleteLocalRef(cls);
		return false;
	}
	jboolean result = env->CallBooleanMethod(activity, mid);
	if (env->ExceptionCheck())
	{
		env->ExceptionClear();
		env->DeleteLocalRef(cls);
		return false;
	}
	env->DeleteLocalRef(cls);
	return result == JNI_TRUE;
}
#endif

void ModPickerMenuInit(WindowLayout *layout, void *userData)
{
	buttonActivateID = TheNameKeyGenerator->nameToKey("ModPickerMenu.wnd:ButtonActivate");
	buttonCancelID = TheNameKeyGenerator->nameToKey("ModPickerMenu.wnd:ButtonCancel");
	listBoxModsID = TheNameKeyGenerator->nameToKey("ModPickerMenu.wnd:ListBoxMods");

	modPickerListBox = TheWindowManager->winGetWindowFromId(nullptr, listBoxModsID);

	// GeneralsX @bugfix Claude 30/07/2026 Set literal captions directly on the
	// listbox and buttons. The WND parser routes TEXT through GameText; these
	// keys are absent from the Android string table, so the picker showed
	// "MISSING:" placeholders instead of Mods/Activate/Cancel.
	GameWindow *activateButton = TheWindowManager->winGetWindowFromId(nullptr, buttonActivateID);
	GameWindow *cancelButton = TheWindowManager->winGetWindowFromId(nullptr, buttonCancelID);
	if (activateButton)
		GadgetButtonSetText(activateButton, UnicodeString(L"Activate"));
	if (cancelButton)
		GadgetButtonSetText(cancelButton, UnicodeString(L"Cancel"));
	if (modPickerListBox)
		modPickerListBox->winSetText(UnicodeString(L"Mods"));

	// GeneralsX @bugfix Claude 30/07/2026 Scale the picker font to display width.
	// ModPickerMenu.wnd rectangles are authored in an 800px design coordinate
	// system and the window manager scales them to the real display, but font
	// point sizes do not scale with geometry. On a 1904px Android display the
	// WND's 18pt Arial renders ~18px tall and is unreadable. Recompute the
	// 18pt@800px base against the current display width (clamped to >=18) and
	// assign it to the listbox and Activate/Cancel/Import controls. Menu-local:
	// does not alter global font scaling or WND font values.
	const int pickerBasePointSize = 18;
	const UnsignedInt pickerDesignWidth = 800;
	int pickerPointSize = pickerBasePointSize;
	const UnsignedInt pickerDisplayWidth = TheDisplay->getWidth();
	if (pickerDisplayWidth > 0)
	{
		const double pickerScale = (double)pickerDisplayWidth / (double)pickerDesignWidth;
		pickerPointSize = (int)(pickerScale * (double)pickerBasePointSize + 0.5);
		if (pickerPointSize < pickerBasePointSize)
			pickerPointSize = pickerBasePointSize;
	}
	GameFont *pickerFont = TheWindowManager->winFindFont("Arial", pickerPointSize, FALSE);
	if (pickerFont)
	{
		if (modPickerListBox)
			modPickerListBox->winSetFont(pickerFont);
		if (activateButton)
			activateButton->winSetFont(pickerFont);
		if (cancelButton)
			cancelButton->winSetFont(pickerFont);
	}

	scanModDirectories();
	populateModListBox();

#if defined(__ANDROID__)
	// GeneralsX @feature Claude 30/07/2026 Add "Import Folder" alongside
	// Activate/Cancel. Same row/height as the WND buttons (y450, h30), placed
	// left of Activate. WIN_STATUS_ENABLED without IMAGE selects the color-draw
	// path used by the repaired Activate/Cancel controls.
	if (importButton == nullptr)
	{
		GameWindow *parent = activateButton ? activateButton->winGetParent() : nullptr;
		if (parent)
		{
			buttonImportID = TheNameKeyGenerator->nameToKey("ModPickerMenu.wnd:ButtonImport");

			WinInstanceData instData;
			instData.init();
			BitSet(instData.m_style, GWS_PUSH_BUTTON | GWS_MOUSE_TRACK);
			instData.m_textLabelString = "ModPickerMenu.wnd:ButtonImport";

			importButton = TheWindowManager->gogoGadgetPushButton(
				parent,
				WIN_STATUS_ENABLED,
				140, 450, 120, 30,
				&instData, nullptr, TRUE);

			if (importButton)
			{
				GameFont *font = TheWindowManager->winFindFont("Arial", pickerPointSize, FALSE);
				if (font)
					importButton->winSetFont(font);
				GadgetButtonSetText(importButton, UnicodeString(L"Import Folder"));
			}
		}
	}
#endif
}

void ModPickerMenuUpdate(WindowLayout *layout, void *userData)
{
#if defined(__ANDROID__)
	// GeneralsX @feature Claude 30/07/2026 If GameActivity finished a SAF import,
	// rescan Mods/ and refresh the visible list in place. Selection/apply logic
	// is untouched; the player still picks a row and taps Activate as before.
	const bool imported = consumeModImportedJni();
	if (imported)
	{
		scanModDirectories();
		populateModListBox();
	}
#endif
}

void ModPickerMenuShutdown(WindowLayout *layout, void *userData)
{
#if defined(__ANDROID__)
	// GeneralsX @bugfix Claude 30/07/2026 Destroy the dynamic import button on
	// shutdown so the pointer cannot dangle if the menu is reopened (same fix
	// applied to the Mods button on MainMenu).
	if (importButton)
	{
		TheWindowManager->winDestroy(importButton);
		importButton = nullptr;
	}
	buttonImportID = NAMEKEY_INVALID;
#endif

	modPickerListBox = nullptr;
	s_modPaths.clear();
	layout->hide(TRUE);
	TheShell->shutdownComplete(layout);
}

WindowMsgHandledType ModPickerMenuSystem(GameWindow *window, UnsignedInt msg,
																				 WindowMsgData mData1, WindowMsgData mData2)
{
	switch (msg)
	{
		case GWM_CREATE:
			break;

		case GWM_DESTROY:
			break;

		case GWM_INPUT_FOCUS:
		{
			if (mData1 == TRUE)
				*(Bool *)mData2 = TRUE;
			return MSG_HANDLED;
		}

		case GBM_SELECTED:
		{
			GameWindow *control = (GameWindow *)mData1;
			Int controlID = control->winGetWindowId();

			if (controlID == buttonActivateID)
			{
				if (modPickerListBox && !s_modPaths.empty())
				{
					Int selected = -1;
					GadgetListBoxGetSelected(modPickerListBox, &selected);
					if (selected >= 0 && selected < (Int)s_modPaths.size())
					{
						writeModTxt(s_modPaths[selected]);
					}
				}
				TheShell->pop();
			}
			else if (controlID == buttonCancelID)
			{
				TheShell->pop();
			}
#if defined(__ANDROID__)
			// GeneralsX @feature Claude 30/07/2026 Import button is matched by
			// pointer (like the Mods button on MainMenu); routing it only fires
			// the SAF request and leaves Activate/Cancel exactly as before.
			else if (control == importButton)
			{
				requestModFolderImportJni();
			}
#endif
			break;
		}

		default:
			break;
	}

	return MSG_IGNORED;
}

WindowMsgHandledType ModPickerMenuInput(GameWindow *window, UnsignedInt msg,
																			 WindowMsgData mData1, WindowMsgData mData2)
{
	switch (msg)
	{
		case GWM_CHAR:
		{
			UnsignedByte key = mData1;
			if (key == KEY_ESC)
			{
				TheShell->pop();
				return MSG_HANDLED;
			}
			break;
		}
		default:
			break;
	}
	return MSG_IGNORED;
}
